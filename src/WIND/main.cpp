/*
 * ============================================================
 *  CAI_MINI — WIND
 * ============================================================
 *  Beschreibung : Verbindet sich per WLAN und sendet
 *                 Sensordaten (BME680) via MQTT / ThingsBoard
 *  Board        : Seeed XIAO ESP32-S3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 * ============================================================
 */

// ============================================================
//  Includes
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "config_WIND.h"
#include "time.h"
#include "BME680_Sensor.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "FirmwareUpdater.h"
#include <IniFile.h>
#include "wind_rain.h"
#include "soc/rtc.h"
#include "wifi_functions.h"
#include "time_functions.h"
#include "sdcard.h"


// ============================================================
//  Konstanten
// ============================================================
constexpr uint32_t MAX_MESSAGE_SIZE    = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD   = 115200U;




// ============================================================
//  Netzwerk- und ThingsBoard-Konfiguration (aus INI geladen)
// ============================================================
char     ssid[64];
char     password[64];
char     thingsboardServer[64];
char     accessToken[64];
uint16_t THINGSBOARD_PORT;


// ============================================================
//  Timing
// ============================================================
unsigned long sending_period = 30000;// Standard: 30 Sek. in Millisekunden
unsigned long last_10min     = 0;
unsigned long now            = 0;
unsigned long last_update    = 0;


// ============================================================
//  Sensormesswerte
// ============================================================
float temperature      = 0.0;
float pressure         = 0.0;
float humidity         = 0.0;
float gas_resistance   = 0.0;
float battery_voltage  = 0.0;

float wind_vane        = 0.0;  // Windrichtung in Grad
float wind_speed_avg   = 0.0;  // Mittlere Windgeschwindigkeit [m/s]
float wind_speed_gust  = 0.0;  // Böengeschwindigkeit [m/s]
float rain_amount      = 0.0;  // Regenmenge [mm]
uint16_t rain_count    = 0;
uint16_t wind_count    = 0;


// ============================================================
//  Kalibrierungsoffsets (aus INI-Datei)
// ============================================================
float temperature_offset = 0.0;
float Pressure_offset    = 0.0;
float Huminity_offset    = 0.0;
float Gas_offset         = 0.0;
float wind_vane_offset   = 0.0;
float wind_speed_offset  = 0.0;
float rain_offset        = 0.0;

// Ausrichtung des Geräts (0° = Norden, 90° = Osten, …)
float device_direction   = 0.0;

// ADC-Lookup-Tabelle für 16 Windrichtungen (je 22,5°)
uint16_t wind_adc_table[16];

// Testmodus: Wind-ADC-Rohwert per Seriell ausgeben (1 = aktiv)
uint8_t wind_direction_test = 0;

// ============================================================
//  Time
// ============================================================

struct tm timeinfo;
char datetime[30]; // Formatierter Zeitstring (YYYY-MM-DD HH:MM:SS)


// ============================================================
//  Objekte
// ============================================================
BME680_Sensor bme;
wind_rain     windRain;
FirmwareUpdater updater;

IniFile ini("/INIT.ini", FILE_READ, true);
File    logfile;

WiFiClient          wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoardSized<32, 10> tb(mqttClient, MAX_MESSAGE_SIZE);





// Schreibt eine Zeile Messdaten in /data.csv; erstellt Datei mit Header falls nötig
void write_logdata(float temperature, float pressure, float humidity,
                   float gas_resistance, float battery_voltage,
                   float wind_vane, float wind_speed_avg,
                   float wind_speed_gust, float rain_gauge)
{
    if (!SD.exists("/data.csv")) {
        logfile = SD.open("/data.csv", FILE_WRITE);
        logfile.println("Date,Temperature,Pressure,Humidity,Gas_Resistance,Battery_Voltage,Wind_Vane,Wind_Speed_Avg,Wind_Speed_Gust,Rain_Gauge");
        logfile.close();
        Serial.println("CSV neu erstellt mit Header.");
    }

    logfile = SD.open("/data.csv", FILE_APPEND);
    if (logfile) {
        logfile.print(datetime);         logfile.print(",");  // leer falls keine Zeit
        logfile.print(temperature);      logfile.print(",");
        logfile.print(pressure);         logfile.print(",");
        logfile.print(humidity);         logfile.print(",");
        logfile.print(gas_resistance);   logfile.print(",");
        logfile.print(battery_voltage);  logfile.print(",");
        logfile.print(wind_vane);        logfile.print(",");
        logfile.print(wind_speed_avg);   logfile.print(",");
        logfile.print(wind_speed_gust);  logfile.print(",");
        logfile.println(rain_gauge);
        logfile.close();
    }
}


