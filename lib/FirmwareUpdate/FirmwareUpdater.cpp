#include "FirmwareUpdater.h"
#include <Update.h>

FirmwareUpdater::FirmwareUpdater(const char* server, const char* token, const String& currentVersion) 
    : _server(server), _deviceToken(token), _currentVersion(currentVersion) {
    
    _wifiClient.setInsecure(); // SSL-Zertifikatsvalidierung deaktivieren
    _http = new HttpClient(_wifiClient, _server, 443);
}

void FirmwareUpdater::setCurrentVersion(const String& version) {
    _currentVersion = version;
}

String FirmwareUpdater::getCurrentVersion() const {
    return _currentVersion;
}

void FirmwareUpdater::cleanupOldFirmware() {
    Serial.println("🔍 Suche nach alten .bin Dateien auf SD-Karte...");
    
    File root = SD.open("/");
    if (!root) {
        Serial.println("⚠️ Konnte SD-Karte Root nicht öffnen.");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        String fileName = String(file.name());
        
        // Prüfe ob es eine .bin Datei ist
        if (fileName.endsWith(".bin") || fileName.endsWith(".BIN")) {
            Serial.print("🗑️ Lösche alte Firmware: ");
            Serial.println(fileName);
            
            file.close();
            if (SD.remove("/" + fileName)) {
                Serial.println("   ✅ Gelöscht.");
            } else {
                Serial.println("   ⚠️ Fehler beim Löschen.");
            }
            
            // Nach dem Löschen neu öffnen
            file = root.openNextFile();
        } else {
            file = root.openNextFile();
        }
    }
    root.close();
    Serial.println("✅ Cleanup abgeschlossen.");
}

bool FirmwareUpdater::checkAndUpdate() {
    Serial.println("🔍 Checking for firmware updates...");
    
    // Erweiterte Abfrage mit allen benötigten Attributen
    String path = "/api/v1/" + String(_deviceToken) + "/attributes?sharedKeys=fw_version,fw_title,fw_tag";
    
    _http->get(path.c_str());
    
    int statusCode = _http->responseStatusCode();
    String response = _http->responseBody();
    
    Serial.print("Status code: ");
    Serial.println(statusCode);
    Serial.print("Response: ");
    Serial.println(response);
    
    if (statusCode == 200) {
        // Prüfen ob Response leer ist
        response.trim();
        if (response.length() <= 2 || response == "{}" || response == "[]") {
            Serial.println("ℹ️ Keine Firmware-Informationen auf Server verfügbar.");
            Serial.println("✅ Fahre mit aktueller Firmware fort.");
            return false;
        }
        
        FirmwareInfo fwInfo = extractFirmwareInfo(response);
        
        // Prüfen ob fw_version extrahiert werden konnte
        if (fwInfo.version.length() == 0) {
            Serial.println("⚠️ Keine Firmware-Version auf Server gefunden.");
            Serial.println("✅ Fahre mit aktueller Firmware fort.");
            return false;
        }
        
        // Version vergleichen
        if (fwInfo.version != _currentVersion) {
            Serial.println("🆕 New firmware version available: " + fwInfo.version);
            Serial.println("   Current version: " + _currentVersion);
            Serial.println("   Title: " + fwInfo.title);
            Serial.println("   Tag: " + fwInfo.tag);
            
            // Alte .bin Dateien vor dem Download löschen
            cleanupOldFirmware();
            
            // Download-URL mit Query-Parametern
            String downloadPath = "/api/v1/" + String(_deviceToken) + 
                                 "/firmware?title=" + fwInfo.title + 
                                 "&version=" + fwInfo.version;
            
            // Firmware auf SD-Karte herunterladen
            if (downloadFirmwareToSD(downloadPath, fwInfo.version)) {
                // Von SD-Karte updaten (mit versioniertem Dateinamen)
                String versionedFile = "/firmware_" + fwInfo.version + ".bin";
                return updateFromSD(versionedFile);
            }
        } else {
            Serial.println("✅ Firmware is up to date (Version " + _currentVersion + ")");
        }
    } else {
        Serial.println("❌ Failed to check firmware version (HTTP " + String(statusCode) + ")");
        Serial.println("✅ Fahre mit aktueller Firmware fort.");
    }
    
    return false;
}

FirmwareInfo FirmwareUpdater::extractFirmwareInfo(const String& payload) {
    FirmwareInfo info;
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        Serial.print("⚠️ JSON parsing failed: ");
        Serial.println(error.c_str());
        return info;
    }
    
    if (doc.containsKey("shared")) {
        JsonObject shared = doc["shared"];
        
        if (shared.containsKey("fw_version")) {
            info.version = shared["fw_version"].as<String>();
        }
        if (shared.containsKey("fw_title")) {
            info.title = shared["fw_title"].as<String>();
        }
        if (shared.containsKey("fw_tag")) {
            info.tag = shared["fw_tag"].as<String>();
        }
    }
    
    return info;
}

