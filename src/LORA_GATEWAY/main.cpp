/*
 * ============================================================
 *  CAI_MINI — LoRa Gateway
 * ============================================================
 *  Beschreibung : Empfängt LoRa-Pakete von Sensoren und Routern,
 *                 übersetzt die Kurzkeys und sendet die Werte
 *                 per MQTT an ThingsBoard
 *  Board        : Seeed XIAO ESP32-S3 / Heltec WSL V3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 *
 *  Architektur  : Dual-Core via FreeRTOS Queue
 *                 Core 0 → LoRa empfangen
 *                 Core 1 → WiFi / MQTT / ThingsBoard
 *
 *  Erweiterung  :
 *    Neuer Messwert   → TelemetryKeys.h  (Kurzkey + TB-Name)
 *    Berechneter Wert → DERIVED_VALUES   (Abschnitt unten)
 * ============================================================
 */

#include "config_LORA_GATEWAY.h"
#include "pins_LORA_GATEWAY.h"
#include <Arduino.h>
#include "LORA.h"
#include "TelemetryPayload.h"
#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "credentials.h"
#include "FirmwareUpdater.h"
#include "log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef LOG_TELNET
  #include <TelnetStream.h>
#endif


// ============================================================
//  Netzwerk & ThingsBoard Konfiguration
// ============================================================
const char* WIFI_SSID        = WLAN_SSID;
const char* WIFI_PASSWORD    = WLAN_PW;
const char* TB_SERVER        = "iot.mfsquare.ch";
const char* TB_TOKEN_GATEWAY = GATEWAY_TOKEN;
const uint16_t TB_PORT       = 1884;

// ============================================================
//  Akku-Kennlinie (Li-Ion)
// ============================================================
constexpr double BATT_V_EMPTY = 3.0;
constexpr double BATT_V_FULL  = 4.2;

static double batteryPercent(double voltage)
{
    double pct = (voltage - BATT_V_EMPTY) / (BATT_V_FULL - BATT_V_EMPTY) * 100.0;
    pct = constrain(pct, 0.0, 100.0);
    return round(pct * 100.0) / 100.0;
}

// ============================================================
//  Berechnete Werte
//
//  Werden aus den empfangenen Werten eines Pakets berechnet
//  und zusätzlich als Telemetrie gesendet. Liefert ein Gerät
//  den Wert selbst, wird er nicht berechnet.
//
//  Neuer Wert:
//    1) Funktion  bool derive_xxx(const TelemetryData& d, double& out)
//       → true + out setzen, wenn berechenbar
//    2) Zeile in DERIVED_VALUES[] eintragen
//
//  Eingangswerte über ThingsBoard-Namen abfragen:
//    const TelemetryField* f = tp_find(d, "Battery_Voltage");
// ============================================================
typedef bool (*DeriveFn)(const TelemetryData& d, double& out);

struct DerivedValue
{
    const char* tbKey;
    DeriveFn    fn;
};

static bool derive_batteryPercentage(const TelemetryData& d, double& out)
{
    const TelemetryField* bv = tp_find(d, "Battery_Voltage");
    if (!bv) return false;
    out = batteryPercent(bv->value);
    return true;
}

static const DerivedValue DERIVED_VALUES[] =
{
    //  ThingsBoard-Name        Berechnung
    { "Battery_Percentage",  derive_batteryPercentage },
};

// ============================================================
//  Queue-Element
//  Core 0 kopiert Rohpayload + Funkwerte hinein,
//  Core 1 parst und sendet an ThingsBoard
// ============================================================
struct LoraRxItem
{
    char  sender[32];
    char  payload[TP_RX_MAX + 1];
    float gatewayRSSI;      // LoRa-RSSI gemessen vom Gateway
    float gatewaySNR;       // LoRa-SNR  gemessen vom Gateway
};

// ============================================================
//  FreeRTOS Queue
//  Tiefe 8: bis zu 8 Pakete können warten
// ============================================================
static QueueHandle_t s_packetQueue = nullptr;

