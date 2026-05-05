// ============================================================
//  main.cpp
//  CAI_MINI LoRa Sensor
//  BME680 + SX1262 via RadioLib
//  Datum: 2026-04-16
// ============================================================

#include "config_LORA_SENSOR.h"
#include <Arduino.h>
#include "LORA.h"
#include "BME680_Sensor.h"
#include <SPI.h>
#include "battery.h"
#include "sdcard.h"
#include "esp_wifi.h"
#include "wifi_functions.h"
#include "FirmwareUpdater.h"
#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "log.h"

// ============================================================
//  Objekte
// ============================================================
SPIClass spi_sd(HSPI);
SPIClass spi_lora(FSPI);

BME680_Sensor bme;
Battery       battery;
SDCard        sdcard;
FirmwareUpdater updater;


// ============================================================
//  Konstanten
// ============================================================
constexpr uint32_t MAX_MESSAGE_SIZE    = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD   = 115200U;


WiFiClient          wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoardSized<32, 10> tb(mqttClient, MAX_MESSAGE_SIZE);


LORA Lora_sensor(
    LORA_NSS,
    LORA_DIO1,
    LORA_RESET,
    LORA_BUSY,
    spi_lora,
    LORA_SCK,
    LORA_MISO,
    LORA_MOSI,
    3,      // maxRetries
    500,    // retryDelay  [ms]
    2000    // ackTimeout  [ms]
);

// ============================================================
//  Zeitstempel letztes Senden
// ============================================================
unsigned long last_send = 0;
unsigned long sending_period = 1;// Standard: 1Min. 





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
//  CPU Frequenz helpers
// ============================================================
void setCpuLow()
{
    setCpuFrequencyMhz(10);
}

void setCpuHigh()
{
    setCpuFrequencyMhz(240);
}

// ============================================================
//  Sensordaten lesen + ausgeben
// ============================================================
bool readSensors(float &temperature, float &pressure, float &humidity,
                 float &gas_resistance, float &battery_voltage)
{
    if (!bme.readSensor()) {
        logln("[BME680] FEHLER beim Lesen!");
        return false;
    }

    temperature     = bme.getTemperature();
    pressure        = bme.getPressure();
    humidity        = bme.getHumidity();
    gas_resistance  = bme.getGasResistance();
    battery_voltage = battery.getVoltage();

    logln("[BME680] Messwerte:");
    logln("         Temperatur:    " + String(temperature,    2) + " °C");
    logln("         Luftdruck:     " + String(pressure,       2) + " hPa");
    logln("         Luftfeuchte:   " + String(humidity,       2) + " %");
    logln("         Gaswiderstand: " + String(gas_resistance, 2) + " kOhm");
    logln("         Akku:          " + String(battery_voltage,2) + " V");
    logln("         --------------------------------");

    return true;
}

// ============================================================
//  LoRa-Paket zusammenbauen
// ============================================================
String buildPayload(float temperature, float pressure, float humidity,
                    float gas_resistance, float battery_voltage)
{
    return "Token:"            + String(sdcard.cfg.accessToken)                          +
           ";Temperature:"     + String(round(temperature    * 100.0) / 100.0) +
           ";Pressure:"        + String(round(pressure       * 100.0) / 100.0) +
           ";Humidity:"        + String(round(humidity       * 100.0) / 100.0) +
           ";Gas_Resistance:"  + String(round(gas_resistance * 100.0) / 100.0) +
           ";Battery_Voltage:" + String(round(battery_voltage* 100.0) / 100.0) +
           ";RSSI:";            // ← leer, Router füllt es
}

