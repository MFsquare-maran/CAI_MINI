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

// ============================================================
//  Objekte
// ============================================================
SPIClass spi_sd(HSPI);
SPIClass spi_lora(FSPI);

BME680_Sensor bme;
Battery       battery;
SDCard        sdcard;

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
        Serial.println("[BME680] FEHLER beim Lesen!");
        return false;
    }

    temperature     = bme.getTemperature();
    pressure        = bme.getPressure();
    humidity        = bme.getHumidity();
    gas_resistance  = bme.getGasResistance();
    battery_voltage = battery.getVoltage();

    Serial.println("[BME680] Messwerte:");
    Serial.println("         Temperatur:    " + String(temperature,    2) + " °C");
    Serial.println("         Luftdruck:     " + String(pressure,       2) + " hPa");
    Serial.println("         Luftfeuchte:   " + String(humidity,       2) + " %");
    Serial.println("         Gaswiderstand: " + String(gas_resistance, 2) + " kOhm");
    Serial.println("         Akku:          " + String(battery_voltage,2) + " V");
    Serial.println("         --------------------------------");

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
           ";Battery_Voltage:" + String(round(battery_voltage* 100.0) / 100.0);
}

// ============================================================
//  Messen + Senden
// ============================================================
void measureAndSend()
{
    setCpuHigh();

    // ── LoRa aufwecken ────────────────────────────────────────
    Serial.println("[SENSOR] Wecke LoRa auf...");
    if (!Lora_sensor.begin(sdcard.cfg.DeviceID)) {
        Serial.println("[LORA] KRITISCH: Initialisierung fehlgeschlagen!");
        setCpuLow();
        return;
    }

    // ── Messen ────────────────────────────────────────────────
    float temperature, pressure, humidity, gas_resistance, battery_voltage;

    if (!readSensors(temperature, pressure, humidity, gas_resistance, battery_voltage)) {
        Serial.println("[SENSOR] Sensor Fehler – überspringe Sendung.");
    } else {
        // ── Senden ────────────────────────────────────────────
        digitalWrite(LED_BLUE, HIGH);
        bool ok = Lora_sensor.transmit(sdcard.cfg.SenderID,
                      buildPayload(temperature, pressure, humidity,
                                   gas_resistance, battery_voltage));
        digitalWrite(LED_BLUE, LOW);

        if (ok) {
            Serial.println("[SENSOR] ✅ Paket erfolgreich gesendet.");
            Serial.println("         Gesendet gesamt: " + String(Lora_sensor.getSentCount()));
        } else {
            Serial.println("[SENSOR] ❌ Senden fehlgeschlagen nach allen Versuchen.");
        }
    }

    // ── LoRa schlafen schicken ────────────────────────────────
    Serial.println("[SENSOR] LoRa → Sleep.");
    Lora_sensor.sleepRadio(); // nur Radio schlafen, kein ESP32-Sleep

    last_send = millis();
    Serial.println("[SENSOR] Gute Nacht! Warte auf nächsten Zyklus...");
    setCpuLow();
}

// ============================================================
//  setup()
// ============================================================
void setup()
{
    setCpuFrequencyMhz(240);
    Serial.begin(115200);
    delay(500);

    Serial.println("╔══════════════════════════════╗");
    Serial.println("║   CAI_MINI LoRa Sensor       ║");
    Serial.println("╚══════════════════════════════╝");
    Serial.print("Firmware Version: ");
    Serial.println(FW_VERSION);

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

    // ── Erstes Senden sofort beim Start ───────────────────────
    measureAndSend();
}

// ============================================================
//  loop()
// ============================================================
void loop()
{
    if (millis() - last_send >= (unsigned long)SEND_INTERVAL_SEC * 1000UL)
    {
        measureAndSend();
    }

    // ── Kurz yielden ──────────────────────────────────────────
    delay(10);
}