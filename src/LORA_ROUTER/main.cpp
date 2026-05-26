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
#include "wifi_functions.h"
#include "FirmwareUpdater.h"
#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "log.h"

// ============================================================
//  Objekte
// ============================================================
SPIClass spi_lora(FSPI);
SPIClass spi_sd(HSPI);
FirmwareUpdater updater;

// ============================================================
//  Konstanten
// ============================================================
constexpr uint32_t MAX_MESSAGE_SIZE    = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD   = 115200U;


WiFiClient          wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoardSized<32, 10> tb(mqttClient, MAX_MESSAGE_SIZE);



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
    7000    // ackTimeout  [ms]
);

SDCard        sdcard;
Battery       battery;
BME680_Sensor bme;

// ============================================================
//  Zeitstempel für eigenes Paket
// ============================================================
unsigned long last_own_send = 0;
unsigned long sending_period = 1; // 1min.


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
    logln("[PM] CPU → 10 MHz (Idle)");
}

void setCpuHigh()
{
    setCpuFrequencyMhz(240);
    logln("[PM] CPU → 240 MHz (Aktiv)");
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

    logln("[ROUTER] Paket empfangen von: " + sender);
    logln("[ROUTER] RSSI vom Sensor:      " + String(rssi, 1) + " dBm");

    // ── RSSI anhängen (leeres Feld ersetzen oder hinzufügen) ──
    if (data.endsWith("RSSI:")) {
        // Sensor hat leeres RSSI-Feld → befüllen
        data += String(round(rssi * 10.0) / 10.0);
    } else if (data.indexOf("RSSI:") == -1) {
        // kein RSSI-Feld vorhanden → anhängen
        data += ";RSSI:" + String(round(rssi * 10.0) / 10.0);
    }
    // sonst: RSSI bereits befüllt → nicht überschreiben

    logln("[ROUTER] Weiterleiten: " + data);

    digitalWrite(LED_BLUE, HIGH);
    bool ok = Lora_router.transmit(sdcard.cfg.SenderID, data);
    digitalWrite(LED_BLUE, LOW);

    if (ok) logln("[ROUTER] ✅ Paket weitergeleitet.");
    else    logln("[ROUTER] ❌ Weiterleiten fehlgeschlagen.");

    setCpuLow();
}

// ============================================================
//  Eigenes Paket senden
// ============================================================
void sendOwnPacket()
{
    setCpuHigh();
    logln("[ROUTER] Sende eigenes Paket...");

    if (!bme.readSensor()) {
        logln("[BME680] FEHLER beim Lesen!");
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

    if (ok) logln("[ROUTER] ✅ Eigenes Paket gesendet.");
    else    logln("[ROUTER] ❌ Senden fehlgeschlagen.");

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
    delay(3000);

    _log_mutex = xSemaphoreCreateMutex();

    logln("╔══════════════════════════════╗");
    logln("║   CAI_MINI LoRa Router       ║");
    logln("╚══════════════════════════════╝");
    logf("Firmware Version: ");
    logln(FW_VERSION);

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
        logln("[LORA] KRITISCH: Initialisierung fehlgeschlagen!");
        while (1) { delay(1000); }
    }



    logln("[SETUP] ✅ Bereit ");

        
    sending_period = sdcard.cfg.sending_period;

    // ── Erstes Senden sofort beim Start ───────────────────────
    last_own_send = millis() + sending_period ;

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
    if (millis() - last_own_send >= sending_period)
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
        
            sendOwnPacket(); 

            
        }else{
            sendOwnPacket(); 
        }

        last_own_send = millis();

    }

    // ── Kurz yielden damit Interrupts verarbeitet werden ──────
    delay(10);
}