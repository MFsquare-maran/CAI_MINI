/*
 * ============================================================
 *  CAI_MINI — LoRa Gateway
 * ============================================================
 *  Beschreibung : Empfängt LoRa Pakete von Sensoren und
 *                 Router und sendert sie per mqtt an TB weiter
 *  Board        : Seeed XIAO ESP32-S3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 * ============================================================
 */

#include "config_LORA_GATEWAY.h"
#include <Arduino.h>
#include "LORA.h"
#include "SensorPacket.h"
#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "credentials.h"
#include "FirmwareUpdater.h"
#include "log.h"

// ============================================================
//  Netzwerk & ThingsBoard Konfiguration
// ============================================================
const char* WIFI_SSID        = WLAN_SSID;
const char* WIFI_PASSWORD    = WLAN_PW;
const char* TB_SERVER        = "iot.mfsquare.ch";
const char* TB_TOKEN_GATEWAY = GATEWAY_TOKEN;
const uint16_t TB_PORT       = 1884;

char accessToken[64] = {0};

unsigned long last_gateway_send = 0;

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
//  Firmware Updater
// ============================================================
FirmwareUpdater updater;

// ============================================================
//  WiFi initialisieren
// ============================================================
void InitWiFi()
{
    logln("[WIFI] Verbinde mit: " + String(WIFI_SSID));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 0, nullptr, true);

    unsigned long start    = millis();
    const uint32_t TIMEOUT = 15000UL;

    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < TIMEOUT)
    {
        delay(500);
        logf(".");
        digitalWrite(LED_BOARD, !digitalRead(LED_BOARD));
    }

    digitalWrite(LED_BOARD, OFF);

    if (WiFi.status() == WL_CONNECTED)
    {
        logln("\n[WIFI] ✅ Verbunden!");
        logln("[WIFI] IP: " + WiFi.localIP().toString());
    }
    else
    {
        logln("\n[WIFI] ❌ Verbindung fehlgeschlagen (Timeout)!");
    }
}

// ============================================================
//  ThingsBoard verbinden
// ============================================================
bool InitTB(const char* tb_server, const char* access_token, uint16_t tb_port)
{
    logln("[TB] Verbinde mit: " + String(tb_server) +
          " | Token: " + String(access_token));

    if (!tb.connect(tb_server, access_token, tb_port))
    {
        logln("[TB] ❌ Verbindung fehlgeschlagen!");
        return false;
    }

    logln("[TB] ✅ Verbunden!");
    return true;
}

// ============================================================
//  WiFi neu verbinden falls getrennt
// ============================================================
void ensureWiFi()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        logln("[WIFI] Verbindung verloren – reconnect...");
        WiFi.disconnect();
        delay(500);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 0, nullptr, true);

        unsigned long start    = millis();
        const uint32_t TIMEOUT = 15000UL;

        while (WiFi.status() != WL_CONNECTED &&
               millis() - start < TIMEOUT)
        {
            delay(500);
            logf(".");
            digitalWrite(LED_BOARD, !digitalRead(LED_BOARD));
        }
        digitalWrite(LED_BOARD, OFF);

        if (WiFi.status() == WL_CONNECTED)
            logln("\n[WIFI] ✅ Reconnect erfolgreich!");
        else
            logln("\n[WIFI] ❌ Reconnect fehlgeschlagen!");
    }
}

// ============================================================
//  Telemetrie an ThingsBoard senden
// ============================================================
void sendToThingsBoard(const SensorPacket& p)
{
    strlcpy(accessToken, p.token, sizeof(accessToken));
    ensureWiFi();

    // FIX 3: Vorherige Verbindung sauber trennen
    if (tb.connected())
    {
        tb.disconnect();
        delay(100);
    }

    if (!InitTB(TB_SERVER, accessToken, TB_PORT)) return;

    tb.sendTelemetryData("Temperature",    round(p.temperature    * 100.0) / 100.0);
    tb.sendTelemetryData("Pressure",       round(p.pressure       * 100.0) / 100.0);
    tb.sendTelemetryData("Humidity",       round(p.humidity       * 100.0) / 100.0);
    tb.sendTelemetryData("Gas_Resistance", round(p.gasResistance  * 100.0) / 100.0);
    tb.sendTelemetryData("Battery_Voltage",round(p.batteryVoltage * 100.0) / 100.0);

    // ── RSSI: Gateway-RSSI anhängen falls noch leer ──────────
    float rssi_to_send;
    if (p.rssi == -1.0f)
    {
        rssi_to_send = Lora_gateway.getLastRSSI();
        logln("[TB] RSSI vom Gateway: " + String(rssi_to_send, 1) + " dBm");
    }
    else
    {
        rssi_to_send = p.rssi;
        logln("[TB] RSSI vom Router:  " + String(rssi_to_send, 1) + " dBm");
    }
    tb.sendAttributeData("rssi", round(rssi_to_send * 10.0) / 10.0);
    tb.sendAttributeData("snr",  round(Lora_gateway.getLastSNR() * 10.0) / 10.0);

    logln("[TB] ✅ Daten gesendet.");
    tb.loop();
    delay(1000);
    tb.disconnect();
}