// Signal-Flag: Core 0 → Core 1 soll gateway_send() ausführen
static volatile bool s_doGatewaySend = false;

// ============================================================
//  MQTT / ThingsBoard Objekte
//  NUR von Core 1 verwendet → kein Mutex nötig
// ============================================================
constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;

WiFiClient               wifiClient;
Arduino_MQTT_Client      mqttClient(wifiClient);
ThingsBoardSized<32, 10> tb(mqttClient, MAX_MESSAGE_SIZE, MAX_MESSAGE_SIZE);   // Empfangs- + Sendepuffer

// ============================================================
//  SPI + LoRa Objekte
//  NUR von Core 0 verwendet → kein Mutex nötig
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
    7000    // ackTimeout  [ms]
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

        configTime(3600, 3600, "ch.pool.ntp.org");
        logln("[NTP] Synchronisiere Zeit...");

        struct tm timeinfo;
        unsigned long ntpStart = millis();
        while (!getLocalTime(&timeinfo) && millis() - ntpStart < 5000)
        {
            delay(200);
        }

        if (getLocalTime(&timeinfo))
            logln("[NTP] ✅ Zeit synchronisiert.");
        else
            logln("[NTP] ⚠️ Zeitsynchronisation fehlgeschlagen!");
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
//  Telemetrie senden — alle empfangenen Werte
// ============================================================
static void sendReceivedValues(const TelemetryData& d)
{
    logln("[TB] Telemetrie (" + String(d.count) + " Felder):");

    for (uint8_t i = 0; i < d.count; i++)
    {
        tb.sendTelemetryData(d.fields[i].key, d.fields[i].value);
        logln("        " + String(d.fields[i].key) + " = " + String(d.fields[i].value, 2));
    }
}

// ============================================================
//  Telemetrie senden — berechnete Werte
// ============================================================
static void sendDerivedValues(const TelemetryData& d)
{
    for (const DerivedValue& dv : DERIVED_VALUES)
    {
        if (tp_find(d, dv.tbKey)) continue;   // vom Gerät geliefert

        double value;
        if (dv.fn(d, value))
        {
            tb.sendTelemetryData(dv.tbKey, value);
            logln("        " + String(dv.tbKey) + " = " + String(value, 2) + " (berechnet)");
        }
    }
}

// ============================================================
//  Attribute senden — Funkqualität
//
//  RS/SN im Paket → Werte des ersten Routers
//  sonst          → Direktempfang, Werte des Gateways
// ============================================================
static void sendLinkQuality(const TelemetryData& d, const LoraRxItem& item)
{
    float rssi = d.hasRssi ? d.rssi : item.gatewayRSSI;
    float snr  = d.hasSnr  ? d.snr  : item.gatewaySNR;

    logln(String("[TB] RSSI ") + (d.hasRssi ? "vom Router:  " : "vom Gateway: ") +
          String(rssi, 1) + " dBm");
    logln(String("[TB] SNR  ") + (d.hasSnr  ? "vom Router:  " : "vom Gateway: ") +
          String(snr, 1) + " dB");

    tb.sendAttributeData("rssi", round(rssi * 10.0) / 10.0);
    tb.sendAttributeData("snr",  round(snr  * 10.0) / 10.0);
}

// ============================================================
//  Empfangenes Paket an ThingsBoard senden
//  Läuft auf Core 1
// ============================================================
void sendToThingsBoard(const LoraRxItem& item)
{
    static TelemetryData d;   // ~1 KB → statisch statt Stack (nur Core 1)

    if (!tp_parse(item.payload, d))
    {
        logln("[TB] ❌ Paket ungültig – verworfen.");
        return;
    }

    ensureWiFi();

    if (tb.connected())
    {
        tb.disconnect();
        delay(100);
    }

    if (!InitTB(TB_SERVER, d.token, TB_PORT)) return;

    sendReceivedValues(d);
    sendDerivedValues(d);
    sendLinkQuality(d, item);

    logln("[TB] ✅ Daten gesendet.");
    tb.loop();
    delay(1000);
    tb.disconnect();
}