// ============================================================
//  INI-Datei
// ============================================================

void get_ini_values() {
    if (!ini.open()) {
        Serial.println("❌ INI-Datei konnte nicht geöffnet werden!");
        while (1);
    }

    char buffer[128];

    // Allgemein
    if (ini.getValue("GENERAL",      "SENDING_PERIOD",     buffer, sizeof(buffer))) sending_period    = atoi(buffer) * 1000 * 60;

    // WiFi
    if (ini.getValue("WIFI",         "SSID",               buffer, sizeof(buffer))) strcpy(ssid,      buffer);
    if (ini.getValue("WIFI",         "PW",                 buffer, sizeof(buffer))) strcpy(password,  buffer);

    // ThingsBoard
    if (ini.getValue("THINGSBOARD",  "TB_ADRESS",          buffer, sizeof(buffer))) strcpy(thingsboardServer, buffer);
    if (ini.getValue("THINGSBOARD",  "TB_TOKKEN",          buffer, sizeof(buffer))) strcpy(accessToken,       buffer);
    if (ini.getValue("THINGSBOARD",  "TB_PORT",            buffer, sizeof(buffer))) THINGSBOARD_PORT = atoi(buffer);

    // BME680-Offsets
    if (ini.getValue("BME680",       "TEMPERATURE_OFFSET", buffer, sizeof(buffer))) temperature_offset = strtof(buffer, nullptr);
    if (ini.getValue("BME680",       "PRESSURE_OFFSET",    buffer, sizeof(buffer))) Pressure_offset    = strtof(buffer, nullptr);
    if (ini.getValue("BME680",       "HUMINITY_OFFSET",    buffer, sizeof(buffer))) Huminity_offset    = strtof(buffer, nullptr);
    if (ini.getValue("BME680",       "GAS_OFFSET",         buffer, sizeof(buffer))) Gas_offset         = strtof(buffer, nullptr);

    // Wind- und Regenkonfiguration
    if (ini.getValue("WIND",         "DEVICE_DIRECTION",   buffer, sizeof(buffer))) device_direction   = strtof(buffer, nullptr);
    if (ini.getValue("WIND",         "WIND_VANE_OFFSET",   buffer, sizeof(buffer))) wind_vane_offset   = strtof(buffer, nullptr);
    if (ini.getValue("WIND",         "WIND_SPEED_OFFSET",  buffer, sizeof(buffer))) wind_speed_offset  = strtof(buffer, nullptr);
    if (ini.getValue("RAIN",         "RAIN_OFFSET",        buffer, sizeof(buffer))) rain_offset        = strtof(buffer, nullptr);

    // ADC-Tabelle für 16 Windrichtungen (0° bis 337,5° in 22,5°-Schritten)
    for (size_t i = 0; i < 16; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", (int)i);
        if (ini.getValue("WINDADCVALUES", key, buffer, sizeof(buffer)))
            wind_adc_table[i] = strtof(buffer, nullptr);
    }

    if (ini.getValue("WINDADCVALUES", "WIND_Direction_TEST", buffer, sizeof(buffer))) wind_direction_test = strtof(buffer, nullptr);

    // Debug-Ausgabe
    Serial.println("✅ INI-Werte gelesen:");
    Serial.print("  Sending Period (ms): "); Serial.println(sending_period);
    Serial.print("  SSID: ");               Serial.println(ssid);
    Serial.print("  Password: ");           Serial.println(password);
    Serial.print("  TB Server: ");          Serial.println(thingsboardServer);
    Serial.print("  TB Token: ");           Serial.println(accessToken);
    Serial.print("  TB Port: ");            Serial.println(THINGSBOARD_PORT);
    Serial.print("  Temperature Offset: "); Serial.println(temperature_offset);
    Serial.print("  Pressure Offset: ");    Serial.println(Pressure_offset);
    Serial.print("  Humidity Offset: ");    Serial.println(Huminity_offset);
    Serial.print("  Gas Offset: ");         Serial.println(Gas_offset);
    Serial.print("  Device Direction: ");   Serial.println(device_direction);
    Serial.print("  Wind Vane Offset: ");   Serial.println(wind_vane_offset);
    Serial.print("  Wind Speed Offset: ");  Serial.println(wind_speed_offset);
    Serial.print("  Rain Offset: ");        Serial.println(rain_offset);
    Serial.println("  Wind ADC Table:");
    for (int i = 0; i < 16; i++) {
        Serial.print("    "); Serial.print(i * 22.5); Serial.print("° → ADC: ");
        Serial.println(wind_adc_table[i]);
    }

    Serial.print("  Wind Direction Test: "); Serial.println(wind_direction_test);

    ini.close();
}


// ============================================================
//  ThingsBoard
// ============================================================

