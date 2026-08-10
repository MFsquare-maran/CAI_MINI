#include <Arduino.h>
#include <WiFi.h>
#include "log.h"



#define WIFI_CONNECT_TIMEOUT_MS 15000

bool InitWiFi(char ssid[64],char password[64]);
void disconnectWiFi(WiFiClient* wifiClient);