// ============================================================
//  Akku-Spannung lesen (Heltec)
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
//  Läuft auf Core 1
// ============================================================
void gateway_send()
{
    logln("[GATEWAY] Sending data to ThingsBoard.");
    ensureWiFi();

    configTime(3600, 3600, "ch.pool.ntp.org");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        _ntp_time_anchor   = mktime(&timeinfo);
        _ntp_millis_anchor = millis();
        logln("[NTP] ✅ Zeit-Anker gesetzt.");
    } else {
        logln("[NTP] ⚠️ Zeitsynchronisation fehlgeschlagen!");
    }

    digitalWrite(LED_BOARD, ON);

    if (tb.connected())
    {
        tb.disconnect();
        delay(100);
    }

    InitTB(TB_SERVER, TB_TOKEN_GATEWAY, TB_PORT);

    delay(1000);

    logln("[GATEWAY] Sende Attribute: RSSI, SNR, Channel, BSSID, LocalIP, SSID, FW-Version");
    tb.sendAttributeData("rssi",      WiFi.RSSI());
    tb.sendAttributeData("channel",   WiFi.channel());
    tb.sendAttributeData("bssid",     WiFi.BSSIDstr().c_str());
    tb.sendAttributeData("localIp",   WiFi.localIP().toString().c_str());
    tb.sendAttributeData("ssid",      WiFi.SSID().c_str());
    tb.sendAttributeData("fwversion", FW_VERSION);

#ifdef HELTEC_WSL_V3
    float voltage = readBattVoltage_heltec(VBAT_PIN);
    tb.sendTelemetryData("Battery_Voltage",    round(voltage * 100.0) / 100.0);
    tb.sendTelemetryData("Battery_Percentage", batteryPercent(voltage));

    logln("[GATEWAY] Battery Voltage = " + String(voltage, 2) + " V");
#endif

#ifdef SEED_XIAO_ESP32S3
    float voltage = random(3500, 4201) / 1000.0f; // Simuliere Spannung zwischen 3.5V und 4.2V
    tb.sendTelemetryData("Battery_Voltage",    voltage);
    tb.sendTelemetryData("Battery_Percentage", batteryPercent(voltage));
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
//  Core 1 Task — WiFi / MQTT / ThingsBoard
//
//  Wartet auf Queue-Einträge von Core 0.
//  Verarbeitet Pakete und das gateway_send() Signal.
//  Core 0 (LoRa) läuft komplett unabhängig weiter.
// ============================================================
void mqttTask(void* pvParameters)
{
    logln("[MQTT-TASK] Core 1 gestartet.");

    static LoraRxItem item;

    for (;;)
    {
        // ── gateway_send() Signal von Core 0 ─────────────────
        if (s_doGatewaySend)
        {
            s_doGatewaySend = false;
            gateway_send();
        }

        // ── Queue: Paket vorhanden? (50ms warten) ────────────
        if (xQueueReceive(s_packetQueue, &item, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            logln("[MQTT-TASK] Paket aus Queue (" + String(item.sender) +
                  ") – sende an ThingsBoard.");
            digitalWrite(LED_BOARD, ON);
            sendToThingsBoard(item);
            digitalWrite(LED_BOARD, OFF);
        }
    }
}

// ============================================================
//  setup()
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(3000);

    _log_mutex = xSemaphoreCreateMutex();

    logln("╔══════════════════════════════╗");
    logln("║    LoRa Gateway              ║");
    logln("╚══════════════════════════════╝");
    logf("Firmware Version: ");
    logln(FW_VERSION);

    pinMode(LED_BOARD, OUTPUT);
    digitalWrite(LED_BOARD, ON);

#ifdef HELTEC_WSL_V3
    pinMode(VBAT_PIN, INPUT);
    pinMode(ADC_CTRL_PIN, OUTPUT);
    digitalWrite(ADC_CTRL_PIN, LOW);
#endif

    // ── WiFi zuerst (TelnetStream braucht WiFi) ──────────────
    InitWiFi();

#ifdef LOG_TELNET
    TelnetStream.begin();
    logln("[TELNET] TelnetStream gestartet.");
    logln("[TELNET] Warte auf Telnet-Verbindung... (5s)");

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

    // ── LoRa ─────────────────────────────────────────────────
    if (!Lora_gateway.begin("GATEWAY01"))
    {
        logln("[LORA] KRITISCH: Initialisierung fehlgeschlagen!");
        while (1) { delay(1000); }
    }

    // ── FreeRTOS Queue erstellen ──────────────────────────────
    s_packetQueue = xQueueCreate(8, sizeof(LoraRxItem));
    if (s_packetQueue == nullptr)
    {
        logln("[QUEUE] KRITISCH: Queue konnte nicht erstellt werden!");
        while (1) { delay(1000); }
    }
    logln("[QUEUE] ✅ Queue erstellt (Tiefe 8).");

    // ── Core 1 Task starten ───────────────────────────────────
    // Stack 8192: WiFi + MQTT + ThingsBoard brauchen viel Stack
    xTaskCreatePinnedToCore(
        mqttTask,       // Task-Funktion
        "mqttTask",     // Name (Debug)
        8192,           // Stack in Bytes
        nullptr,        // Parameter
        1,              // Priorität (1 = niedrig, genug für MQTT)
        nullptr,        // Task-Handle (nicht benötigt)
        1               // Core 1
    );
    logln("[TASK] ✅ mqttTask auf Core 1 gestartet.");

    // ── Erstes gateway_send() via Signal ─────────────────────
    s_doGatewaySend = true;

    logln("[SETUP] ✅ Bereit – warte auf LoRa Pakete...");
}