bool FirmwareUpdater::downloadFirmwareToSD(const String& downloadPath, const String& version) {
    Serial.println("📥 Downloading firmware to SD card...");
    Serial.println("    URL: https://" + String(_server) + downloadPath);
    
    _http->get(downloadPath.c_str());
    
    int statusCode = _http->responseStatusCode();
    int contentLength = _http->contentLength();
    
    Serial.print("HTTP Status: ");
    Serial.println(statusCode);
    Serial.print("Content Length: ");
    Serial.println(contentLength);
    
    if (statusCode != 200 || contentLength <= 0) {
        Serial.println("❌ Failed to download firmware.");
        return false;
    }
    
    // Zuerst als firmware.bin speichern
    File file = SD.open(_firmwareFile, FILE_WRITE);
    if (!file) {
        Serial.println("❌ Failed to open firmware file on SD card!");
        return false;
    }
    
    Serial.println("💾 Writing to SD card as firmware.bin...");
    Serial.print("Progress: ");
    
    uint8_t buff[512];
    int bytesWritten = 0;
    int lastPercent = 0;
    
    while (_http->available() || _http->connected()) {
        size_t size = _http->available();
        
        if (size) {
            int c = _http->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
            file.write(buff, c);
            bytesWritten += c;
            
            // Fortschrittsanzeige
            int percent = (bytesWritten * 100) / contentLength;
            if (percent != lastPercent && percent % 10 == 0) {
                Serial.print(percent);
                Serial.print("%...");
                lastPercent = percent;
            }
        }
        delay(1);
    }
    
    file.close();
    Serial.println("\n✅ Firmware successfully downloaded!");
    Serial.print("File size: ");
    Serial.print(bytesWritten);
    Serial.println(" bytes");
    
    if (bytesWritten != contentLength) {
        Serial.println("❌ Download incomplete!");
        return false;
    }
    
    // Jetzt umbenennen zu firmware_VERSION.bin
    String versionedFileName = "/firmware_" + version + ".bin";
    Serial.print("🔄 Rename to: ");
    Serial.println(versionedFileName);
    
    if (SD.rename(_firmwareFile, versionedFileName.c_str())) {
        Serial.println("✅ Firmware renamed successfully!");
        Serial.print("💾 Firmware auf SD-Karte: ");
        Serial.println(versionedFileName);
        return true;
    } else {
        Serial.println("⚠️ Warning: Could not rename file, but continuing with firmware.bin");
        return true; // Trotzdem weitermachen
    }
}

bool FirmwareUpdater::updateFromSD(const String& fileName) {
    Serial.println("🔄 Starting OTA update from SD card...");
    Serial.print("Using file: ");
    Serial.println(fileName);
    
    File file = SD.open(fileName.c_str(), FILE_READ);
    if (!file) {
        Serial.println("❌ Failed to open firmware file from SD card!");
        return false;
    }
    
    size_t fileSize = file.size();
    Serial.print("Firmware size: ");
    Serial.print(fileSize);
    Serial.println(" bytes");
    
    if (!Update.begin(fileSize)) {
        Serial.println("❌ Not enough space for OTA update!");
        file.close();
        return false;
    }
    
    Serial.print("Progress: ");
    uint8_t buff[512];
    size_t bytesWritten = 0;
    int lastPercent = 0;
    
    while (file.available()) {
        size_t len = file.read(buff, sizeof(buff));
        Update.write(buff, len);
        bytesWritten += len;
        
        // Fortschrittsanzeige
        int percent = (bytesWritten * 100) / fileSize;
        if (percent != lastPercent && percent % 10 == 0) {
            Serial.print(percent);
            Serial.print("%...");
            lastPercent = percent;
        }
    }
    
    file.close();
    Serial.println();
    
    if (Update.end()) {
        if (Update.isFinished()) {
            Serial.println("✅ OTA update successful!");
            Serial.print("💾 Firmware verbleibt auf SD-Karte: ");
            Serial.println(fileName);
            
            Serial.println("🔄 Rebooting in 3 seconds...");
            delay(3000);
            ESP.restart();
            return true;
        } else {
            Serial.println("❌ OTA update not finished!");
        }
    } else {
        Serial.println("❌ OTA update failed!");
        Serial.println(Update.errorString());
    }
    
    return false;
}