void InitTB() {
    Serial.print("Connecting to: ");
    Serial.print(thingsboardServer);
    Serial.print(" with token ");
    Serial.println(accessToken);

    if (!tb.connect(thingsboardServer, accessToken, THINGSBOARD_PORT)) {
        Serial.println("Failed to connect to ThingsBoard");
    } else {
        Serial.println("Connected to ThingsBoard");
    }
}


// ============================================================
//  Akkuspannung (ADC mit linearer Interpolation)
// ============================================================

float getBatteryVoltage() {
    const uint8_t  N_POINTS = 8;
    const float    voltVals[N_POINTS] = { 3.5, 3.6, 3.7, 3.8, 3.9, 4.0, 4.1, 4.2 };
    const uint16_t adcVals[N_POINTS]  = { 2644, 2720, 2801, 2889, 2972, 3060, 3155, 3249 };

    uint16_t adcValue = analogRead(BATTERY_VOLTAGE);

    // Interpolation innerhalb der Tabelle
    for (uint8_t i = 0; i < N_POINTS - 1; i++) {
        if (adcValue >= adcVals[i] && adcValue <= adcVals[i + 1]) {
            float a1 = adcVals[i],     a2 = adcVals[i + 1];
            float v1 = voltVals[i],    v2 = voltVals[i + 1];
            return v1 + (adcValue - a1) * (v2 - v1) / (a2 - a1);
        }
    }

    // Extrapolation unterhalb der Tabelle
    if (adcValue < adcVals[0]) {
        float a1 = adcVals[0],        a2 = adcVals[1];
        float v1 = voltVals[0],       v2 = voltVals[1];
        return v1 + (adcValue - a1) * (v2 - v1) / (a2 - a1);
    }

    // Extrapolation oberhalb der Tabelle
    {
        float a1 = adcVals[N_POINTS - 2], a2 = adcVals[N_POINTS - 1];
        float v1 = voltVals[N_POINTS - 2], v2 = voltVals[N_POINTS - 1];
        return v1 + (adcValue - a1) * (v2 - v1) / (a2 - a1);
    }
}


// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(SERIAL_DEBUG_BAUD);
    delay(5000);

    Serial.println("╔══════════════════════════════╗");
    Serial.println("║   CAI_MINI WIND              ║");
    Serial.println("╚══════════════════════════════╝");
    Serial.print("Firmware Version: ");
    Serial.println(FW_VERSION);

    InitSD(SD_CLK, SD_MISO,SD_MOSI,SD_CS);

    // LEDs und Steuer-Pins konfigurieren
    pinMode(LED_BLUE,     OUTPUT);
    pinMode(LED_ORANGE,   OUTPUT);
    pinMode(SHUTDOWN_PIN, OUTPUT);
    digitalWrite(LED_BLUE,     LOW);
    digitalWrite(LED_ORANGE,   HIGH);
    digitalWrite(SHUTDOWN_PIN, LOW);

    bme.begin();

    get_ini_values();
    bme.set_offset(temperature_offset, Pressure_offset, Huminity_offset, Gas_offset);
    windRain.begin(wind_vane_offset, wind_speed_offset, rain_offset, device_direction, wind_adc_table);

    // CPU auf niedrige Frequenz für stromsparenden Messbetrieb
    setCpuFrequencyMhz(10);
    windRain.enable_interrupts();

    now = millis();
    last_10min = now + sending_period;
}