// ============================================================
//  loop() — läuft auf Core 0
//  Nur LoRa empfangen + Paket in Queue legen
//  Kein WiFi, kein MQTT, kein Blocking
// ============================================================
void loop()
{
    unsigned long now = millis();

    // ── Paket empfangen → Queue ───────────────────────────────
    if (Lora_gateway.packetReceived())
    {
        String sender  = Lora_gateway.readSender();
        String payload = Lora_gateway.readData();

        logln("[LORA] ── Neues Paket ─────────────────────────");
        logln("        Sender  : " + sender);
        logln("        Payload : " + payload);

        if (payload.length() > TP_RX_MAX)
        {
            logln("[LORA] ⚠️ Payload zu lang (" + String(payload.length()) + ") – verworfen!");
        }
        else
        {
            LoraRxItem item = {};
            strlcpy(item.sender,  sender.c_str(),  sizeof(item.sender));
            strlcpy(item.payload, payload.c_str(), sizeof(item.payload));

            // ── RSSI/SNR auf Core 0 kopieren ─────────────────
            // Core 1 greift nicht auf Lora_gateway zu
            item.gatewayRSSI = Lora_gateway.getLastRSSI();
            item.gatewaySNR  = Lora_gateway.getLastSNR();

            logln("        RSSI    : " + String(item.gatewayRSSI, 1) + " dBm");
            logln("        SNR     : " + String(item.gatewaySNR,  1) + " dB");
            logln("[LORA] ──────────────────────────────────────────");

            // ── In Queue legen (0ms warten: wenn voll → verwerfen)
            if (xQueueSend(s_packetQueue, &item, 0) != pdTRUE)
                logln("[QUEUE] ⚠️ Queue voll – Paket verworfen!");
            else
                logln("[QUEUE] ✅ Paket in Queue.");
        }
    }

    // ── gateway_send() Intervall → Signal an Core 1 ──────────
    static unsigned long last_gateway_send = 0;
    if (now - last_gateway_send > gateway_send_interval * 60UL * 1000UL)
    {
        logln("[GATEWAY] Send interval reached – Signal an Core 1.");
        s_doGatewaySend   = true;
        last_gateway_send = now;
    }

#ifdef LOG_TELNET
    static unsigned long last_telnet_ping = 0;
    if (now - last_telnet_ping > 5000)
    {
        TelnetStream.print("");
        last_telnet_ping = now;
    }
#endif
}