// ============================================================
//  Messen + Senden
// ============================================================
void measureAndSend()
{
    setCpuHigh();

    // ── LoRa aufwecken ────────────────────────────────────────
    logln("[SENSOR] Wecke LoRa auf...");
    if (!Lora_sensor.begin(sdcard.cfg.DeviceID)) {
        logln("[LORA] KRITISCH: Initialisierung fehlgeschlagen!");
        setCpuLow();
        return;
    }

    // ── Messen ────────────────────────────────────────────────
    float temperature, pressure, humidity, gas_resistance, battery_voltage;

    if (!readSensors(temperature, pressure, humidity, gas_resistance, battery_voltage)) {
        logln("[SENSOR] Sensor Fehler – überspringe Sendung.");
    } else {
        // ── Senden ────────────────────────────────────────────
        digitalWrite(LED_BLUE, HIGH);
        bool ok = Lora_sensor.transmit(sdcard.cfg.SenderID,
                      buildPayload(temperature, pressure, humidity,
                                   gas_resistance, battery_voltage));
        digitalWrite(LED_BLUE, LOW);

        if (ok) {
            logln("[SENSOR] ✅ Paket erfolgreich gesendet.");
            logln("         Gesendet gesamt: " + String(Lora_sensor.getSentCount()));
        } else {
            logln("[SENSOR] ❌ Senden fehlgeschlagen nach allen Versuchen.");
        }
    }

    // ── LoRa schlafen schicken ────────────────────────────────
    logln("[SENSOR] LoRa → Sleep.");
    Lora_sensor.sleepRadio(); // nur Radio schlafen, kein ESP32-Sleep

    last_send = millis();
    logln("[SENSOR] Gute Nacht! Warte auf nächsten Zyklus...");
    setCpuLow();
}

// ============================================================
//  setup()
// ============================================================
void setup()
{
    setCpuFrequencyMhz(240);
    Serial.begin(115200);
    delay(3000);

    logln("╔══════════════════════════════╗");
    logln("║   CAI_MINI LoRa Sensor       ║");
    logln("╚══════════════════════════════╝");
    logf("Firmware Version: ");
    logln(FW_VERSION);

    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_BLUE, LOW);

    // ── WiFi deaktivieren ─────────────────────────────────────
    esp_wifi_stop();

    // ── SD + INI ──────────────────────────────────────────────
    sdcard.init(SD_CLK, SD_MISO, SD_MOSI, SD_CS, spi_sd);
    sdcard.readIni("/INIT.ini");

    // ── Sensoren ──────────────────────────────────────────────
    battery.begin(BATTERY_VOLTAGE);
    bme.begin();
    bme.set_offset(sdcard.cfg.temperature_offset,
                   sdcard.cfg.Pressure_offset,
                   sdcard.cfg.Huminity_offset,
                   sdcard.cfg.Gas_offset);

    sending_period = sdcard.cfg.sending_period;

    // ── Erstes Senden sofort beim Start ───────────────────────
    last_send = millis() + (sending_period);

    setCpuLow();
}

// ============================================================
//  loop()
// ============================================================
void loop()
{
    if (millis() - last_send >= sending_period )
    {   
        setCpuHigh();

        if (InitWiFi(sdcard.cfg.ssid,sdcard.cfg.password)) 
        {
            logln("\n🔧 Checking for firmware updates...");
            updater.checkAndUpdate(sdcard.cfg.thingsboardServer, sdcard.cfg.accessToken, FW_VERSION, 1);
            InitTB();
            tb.sendAttributeData("channel",   WiFi.channel());
            tb.sendAttributeData("bssid",     WiFi.BSSIDstr().c_str());
            tb.sendAttributeData("localIp",   WiFi.localIP().toString().c_str());
            tb.sendAttributeData("ssid",      WiFi.SSID().c_str());
            tb.sendAttributeData("fwversion", FW_VERSION);

            tb.loop();       // MQTT-Puffer leeren (Daten werden erst hier wirklich gesendet)
            delay(1000);
            tb.disconnect(); // MQTT-Verbindung sauber schliessen
            delay(1000);
            disconnectWiFi(&wifiClient);
            esp_wifi_stop();
            measureAndSend();

        }else{
            measureAndSend(); // senden über LORA
        }

        
    }

    // ── Kurz yielden ──────────────────────────────────────────
    delay(10);
}