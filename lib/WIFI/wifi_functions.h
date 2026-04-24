#include <Arduino.h>
#include <WiFi.h>

bool InitWiFi(char ssid[64],char password[64]);
void disconnectWiFi(WiFiClient* wifiClient);