// ============================================================
//  Loop
// ============================================================
void loop() {

    // --- Testmodus: Wind-ADC-Rohwert per Seriell ausgeben ---
    if (wind_direction_test == 1) {
        Serial.print("Wind Direction ADC Test Value: ");
        Serial.println(windRain.get_wind_direction_raw());
        delay(1000);
        return;
    }

    // --- Normalbetrieb ---
    now = millis();

    if (now - last_10min >= sending_period) {

        Serial.println("Sending");

        windRain.disable_interrupts();

        // CPU hochdrehen für WiFi / Sensor / MQTT
        setCpuFrequencyMhz(80);
        delay(100);

        // --- BME680 auslesen ---
        bme.enable();
        if (bme.readSensor()) {
            temperature    = bme.getTemperature();
            pressure       = bme.getPressure();
            humidity       = bme.getHumidity();
            gas_resistance = bme.getGasResistance();
            battery_voltage = getBatteryVoltage();

            Serial.println("------------------------------------");
            Serial.print("Temperature    = "); Serial.print(temperature);    Serial.println(" °C");
            Serial.print("Pressure       = "); Serial.print(pressure);       Serial.println(" hPa");
            Serial.print("Humidity       = "); Serial.print(humidity);       Serial.println(" %");
            Serial.print("Gas Resistance = "); Serial.print(gas_resistance); Serial.println(" kOhms");
            Serial.print("Battery        = "); Serial.print(battery_voltage); Serial.println(" V");
            Serial.println("------------------------------------");
        } else {
            Serial.println("Fehler beim Lesen des BME680 Sensors.");
        }
        bme.disable();

        // --- Wind und Regen auslesen, danach Zähler zurücksetzen ---
        wind_speed_avg  = windRain.get_wind_average();
        wind_speed_gust = windRain.get_wind_gust();
        rain_amount     = windRain.get_rain();
        wind_vane       = windRain.get_wind_direction_deg();
        wind_count      = windRain.get_wind_count();
        rain_count      = windRain.get_rain_count();
        windRain.reset_all();

        Serial.println("------------------------------------");
        Serial.print("Wind Direction  = "); Serial.print(wind_vane);        Serial.println(" °");
        Serial.print("Wind Avg        = "); Serial.print(wind_speed_avg);   Serial.println(" m/s");
        Serial.print("Wind Gust       = "); Serial.print(wind_speed_gust);  Serial.println(" m/s");
        Serial.print("Wind cnt        = "); Serial.print(wind_count);      Serial.println(" cnt");
        Serial.print("Rain Amount     = "); Serial.print(rain_amount);      Serial.println(" mm");
        Serial.print("Rain cnt        = "); Serial.print(rain_count);      Serial.println(" cnt");
        Serial.println("------------------------------------");

        digitalWrite(LED_BLUE, HIGH);


        // --- WiFi verbinden ---
        if (InitWiFi(ssid,password) == false) {
            last_10min = now;
            Serial.println("Probiere später nochmals");
            digitalWrite(LED_BLUE, LOW);
            // trotzdem loggen, aber ohne Zeit:
            write_logdata(temperature, pressure, humidity, gas_resistance,
                        battery_voltage, wind_vane, wind_speed_avg,
                        wind_speed_gust, rain_amount);
            windRain.enable_interrupts();
    
            return;
        }

        // --- Zeit synchronisieren ---
        LocalTime(datetime,&timeinfo);

        // --- Daten auf SD-Karte loggen ---
        write_logdata(temperature, pressure, humidity, gas_resistance,
                    battery_voltage, wind_vane, wind_speed_avg,
                    wind_speed_gust, rain_amount);

        // --- Firmware-Update prüfen ---
        Serial.println("\n🔧 Checking for firmware updates...");
        updater.checkAndUpdate(thingsboardServer, accessToken, FW_VERSION, 1);

        // --- Daten an ThingsBoard senden ---
        InitTB();

        // Geräteattribute (Netzwerk & Firmware)
        tb.sendAttributeData("rssi",      WiFi.RSSI());
        tb.sendAttributeData("channel",   WiFi.channel());
        tb.sendAttributeData("bssid",     WiFi.BSSIDstr().c_str());
        tb.sendAttributeData("localIp",   WiFi.localIP().toString().c_str());
        tb.sendAttributeData("ssid",      WiFi.SSID().c_str());
        tb.sendAttributeData("fwversion", FW_VERSION);

        // Telemetriedaten (auf 2 Nachkommastellen gerundet)
        tb.sendTelemetryData("Temperature",     round(bme.getTemperature()    * 100.0) / 100.0);
        tb.sendTelemetryData("Pressure",        round(bme.getPressure()       * 100.0) / 100.0);
        tb.sendTelemetryData("Humidity",        round(bme.getHumidity()       * 100.0) / 100.0);
        tb.sendTelemetryData("Gas_Resistance",  round(bme.getGasResistance()  * 100.0) / 100.0);
        tb.sendTelemetryData("Battery_Voltage", round(battery_voltage         * 100.0) / 100.0);
        tb.sendTelemetryData("Wind_Vane",       round(wind_vane               * 100.0) / 100.0);
        tb.sendTelemetryData("Wind_Speed_Avg",  round(wind_speed_avg          * 100.0) / 100.0);
        tb.sendTelemetryData("Wind_Speed_Gust", round(wind_speed_gust         * 100.0) / 100.0);
        tb.sendTelemetryData("Rain_Gauge",      round(rain_amount             * 100.0) / 100.0);

        tb.loop();       // MQTT-Puffer leeren (Daten werden erst hier wirklich gesendet)
        tb.disconnect(); // MQTT-Verbindung sauber schliessen

        disconnectWiFi(&wifiClient);
        digitalWrite(LED_BLUE, LOW);

        // CPU wieder auf Sparmodus
        setCpuFrequencyMhz(10);

        last_10min = now;
        windRain.enable_interrupts();
    }
}
