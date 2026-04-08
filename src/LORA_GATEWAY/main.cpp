
#include "config_LORA_GATEWAY.h"
#include <Arduino.h>
#include "LORA.h"
#include "SensorPacket.h"
#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include <IniFile.h> 


char ssid[64];
char password[64];
char thingsboardServer[64];
char accessToken[64];
uint16_t THINGSBOARD_PORT;




// ═════════════════════════════════════════════════════════════
//  wieder raus§

IniFile ini("/INIT.ini", FILE_READ, true); // INI-Datei-Objekt
SPIClass spi_sd(HSPI);    // SD-Karte  → HSPI Bus

// ═════════════════════════════════════════════════════════════


constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;   // Maximalgröße MQTT-Nachrichten


WiFiClient wifiClient;                         // WiFi Client für MQTT
Arduino_MQTT_Client mqttClient(wifiClient);   // MQTT Client
ThingsBoardSized<32, 10> tb(mqttClient, MAX_MESSAGE_SIZE); // ThingsBoard Client



// ------------------------------------------------------------
//  GATEWAY Instanz
//  - Kein Zielgeraet noetig (Gateway ist Endpunkt)
//  - targetName = "" da Gateway nichts weiterleitet
// ------------------------------------------------------------
//LORA Lora_gateway("GATEWAY01", LoraMode::GATEWAY, LORA_NSS, LORA_DIO1, LORA_RESET, LORA_BUSY, "", 3, 500, 2000);


SPIClass spi_lora(FSPI);

LORA Lora_gateway(
    "GATEWAY01",
    LoraMode::GATEWAY,
    LORA_NSS,
    LORA_DIO1,
    LORA_RESET,
    LORA_BUSY,
    spi_lora,                            // ← NEU: eigener SPI Bus
    LORA_SCK,                            // ← NEU: SCK Pin
    LORA_MISO,                           // ← NEU: MISO Pin
    LORA_MOSI,                           // ← NEU: MOSI Pin
    "",                                  // targetName (Gateway braucht keins)
    5,
    1000,
    2000
);


void InitSD() {
    spi_sd.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, spi_sd)) {           // ← spi_sd hinzugefuegt!
        Serial.println("Fehler: SD-Karte konnte nicht initialisiert werden!");
        while (1);
    }
    Serial.println("SD-Karte erfolgreich initialisiert!");
}


void InitWiFi() {
    Serial.println();
    Serial.print("Connecting to "); Serial.println(ssid);
    WiFi.begin(ssid, password, 0, nullptr, true);
    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 15000; // 15 Sekunden Timeout

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(500);
        Serial.print(".");
        digitalWrite(LED_ORANGE, !digitalRead(LED_ORANGE)); // blinkende LED während Verbindung
    }

    digitalWrite(LED_ORANGE, LOW); // LED ein bei Verbindung
    Serial.println("\n✅ WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}


void get_ini_values() {
    if (!ini.open()) {
        Serial.println("❌ INI-Datei konnte nicht geöffnet werden!");
        while (1);
    }

    char buffer[128];
    if (ini.getValue("WIFI", "SSID", buffer, sizeof(buffer))) strcpy(ssid, buffer);
    if (ini.getValue("WIFI", "PW", buffer, sizeof(buffer))) strcpy(password, buffer);
    if (ini.getValue("THINGSBOARD", "TB_ADRESS", buffer, sizeof(buffer))) strcpy(thingsboardServer, buffer);
    if (ini.getValue("THINGSBOARD", "TB_PORT", buffer, sizeof(buffer))) THINGSBOARD_PORT = atoi(buffer);


    // Debug-Ausgabe
    Serial.println("✅ INI-Werte gelesen:");
    Serial.print("SSID: "); Serial.println(ssid);
    Serial.print("Password: "); Serial.println(password);
    Serial.print("TB Server: "); Serial.println(thingsboardServer);
    Serial.print("TB Port: "); Serial.println(THINGSBOARD_PORT); 
    ini.close();
    SD.end(); 
}

void InitTB() {
  Serial.print("Connecting to: ");
  Serial.print(thingsboardServer);
  Serial.print(" with token ");
  Serial.println(accessToken);
  if (!tb.connect(thingsboardServer, accessToken,THINGSBOARD_PORT)) {
    Serial.println("Failed to connect to tb");
  }
}

void setup()
{
    Serial.begin(115200);
    delay(5000); // Warte 5 Sekunden fuer Serial Debugging

    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_BLUE, LOW);



    Serial.println("CAI_MINI LoRa Gateway gestartet.");
    InitSD();
    get_ini_values();
    InitWiFi();

    Lora_gateway.begin();
}




void loop()
{
    if (Lora_gateway.packetReceived())
    {
        digitalWrite(LED_BLUE, HIGH);
        // ── 1. Rohdaten vom Gateway holen ────────────────────
        String sender  = Lora_gateway.readSender();
        String payload = Lora_gateway.readData();
 
        // ── 2. Parsen ────────────────────────────────────────
        SensorPacket packed = parseSensorPacket(sender, payload);
 

        Serial.println("  Sender          : " + String(packed.sender));
        Serial.println("  Token           : " + String(packed.token));
        Serial.println("  Temperatur      : " + String(packed.temperature,    2) + " °C");
        Serial.println("  Luftdruck       : " + String(packed.pressure,       2) + " hPa");
        Serial.println("  Luftfeuchtigkeit: " + String(packed.humidity,       2) + " %");
        Serial.println("  Gaswiderstand   : " + String(packed.gasResistance,  2) + " kOhm");
        Serial.println("  Batterie        : " + String(packed.batteryVoltage, 2) + " V");


        strlcpy(accessToken, packed.token, sizeof(accessToken));

        
        InitTB();

        tb.sendTelemetryData("Temperature", round(packed.temperature * 100.0) / 100.0);
        tb.sendTelemetryData("Pressure", round(packed.pressure * 100.0) / 100.0);
        tb.sendTelemetryData("Humidity", round(packed.humidity * 100.0) / 100.0);
        tb.sendTelemetryData("Gas_Resistance", round(packed.gasResistance * 100.0) / 100.0);
        tb.sendTelemetryData("Battery_Voltage", round(packed.batteryVoltage * 100.0) / 100.0);

        tb.disconnect();

        digitalWrite(LED_BLUE, LOW);

    }
}

