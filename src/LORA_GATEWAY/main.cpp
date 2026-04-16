// ============================================================
//  main.cpp  —  CAI_MINI LoRa Gateway
//  SX1262 via RadioLib | ThingsBoard MQTT
//  Datum: 2026-04-16
// ============================================================

#include "config_LORA_GATEWAY.h"
#include <Arduino.h>
#include "LORA.h"
#include "SensorPacket.h"
#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>

// ============================================================
//  Netzwerk & ThingsBoard Konfiguration
// ============================================================
const char* WIFI_SSID       = "TP-Link_MF";
const char* WIFI_PASSWORD   = "MFFunkturm";
const char* TB_SERVER       = "iot.mfsquare.ch";
const uint16_t TB_PORT      = 1884;

char accessToken[64] = {0};

// ============================================================
//  MQTT / ThingsBoard Objekte
// ============================================================
constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;

WiFiClient            wifiClient;
Arduino_MQTT_Client   mqttClient(wifiClient);
ThingsBoardSized<32, 10> tb(mqttClient, MAX_MESSAGE_SIZE);

// ============================================================
//  SPI + LoRa Objekte
// ============================================================
SPIClass spi_lora(FSPI);

LORA Lora_gateway(
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
//  WiFi initialisieren
// ============================================================
void InitWiFi()
{
    Serial.println("[WIFI] Verbinde mit: " + String(WIFI_SSID));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 0, nullptr, true);

    unsigned long start   = millis();
    const uint32_t TIMEOUT = 15000UL;

    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < TIMEOUT)
    {
        delay(500);
        Serial.print(".");
        digitalWrite(LED_ORANGE, !digitalRead(LED_ORANGE)); // blinken
    }

    digitalWrite(LED_ORANGE, LOW);

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\n[WIFI] ✅ Verbunden!");
        Serial.println("[WIFI] IP: " + WiFi.localIP().toString());
    }
    else
    {
        Serial.println("\n[WIFI] ❌ Verbindung fehlgeschlagen (Timeout)!");
    }
}

// ============================================================
//  ThingsBoard verbinden
// ============================================================
bool InitTB()
{
    Serial.println("[TB] Verbinde mit: " + String(TB_SERVER) +
                   " | Token: "         + String(accessToken));

    if (!tb.connect(TB_SERVER, accessToken, TB_PORT))
    {
        Serial.println("[TB] ❌ Verbindung fehlgeschlagen!");
        return false;
    }

    Serial.println("[TB] ✅ Verbunden!");
    return true;
}

// ============================================================
//  WiFi neu verbinden falls getrennt
// ============================================================
void ensureWiFi()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WIFI] Verbindung verloren – reconnect...");
        WiFi.disconnect();
        delay(500);
        InitWiFi();
    }
}

// ============================================================
//  Telemetrie an ThingsBoard senden
// ============================================================
void sendToThingsBoard(const SensorPacket& p)
{
    // Token aus dem Paket als Access-Token nutzen
    strlcpy(accessToken, p.token, sizeof(accessToken));

    ensureWiFi();

    if (!InitTB()) return;

    // ── Telemetrie ────────────────────────────────────────────
    tb.sendTelemetryData("Temperature",    round(p.temperature    * 100.0) / 100.0);
    tb.sendTelemetryData("Pressure",       round(p.pressure       * 100.0) / 100.0);
    tb.sendTelemetryData("Humidity",       round(p.humidity       * 100.0) / 100.0);
    tb.sendTelemetryData("Gas_Resistance", round(p.gasResistance  * 100.0) / 100.0);
    tb.sendTelemetryData("Battery_Voltage",round(p.batteryVoltage * 100.0) / 100.0);

    // ── Attribute ─────────────────────────────────────────────
    tb.sendAttributeData("rssi", round(Lora_gateway.getLastRSSI() * 10.0) / 10.0);
    tb.sendAttributeData("snr",  round(Lora_gateway.getLastSNR()  * 10.0) / 10.0);

    Serial.println("[TB] ✅ Daten gesendet.");
    tb.disconnect();
}

// ============================================================
//  Paket auswerten + ausgeben
// ============================================================
void handlePacket()
{
    String sender  = Lora_gateway.readSender();
    String payload = Lora_gateway.readData();

    Serial.println("[LORA] ── Neues Paket ─────────────────────────");
    Serial.println("        Sender          : " + sender);
    Serial.println("        Payload         : " + payload);

    SensorPacket packed = parseSensorPacket(sender, payload);

    Serial.println("        Token           : " + String(packed.token));
    Serial.println("        Temperatur      : " + String(packed.temperature,    2) + " °C");
    Serial.println("        Luftdruck       : " + String(packed.pressure,       2) + " hPa");
    Serial.println("        Luftfeuchtigkeit: " + String(packed.humidity,       2) + " %");
    Serial.println("        Gaswiderstand   : " + String(packed.gasResistance,  2) + " kOhm");
    Serial.println("        Batterie        : " + String(packed.batteryVoltage, 2) + " V");
    Serial.println("        RSSI            : " + String(Lora_gateway.getLastRSSI(), 1) + " dBm");
    Serial.println("        SNR             : " + String(Lora_gateway.getLastSNR(),  1) + " dB");
    Serial.println("[LORA] ──────────────────────────────────────────");

    sendToThingsBoard(packed);
}

// ============================================================
//  setup()
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("╔══════════════════════════════╗");
    Serial.println("║   CAI_MINI LoRa Gateway      ║");
    Serial.println("╚══════════════════════════════╝");

    // ── LEDs ─────────────────────────────────────────────────
    pinMode(LED_BLUE,   OUTPUT);
    pinMode(LED_ORANGE, OUTPUT);
    digitalWrite(LED_BLUE,   LOW);
    digitalWrite(LED_ORANGE, HIGH);

    // ── WiFi ─────────────────────────────────────────────────
    InitWiFi();

    // ── LoRa ─────────────────────────────────────────────────
    // KEIN pinMode(LORA_DIO1, INPUT) hier! 
    // RadioLib / LORA::begin() übernimmt den Pin komplett.
    if (!Lora_gateway.begin("GATEWAY01"))
    {
        Serial.println("[LORA] KRITISCH: Initialisierung fehlgeschlagen!");
        while (1) { delay(1000); }
    }

    Serial.println("[SETUP] ✅ Bereit – warte auf LoRa Pakete...");
}

// ============================================================
//  loop()
// ============================================================
void loop()
{
    // ── Interrupt-basiert: packetReceived() prüft s_packetFlag ──
    // KEIN digitalRead(LORA_DIO1) mehr nötig!
    if (Lora_gateway.packetReceived())
    {
        digitalWrite(LED_ORANGE, LOW);
        handlePacket();
        digitalWrite(LED_ORANGE, HIGH);
    }

    // ── TB loop() für Keep-Alive (optional, wenn Subscription genutzt) ──
    // tb.loop();
}
