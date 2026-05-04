// ============================================================
//  main.cpp
//  CAI_MINI LoRa Router
//  SX1262 via RadioLib
//  Datum: 2026-04-16
// ============================================================

#include "config_LORA_ROUTER.h"
#include <Arduino.h>
#include <SPI.h>
#include "LORA.h"
#include "sdcard.h"
#include "battery.h"
#include "BME680_Sensor.h"
#include "esp_pm.h"
#include "esp_wifi.h"

// ============================================================
//  Objekte
// ============================================================
SPIClass spi_lora(FSPI);
SPIClass spi_sd(HSPI);

LORA Lora_router(
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

SDCard        sdcard;
Battery       battery;
BME680_Sensor bme;

// ============================================================
//  Zeitstempel für eigenes Paket
// ============================================================
unsigned long last_own_send = 0;

// ============================================================
//  CPU Frequenz helpers
// ============================================================
void setCpuLow()
{
    setCpuFrequencyMhz(10);
    Serial.println("[PM] CPU → 10 MHz (Idle)");
}

void setCpuHigh()
{
    setCpuFrequencyMhz(240);
    Serial.println("[PM] CPU → 240 MHz (Aktiv)");
}


// ============================================================
//  Fremdes Paket weiterleiten
// ============================================================
void forwardPacket()
{
    setCpuHigh();

    String sender = Lora_router.readSender();
    String data   = Lora_router.readData();
    float  rssi   = Lora_router.getLastRSSI();  // ← RSSI des empfangenen Pakets

    Serial.println("[ROUTER] Paket empfangen von: " + sender);
    Serial.println("[ROUTER] RSSI vom Sensor:      " + String(rssi, 1) + " dBm");

    // ── RSSI anhängen (leeres Feld ersetzen oder hinzufügen) ──
    if (data.endsWith("RSSI:")) {
        // Sensor hat leeres RSSI-Feld → befüllen
        data += String(round(rssi * 10.0) / 10.0);
    } else if (data.indexOf("RSSI:") == -1) {
        // kein RSSI-Feld vorhanden → anhängen
        data += ";RSSI:" + String(round(rssi * 10.0) / 10.0);
    }
    // sonst: RSSI bereits befüllt → nicht überschreiben

    Serial.println("[ROUTER] Weiterleiten: " + data);

    digitalWrite(LED_BLUE, HIGH);
    bool ok = Lora_router.transmit(sdcard.cfg.SenderID, data);
    digitalWrite(LED_BLUE, LOW);

    if (ok) Serial.println("[ROUTER] ✅ Paket weitergeleitet.");
    else    Serial.println("[ROUTER] ❌ Weiterleiten fehlgeschlagen.");

    setCpuLow();
}

// ============================================================
//  Eigenes Paket senden
// ============================================================
void sendOwnPacket()
{
    setCpuHigh();
    Serial.println("[ROUTER] Sende eigenes Paket...");

    if (!bme.readSensor()) {
        Serial.println("[BME680] FEHLER beim Lesen!");
        setCpuLow();
        return;
    }

    float temperature     = bme.getTemperature();
    float pressure        = bme.getPressure();
    float humidity        = bme.getHumidity();
    float gas_resistance  = bme.getGasResistance();
    float battery_voltage = battery.getVoltage();

    String payload =
        "Token:"            + String(sdcard.cfg.accessToken)                               +
        ";Temperature:"     + String(round(temperature     * 100.0) / 100.0) +
        ";Pressure:"        + String(round(pressure        * 100.0) / 100.0) +
        ";Humidity:"        + String(round(humidity        * 100.0) / 100.0) +
        ";Gas_Resistance:"  + String(round(gas_resistance  * 100.0) / 100.0) +
        ";Battery_Voltage:" + String(round(battery_voltage * 100.0) / 100.0) +
        ";RSSI:";            // ← leer, Gateway füllt es

    digitalWrite(LED_ORANGE, LOW);
    bool ok = Lora_router.transmit(sdcard.cfg.SenderID, payload);
    digitalWrite(LED_ORANGE, HIGH);

    if (ok) Serial.println("[ROUTER] ✅ Eigenes Paket gesendet.");
    else    Serial.println("[ROUTER] ❌ Senden fehlgeschlagen.");

    setCpuLow();
}




// ============================================================
//  setup()
// ============================================================
void setup()
{
    // ── CPU direkt auf Maximum für Init ──────────────────────
    setCpuFrequencyMhz(240);

    Serial.begin(115200);
    delay(500);

    Serial.println("╔══════════════════════════════╗");
    Serial.println("║   CAI_MINI LoRa Router       ║");
    Serial.println("╚══════════════════════════════╝");
    Serial.print("Firmware Version: ");
    Serial.println(FW_VERSION);

    pinMode(LED_BLUE,   OUTPUT); digitalWrite(LED_BLUE,   LOW);
    pinMode(LED_ORANGE, OUTPUT); digitalWrite(LED_ORANGE, HIGH);

    // ── WiFi deaktivieren (nicht benötigt) ───────────────────
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

    // ── LoRa ──────────────────────────────────────────────────
    if (!Lora_router.begin(sdcard.cfg.DeviceID)) {
        Serial.println("[LORA] KRITISCH: Initialisierung fehlgeschlagen!");
        while (1) { delay(1000); }
    }

    // ── Erstes eigenes Paket senden ───────────────────────────
    sendOwnPacket();

    Serial.println("[SETUP] ✅ Bereit – warte auf LoRa Pakete...");

    // ── Nach Init auf niedrige Frequenz ──────────────────────
    setCpuLow();
}

// ============================================================
//  loop()
// ============================================================
void loop()
{
    // ── Paket empfangen? ──────────────────────────────────────
    if (Lora_router.packetReceived())
    {
        forwardPacket(); // setzt CPU hoch/runter intern
    }

    // ── Zeit für eigenes Paket? ───────────────────────────────
    if (millis() - last_own_send >= (unsigned long)SEND_INTERVAL_SEC * 1000UL)
    {
        
        sendOwnPacket(); // setzt CPU hoch/runter intern
    }

    // ── Kurz yielden damit Interrupts verarbeitet werden ──────
    delay(10);
}