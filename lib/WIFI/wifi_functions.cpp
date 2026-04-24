#include "wifi_functions.h"

// ============================================================
//  Wifi
// ============================================================

bool InitWiFi(char ssid[64],char password[64]) {
    Serial.println();
    Serial.print("Connecting to "); Serial.println(ssid);

    WiFi.begin(ssid, password, 0, nullptr, true);

    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 15000;

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    if (WiFi.status() != WL_CONNECTED) {

        Serial.println("❌ WiFi konnte nicht verbunden werden.");
        return false;
    }

    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("\n✅ WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
}

void disconnectWiFi(WiFiClient* wifiClient) {
    Serial.println("Disconnecting WiFi...");
    wifiClient->stop();
    WiFi.disconnect(true);
    delay(50);
    WiFi.mode(WIFI_OFF);
    Serial.println("✅ WiFi disconnected");
    
}