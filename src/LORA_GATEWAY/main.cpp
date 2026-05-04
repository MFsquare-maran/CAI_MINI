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
#include <TelnetStream.h>   

//TODO

/*
// In setup(), nach InitWiFi():
TelnetStream.begin();       // 2. Telnet starten

// Überall wo du Serial.println(...) schreibst:
#define LOG(x) { Serial.println(x); TelnetStream.println(x); }

LOG("SENSORDATEN");

*/


// ============================================================
//  Netzwerk & ThingsBoard Konfiguration
// ============================================================
const char* WIFI_SSID           = WLAN_SSID;
const char* WIFI_PASSWORD       = WLAN_PW;
const char* TB_SERVER           = "iot.mfsquare.ch";
const char* TB_TOKEN_GATEWAY    = GATEWAY_TOKEN;
const uint16_t TB_PORT      = 1884;

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

//============================================================
// Firmware Updater
//============================================================

FirmwareUpdater updater; // Firmware-Update-Objekt

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
        digitalWrite(LED_BOARD, !digitalRead(LED_BOARD)); // blinken
    }

    digitalWrite(LED_BOARD, OFF);

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
bool InitTB(const char* tb_server , const char* access_token, uint16_t tb_port)
{
    Serial.println("[TB] Verbinde mit: " + String(tb_server) +
                   " | Token: "         + String(access_token));

    if (!tb.connect(tb_server, access_token, tb_port))
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
    strlcpy(accessToken, p.token, sizeof(accessToken));
    ensureWiFi();
    if (!InitTB(TB_SERVER, accessToken, TB_PORT)) return;

    tb.sendTelemetryData("Temperature",    round(p.temperature    * 100.0) / 100.0);
    tb.sendTelemetryData("Pressure",       round(p.pressure       * 100.0) / 100.0);
    tb.sendTelemetryData("Humidity",       round(p.humidity       * 100.0) / 100.0);
    tb.sendTelemetryData("Gas_Resistance", round(p.gasResistance  * 100.0) / 100.0);
    tb.sendTelemetryData("Battery_Voltage",round(p.batteryVoltage * 100.0) / 100.0);

    // ── RSSI: Gateway-RSSI anhängen falls noch leer ───────────
    float rssi_to_send;
    if (p.rssi == -1.0f) {
        // kein RSSI im Paket → Gateway hängt seinen eigenen an
        rssi_to_send = Lora_gateway.getLastRSSI();
        Serial.println("[TB] RSSI vom Gateway: " + String(rssi_to_send, 1) + " dBm");
    } else {
        // RSSI bereits vom Router gesetzt → weiterverwenden
        rssi_to_send = p.rssi;
        Serial.println("[TB] RSSI vom Router:  " + String(rssi_to_send, 1) + " dBm");
    }
    tb.sendAttributeData("rssi", round(rssi_to_send * 10.0) / 10.0);

    tb.sendAttributeData("snr", round(Lora_gateway.getLastSNR() * 10.0) / 10.0);

    Serial.println("[TB] ✅ Daten gesendet.");
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
//  Akku-Spannung lesen (mit Kalibrierungstabelle für HELTEC-Boards)
// ============================================================

float readBattVoltage_heltec(uint8_t adc_pin) {
  uint32_t raw = 0;
  for (int i = 0; i < 20; i++) {
    raw += analogReadMilliVolts(adc_pin)*(4.095f/3.95f);
  }
  float avg_mv = raw / 20.0;
  // Spannungsteiler zurückrechnen: 100k / (100k + 390k)
  return avg_mv * (490.0 / 100.0) / 1000.0; // in Volt
}

// ============================================================
//  gateway send
// ============================================================
void gateway_send()
{
        Serial.println("[GATEWAY] Sending data to ThingsBoard.");
        ensureWiFi();

        digitalWrite(LED_BOARD, ON);

        InitTB(TB_SERVER, TB_TOKEN_GATEWAY, TB_PORT);


        tb.sendAttributeData("rssi", WiFi.RSSI());
        tb.sendAttributeData("channel", WiFi.channel());
        tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
        tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
        tb.sendAttributeData("ssid", WiFi.SSID().c_str());
        tb.sendAttributeData("fwversion", FW_VERSION);

        #ifdef HELTEC_WSL_V3
            
            float voltage = readBattVoltage_heltec(VBAT_PIN);
        
            tb.sendTelemetryData("Battery_Voltage", ((voltage* 100.0) / 100.0));
            Serial.print("Batery Voltage = ");
            Serial.print((voltage * 100.0) / 100.0);
            Serial.println(" V");

        #endif

        #ifdef SEED_XIAO_ESP32S3

            tb.sendTelemetryData("Battery_Voltage", random(3500, 4201) / 1000.0 );

        #endif

        Serial.println("[TB] ✅ Daten gesendet.");

        tb.loop(); // Keep-Alive für MQTT-Verbindung
        delay(1000);
        tb.disconnect();

        digitalWrite(LED_BOARD, OFF);

        Serial.println("[GATEWAY] Check for Updates...");

        updater.checkAndUpdate(TB_SERVER,TB_TOKEN_GATEWAY,FW_VERSION,0);


}



// ============================================================
//  setup()
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(3000);

    Serial.println("╔══════════════════════════════╗");
    Serial.println("║    LoRa Gateway              ║");
    Serial.println("╚══════════════════════════════╝");
    Serial.print("Firmware Version: ");
    Serial.println(FW_VERSION);

    // ── LEDs ─────────────────────────────────────────────────
    
    pinMode(LED_BOARD, OUTPUT);
    digitalWrite(LED_BOARD, ON);

    #ifdef HELTEC_WSL_V3

        pinMode(VBAT_PIN, INPUT); //HELTEC-Boards haben VBAT intern mit ADC verbunden, daher INPUT
        pinMode(ADC_CTRL_PIN, OUTPUT); //HELTEC-Boards haben ADC_CTRL intern mit VBAT verbunden, daher OUTPUT und HIGH für Messung
        digitalWrite(ADC_CTRL_PIN, LOW); // LOW für normale Messung, HIGH für Kalibrierung (je nach Board nötig)

    #endif
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

    gateway_send();

    Serial.println("[SETUP] ✅ Bereit – warte auf LoRa Pakete...");

}

// ============================================================
//  loop()
// ============================================================
void loop()
{
    unsigned long now = millis();
    // ── Interrupt-basiert: packetReceived() prüft s_packetFlag ──
    // KEIN digitalRead(LORA_DIO1) mehr nötig!
    if (Lora_gateway.packetReceived())
    {
        digitalWrite(LED_BOARD, ON);
        handlePacket();
        digitalWrite(LED_BOARD, OFF);
    }

    if(now - last_gateway_send > gateway_send_interval * 60UL * 1000UL)
    {
        Serial.println("[GATEWAY] Send interval reached.");
        Serial.println("[GATEWAY] Sending data to ThingsBoard.");

        gateway_send();

        last_gateway_send =  now;

    }

 
}
