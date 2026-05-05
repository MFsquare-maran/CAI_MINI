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
#include "battery.h"
#include "log.h"

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

SDCard  sdcard;
LogEntry data;

WiFiClient          wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoardSized<32, 10> tb(mqttClient, MAX_MESSAGE_SIZE);

Battery battery;



// ============================================================
//  ThingsBoard
// ============================================================

void InitTB() {
    logf("Connecting to: ");
    logf(sdcard.cfg.thingsboardServer);
    logf(" with token ");
    logln(sdcard.cfg.accessToken);

    if (!tb.connect(sdcard.cfg.thingsboardServer, sdcard.cfg.accessToken, sdcard.cfg.THINGSBOARD_PORT)) {
        logln("Failed to connect to ThingsBoard");
    } else {
        logln("Connected to ThingsBoard");
    }
}


// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(SERIAL_DEBUG_BAUD);
    delay(5000);

    logln("╔══════════════════════════════╗");
    logln("║   CAI_MINI WIND              ║");
    logln("╚══════════════════════════════╝");
    logf("Firmware Version: ");
    logln(FW_VERSION);

    sdcard.init(SD_CLK, SD_MISO,SD_MOSI,SD_CS);

    // LEDs und Steuer-Pins konfigurieren
    pinMode(LED_BLUE,     OUTPUT);
    pinMode(LED_ORANGE,   OUTPUT);
    pinMode(SHUTDOWN_PIN, OUTPUT);
    digitalWrite(LED_BLUE,     LOW);
    digitalWrite(LED_ORANGE,   HIGH);
    digitalWrite(SHUTDOWN_PIN, LOW);

    bme.begin();
    battery.begin(BATTERY_VOLTAGE);

    sdcard.readIni("/INIT.ini");
   
    bme.set_offset(sdcard.cfg.temperature_offset, sdcard.cfg.Pressure_offset, sdcard.cfg.Huminity_offset, sdcard.cfg.Gas_offset);
    windRain.begin(sdcard.cfg.wind_vane_offset, sdcard.cfg.wind_speed_offset, sdcard.cfg.rain_offset, sdcard.cfg.device_direction, sdcard.cfg.wind_adc_table);

    // CPU auf niedrige Frequenz für stromsparenden Messbetrieb
    setCpuFrequencyMhz(10);
    windRain.enable_interrupts();

    now = millis();

    sending_period = sdcard.cfg.sending_period;

    last_10min = now + sending_period;
}


// ============================================================
//  Loop
// ============================================================
void loop() {

    // --- Testmodus: Wind-ADC-Rohwert per Seriell ausgeben ---
    if (wind_direction_test == 1) {
        logf("Wind Direction ADC Test Value: ");
        logln(windRain.get_wind_direction_raw());
        delay(1000);
        return;
    }

    // --- Normalbetrieb ---
    now = millis();

    if (now - last_10min >= sending_period) {

        logln("Sending");

        windRain.disable_interrupts();

        // CPU hochdrehen für WiFi / Sensor / MQTT
        setCpuFrequencyMhz(80);
        delay(100);

        // --- BME680 auslesen ---
        bme.enable();
        if (bme.readSensor()) {
            data.temperature    = bme.getTemperature();
            data.pressure       = bme.getPressure();
            data.humidity       = bme.getHumidity();
            data.gas_resistance = bme.getGasResistance();
            data.battery_voltage = battery.getVoltage();

            logln("------------------------------------");
            logf("Temperature    = "); logf(data.temperature);    logln(" °C");
            logf("Pressure       = "); logf(data.pressure);       logln(" hPa");
            logf("Humidity       = "); logf(data.humidity);       logln(" %");
            logf("Gas Resistance = "); logf(data.gas_resistance); logln(" kOhms");
            logf("Battery        = "); logf(data.battery_voltage); logln(" V");
            logln("------------------------------------");
        } else {
            logln("Fehler beim Lesen des BME680 Sensors.");
        }
        bme.disable();

        // --- Wind und Regen auslesen, danach Zähler zurücksetzen ---
        data.wind_speed_avg  = windRain.get_wind_average();
        data.wind_speed_gust = windRain.get_wind_gust();
        data.rain_gauge     = windRain.get_rain();
        data.wind_vane       = windRain.get_wind_direction_deg();

        
        

        windRain.reset_all();

        logln("------------------------------------");
        logf("Wind Direction  = "); logf(data.wind_vane);        logln(" °");
        logf("Wind Avg        = "); logf(data.wind_speed_avg);   logln(" m/s");
        logf("Wind Gust       = "); logf(data.wind_speed_gust);  logln(" m/s");
        logf("Rain Amount     = "); logf(data.rain_gauge);      logln(" mm");
        logln("------------------------------------");

        digitalWrite(LED_BLUE, HIGH);


        // --- WiFi verbinden ---
        if (InitWiFi(sdcard.cfg.ssid,sdcard.cfg.password) == false) {
            last_10min = now;
            logln("Probiere später nochmals");
            digitalWrite(LED_BLUE, LOW);
            // trotzdem loggen, aber ohne Zeit:
            sdcard.writeLog(data,"/data.csv");
            windRain.enable_interrupts();
    
            return;
        }

        // --- Zeit synchronisieren ---
        LocalTime(datetime,&timeinfo);
        data.datetime = datetime;

        // --- Daten auf SD-Karte loggen ---
        sdcard.writeLog(data,"/data.csv");

        // --- Firmware-Update prüfen ---
        logln("\n🔧 Checking for firmware updates...");
        updater.checkAndUpdate(sdcard.cfg.thingsboardServer, sdcard.cfg.accessToken, FW_VERSION, 1);

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
        tb.sendTelemetryData("Temperature",     round(data.temperature    * 100.0) / 100.0);
        tb.sendTelemetryData("Pressure",        round(data.pressure       * 100.0) / 100.0);
        tb.sendTelemetryData("Humidity",        round(data.humidity       * 100.0) / 100.0);
        tb.sendTelemetryData("Gas_Resistance",  round(data.gas_resistance  * 100.0) / 100.0);
        tb.sendTelemetryData("Battery_Voltage", round(data.battery_voltage         * 100.0) / 100.0);
        tb.sendTelemetryData("Wind_Vane",       round(data.wind_vane             * 100.0) / 100.0);
        tb.sendTelemetryData("Wind_Speed_Avg",  round(data.wind_speed_avg          * 100.0) / 100.0);
        tb.sendTelemetryData("Wind_Speed_Gust", round(data.wind_speed_gust         * 100.0) / 100.0);
        tb.sendTelemetryData("Rain_Gauge",      round(data.rain_gauge             * 100.0) / 100.0);

        tb.loop();       // MQTT-Puffer leeren (Daten werden erst hier wirklich gesendet)
        delay(1000);
        tb.disconnect(); // MQTT-Verbindung sauber schliessen

        disconnectWiFi(&wifiClient);
        digitalWrite(LED_BLUE, LOW);

        // CPU wieder auf Sparmodus
        setCpuFrequencyMhz(10);

        last_10min = now;
        windRain.enable_interrupts();
    }
}
