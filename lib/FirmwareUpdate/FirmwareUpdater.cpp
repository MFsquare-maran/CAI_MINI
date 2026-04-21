#include "FirmwareUpdater.h"
#include <Update.h>

// =========================
// Constructor
// =========================
FirmwareUpdater::FirmwareUpdater() {
    _wifiClient.setInsecure();
    _http = nullptr;
}

// =========================
void FirmwareUpdater::closeConnection() {
    if (_http) {
        _http->stop();
        delete _http;
        _http = nullptr;
    }
}

// =========================
bool FirmwareUpdater::checkAndUpdate(const char* server,
                                     const char* token,
                                     const String& currentVersion,
                                     bool useSD) {

    _server = server;
    _deviceToken = token;
    _currentVersion = currentVersion;

    Serial.println("🔍 Checking firmware update...");

    closeConnection();
    _http = new HttpClient(_wifiClient, _server, 443);

    String path = "/api/v1/" + String(_deviceToken) +
                  "/attributes?sharedKeys=fw_version,fw_title,fw_tag";

    _http->get(path.c_str());

    int statusCode = _http->responseStatusCode();
    String response = _http->responseBody();

    Serial.print("HTTP: ");
    Serial.println(statusCode);

    if (statusCode != 200) {
        Serial.println("❌ HTTP error");
        closeConnection();
        return false;
    }

    response.trim();

    if (response.length() < 5 || response == "{}") {
        Serial.println("ℹ️ No firmware info");
        closeConnection();
        return false;
    }

    FirmwareInfo fw = extractFirmwareInfo(response);

    if (fw.version.length() == 0) {
        Serial.println("⚠️ No version found");
        closeConnection();
        return false;
    }

    if (fw.version == _currentVersion) {
        Serial.println("✅ Firmware up to date");
        closeConnection();
        return false;
    }

    Serial.println("🆕 New firmware: " + fw.version);

    String downloadPath = "/api/v1/" + String(_deviceToken) +
                          "/firmware?title=" + fw.title +
                          "&version=" + fw.version;

    if (useSD) {
        cleanupOldFirmware();

        if (!downloadFirmwareToSD(downloadPath, fw.version)) {
            Serial.println("❌ SD download failed");
            closeConnection();
            return false;
        }

        String file = "/firmware_" + fw.version + ".bin";
        closeConnection();
        return updateFromSD(file);

    } else {
        bool ok = downloadAndFlashDirect(downloadPath);
        closeConnection();
        return ok;
    }
}

// =========================
FirmwareInfo FirmwareUpdater::extractFirmwareInfo(const String& payload) {
    FirmwareInfo info;

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, payload)) {
        Serial.println("❌ JSON parse error");
        return info;
    }

    JsonObject shared = doc["shared"];

    if (!shared.isNull()) {
        if (shared.containsKey("fw_version"))
            info.version = shared["fw_version"].as<String>();

        if (shared.containsKey("fw_title"))
            info.title = shared["fw_title"].as<String>();

        if (shared.containsKey("fw_tag"))
            info.tag = shared["fw_tag"].as<String>();
    }

    return info;
}

// =========================
void FirmwareUpdater::cleanupOldFirmware() {
    File root = SD.open("/");
    if (!root) return;

    File file = root.openNextFile();
    while (file) {
        String name = file.name();

        if (name.endsWith(".bin")) {
            file.close();
            SD.remove("/" + name);
        } else {
            file.close();
        }

        file = root.openNextFile();
    }

    root.close();
}

// =========================
bool FirmwareUpdater::downloadFirmwareToSD(const String& path,
                                           const String& version) {

    Serial.println("📥 Download firmware to SD...");

    _http->get(path.c_str());

    int code = _http->responseStatusCode();
    int len  = _http->contentLength();

    if (code != 200 || len <= 0) {
        Serial.println("❌ Download HTTP error");
        return false;
    }

    File file = SD.open(_firmwareFile, FILE_WRITE);
    if (!file) {
        Serial.println("❌ File open error");
        return false;
    }

    uint8_t buf[512];
    int written = 0;

    while (_http->connected() || _http->available()) {
        int r = _http->readBytes(buf, sizeof(buf));
        if (r > 0) {
            file.write(buf, r);
            written += r;
        }
        delay(1);
    }

    file.close();

    String newName = "/firmware_" + version + ".bin";
    SD.rename(_firmwareFile, newName.c_str());

    Serial.println("✅ Download done");
    return true;
}

// =========================
bool FirmwareUpdater::downloadAndFlashDirect(const String& path) {

    Serial.println("📥 Streaming OTA (no SD)...");

    _http->get(path.c_str());

    int code = _http->responseStatusCode();
    int len  = _http->contentLength();

    if (code != 200 || len <= 0) {
        Serial.println("❌ HTTP error beim Stream");
        return false;
    }

    if (!Update.begin(len)) {
        Serial.println("❌ Update.begin() failed");
        return false;
    }

    uint8_t buf[512];
    int written = 0;

    while (_http->connected() || _http->available()) {
        int r = _http->readBytes(buf, sizeof(buf));
        if (r > 0) {
            if (Update.write(buf, r) != r) {
                Serial.println("❌ Write error");
                Update.abort();
                return false;
            }
            written += r;
        }
        delay(1);
    }

    if (!Update.end() || !Update.isFinished()) {
        Serial.println("❌ OTA nicht abgeschlossen");
        return false;
    }

    Serial.println("✅ Stream-OTA success → reboot");
    delay(2000);
    ESP.restart();
    return true;
}

// =========================
bool FirmwareUpdater::updateFromSD(const String& fileName) {

    Serial.println("🔄 OTA update from SD...");

    File file = SD.open(fileName);
    if (!file) {
        Serial.println("❌ File not found");
        return false;
    }

    size_t size = file.size();

    if (!Update.begin(size)) {
        Serial.println("❌ OTA begin failed");
        file.close();
        return false;
    }

    uint8_t buf[512];
    while (file.available()) {
        size_t r = file.read(buf, sizeof(buf));
        Update.write(buf, r);
    }

    file.close();

    if (!Update.end()) {
        Serial.println("❌ OTA failed");
        return false;
    }

    if (!Update.isFinished()) {
        Serial.println("❌ OTA not finished");
        return false;
    }

    Serial.println("✅ OTA success → reboot");
    delay(2000);
    ESP.restart();

    return true;
}