// ============================================================
//  Paket auswerten + ausgeben
// ============================================================
void handlePacket()
{
    String sender  = Lora_gateway.readSender();
    String payload = Lora_gateway.readData();

    logln("[LORA] ── Neues Paket ─────────────────────────");
    logln("        Sender          : " + sender);
    logln("        Payload         : " + payload);

    SensorPacket packed = parseSensorPacket(sender, payload);

    logln("        Token           : " + String(packed.token));
    logln("        Temperatur      : " + String(packed.temperature,    2) + " °C");
    logln("        Luftdruck       : " + String(packed.pressure,       2) + " hPa");
    logln("        Luftfeuchtigkeit: " + String(packed.humidity,       2) + " %");
    logln("        Gaswiderstand   : " + String(packed.gasResistance,  2) + " kOhm");
    logln("        Batterie        : " + String(packed.batteryVoltage, 2) + " V");
    logln("        RSSI            : " + String(Lora_gateway.getLastRSSI(), 1) + " dBm");
    logln("        SNR             : " + String(Lora_gateway.getLastSNR(),  1) + " dB");
    logln("[LORA] ──────────────────────────────────────────");

    sendToThingsBoard(packed);
}

// ============================================================
//  Akku-Spannung lesen (Kalibrierungstabelle für HELTEC-Boards)
// ============================================================
float readBattVoltage_heltec(uint8_t adc_pin)
{
    uint32_t raw = 0;
    for (int i = 0; i < 20; i++)
        raw += analogReadMilliVolts(adc_pin) * (4.095f / 3.95f);

    float avg_mv = raw / 20.0f;
    return avg_mv * (490.0f / 100.0f) / 1000.0f;
}

// ============================================================
//  Gateway-Daten senden + OTA prüfen
// ============================================================
void gateway_send()
{
    logln("[GATEWAY] Sending data to ThingsBoard.");
    ensureWiFi();

    digitalWrite(LED_BOARD, ON);

    // FIX 3: Vorherige Verbindung sauber trennen
    if (tb.connected())
    {
        tb.disconnect();
        delay(100);
    }

    InitTB(TB_SERVER, TB_TOKEN_GATEWAY, TB_PORT);

    tb.sendAttributeData("rssi",      WiFi.RSSI());
    tb.sendAttributeData("channel",   WiFi.channel());
    tb.sendAttributeData("bssid",     WiFi.BSSIDstr().c_str());
    tb.sendAttributeData("localIp",   WiFi.localIP().toString().c_str());
    tb.sendAttributeData("ssid",      WiFi.SSID().c_str());
    tb.sendAttributeData("fwversion", FW_VERSION);

#ifdef HELTEC_WSL_V3
    float voltage = readBattVoltage_heltec(VBAT_PIN);
    tb.sendTelemetryData("Battery_Voltage", (voltage * 100.0f) / 100.0f);
    logf("[GATEWAY] Battery Voltage = ");
    logf(voltage);
    logln(" V");
#endif

#ifdef SEED_XIAO_ESP32S3
    tb.sendTelemetryData("Battery_Voltage", random(3500, 4201) / 1000.0f);
#endif

    logln("[TB] ✅ Daten gesendet.");
    tb.loop();
    delay(1000);
    tb.disconnect();

    digitalWrite(LED_BOARD, OFF);

    logln("[GATEWAY] Check for Updates...");
    updater.checkAndUpdate(TB_SERVER, TB_TOKEN_GATEWAY, FW_VERSION, 0);
}

// ============================================================
//  setup()
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(3000);

    logln("╔══════════════════════════════╗");
    logln("║    LoRa Gateway              ║");
    logln("╚══════════════════════════════╝");
    logf("Firmware Version: ");
    logln(FW_VERSION);

    // ── LEDs ────────────────────────────────────────────────
    pinMode(LED_BOARD, OUTPUT);
    digitalWrite(LED_BOARD, ON);

#ifdef HELTEC_WSL_V3
    pinMode(VBAT_PIN, INPUT);
    pinMode(ADC_CTRL_PIN, OUTPUT);
    digitalWrite(ADC_CTRL_PIN, LOW);
#endif

    // ── FIX 1: WiFi ZUERST ──────────────────────────────────
    InitWiFi();

    // ── FIX 1: TelnetStream erst NACH WiFi starten ──────────
#ifdef LOG_TELNET
    TelnetStream.begin();
    logln("[TELNET] TelnetStream gestartet.");
    logln("[TELNET] Warte auf Telnet-Verbindung... (5s) – Taste druecken nach Connect!");

    unsigned long telnetWait = millis();
    while (TelnetStream.available() == 0 && millis() - telnetWait < 5000)
    {
        delay(100);
        digitalWrite(LED_BOARD, !digitalRead(LED_BOARD));
    }
    digitalWrite(LED_BOARD, OFF);

    if (TelnetStream.available() > 0)
        logln("[TELNET] ✅ Client verbunden!");
    else
        logln("[TELNET] Kein Client – fahre ohne Telnet fort.");
#endif

    // ── LoRa ────────────────────────────────────────────────
    if (!Lora_gateway.begin("GATEWAY01"))
    {
        logln("[LORA] KRITISCH: Initialisierung fehlgeschlagen!");
        while (1) { delay(1000); }
    }

    gateway_send();

    logln("[SETUP] ✅ Bereit – warte auf LoRa Pakete...");
}

// ============================================================
//  loop()
// ============================================================
void loop()
{
    unsigned long now = millis();

    if (Lora_gateway.packetReceived())
    {
        digitalWrite(LED_BOARD, ON);
        handlePacket();
        digitalWrite(LED_BOARD, OFF);
    }

    if (now - last_gateway_send > gateway_send_interval * 60UL * 1000UL)
    {
        logln("[GATEWAY] Send interval reached.");
        logln("[GATEWAY] Sending data to ThingsBoard.");
        gateway_send();
        last_gateway_send = now;
    }
}