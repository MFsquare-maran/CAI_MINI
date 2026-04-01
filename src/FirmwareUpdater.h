#ifndef FIRMWARE_UPDATER_H
#define FIRMWARE_UPDATER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <SD.h>

struct FirmwareInfo {
    String version;
    String title;
    String tag;
};

class FirmwareUpdater {
public:
    FirmwareUpdater(const char* server, const char* token, const String& currentVersion);
    
    bool checkAndUpdate();
    void setCurrentVersion(const String& version);
    String getCurrentVersion() const;
    
private:
    const char* _server;
    const char* _deviceToken;
    String _currentVersion;
    const char* _firmwareFile = "/firmware.bin";
    
    WiFiClientSecure _wifiClient;
    HttpClient* _http;
    
    FirmwareInfo extractFirmwareInfo(const String& payload);
    bool downloadFirmwareToSD(const String& downloadPath, const String& version);
    bool updateFromSD(const String& fileName);
    void cleanupOldFirmware();
};

#endif
