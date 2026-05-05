#include "wifi_functions.h"

// ============================================================
//  Wifi
// ============================================================

bool InitWiFi(char ssid[64],char password[64]) {
    logln();
    logf("Connecting to "); logln(ssid);

    WiFi.begin(ssid, password, 0, nullptr, true);

    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 5000;

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(500);
        logf(".");
    }
    logln("");
    if (WiFi.status() != WL_CONNECTED) {

        logln("❌ WiFi konnte nicht verbunden werden.");
        return false;
    }

    digitalWrite(LED_BUILTIN, HIGH);
    logln("\n✅ WiFi connected");
    logf("IP address: ");
    logln(WiFi.localIP());
    return true;
}

void disconnectWiFi(WiFiClient* wifiClient) {
    logln("Disconnecting WiFi...");
    wifiClient->stop();
    WiFi.disconnect(true);
    delay(50);
    WiFi.mode(WIFI_OFF);
    logln("✅ WiFi disconnected");
    
}