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

    logln("🔍 Checking firmware update...");

    closeConnection();
    _http = new HttpClient(_wifiClient, _server, 443);
    _http->setTimeout(5000); // 5 Sekunden Timeout

    String path = "/api/v1/" + String(_deviceToken) +
                  "/attributes?sharedKeys=fw_version,fw_title,fw_tag";

    int err = _http->get(path.c_str());

    if (err != 0) {
        logf("❌ Verbindung/Timeout bei Version-Check, err=");
        logln(err);
        closeConnection();
        return false;
    }

    int statusCode = _http->responseStatusCode();
    String response = _http->responseBody();

    logf("HTTP: ");
    logln(statusCode);

    if (statusCode != 200) {
        logln("❌ HTTP error");
        closeConnection();
        return false;
    }

    response.trim();

    if (response.length() < 5 || response == "{}") {
        logln("ℹ️ No firmware info");
        closeConnection();
        return false;
    }

    FirmwareInfo fw = extractFirmwareInfo(response);

    if (fw.version.length() == 0) {
        logln("⚠️ No version found");
        closeConnection();
        return false;
    }

    if (fw.version == _currentVersion) {
        logln("✅ Firmware up to date");
        closeConnection();
        return false;
    }

    logln("🆕 New firmware: " + fw.version);

    String downloadPath = "/api/v1/" + String(_deviceToken) +
                          "/firmware?title=" + fw.title +
                          "&version=" + fw.version;

    if (useSD) {
        cleanupOldFirmware();

        if (!downloadFirmwareToSD(downloadPath, fw.version)) {
            logln("❌ SD download failed");
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
        logln("❌ JSON parse error");
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

    logln("📥 Download firmware to SD...");

    _http->get(path.c_str());

    int code = _http->responseStatusCode();
    int len  = _http->contentLength();

    if (code != 200 || len <= 0) {
        logln("❌ Download HTTP error");
        return false;
    }

    File file = SD.open(_firmwareFile, FILE_WRITE);
    if (!file) {
        logln("❌ File open error");
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

    logln("✅ Download done");
    return true;
}

// =========================
bool FirmwareUpdater::downloadAndFlashDirect(const String& path) {

    logln("📥 Streaming OTA (no SD)...");

    _http->get(path.c_str());

    int code = _http->responseStatusCode();
    int len  = _http->contentLength();

    if (code != 200 || len <= 0) {
        logln("❌ HTTP error beim Stream");
        return false;
    }

    if (!Update.begin(len)) {
        logln("❌ Update.begin() failed");
        return false;
    }

    uint8_t buf[512];
    int written = 0;

    while (_http->connected() || _http->available()) {
        int r = _http->readBytes(buf, sizeof(buf));
        if (r > 0) {
            if (Update.write(buf, r) != r) {
                logln("❌ Write error");
                Update.abort();
                return false;
            }
            written += r;
        }
        delay(1);
    }

    if (!Update.end() || !Update.isFinished()) {
        logln("❌ OTA nicht abgeschlossen");
        return false;
    }

    logln("✅ Stream-OTA success → reboot");
    delay(2000);
    ESP.restart();
    return true;
}

// =========================
bool FirmwareUpdater::updateFromSD(const String& fileName) {

    logln("🔄 OTA update from SD...");

    File file = SD.open(fileName);
    if (!file) {
        logln("❌ File not found");
        return false;
    }

    size_t size = file.size();

    if (!Update.begin(size)) {
        logln("❌ OTA begin failed");
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
        logln("❌ OTA failed");
        return false;
    }

    if (!Update.isFinished()) {
        logln("❌ OTA not finished");
        return false;
    }

    logln("✅ OTA success → reboot");
    delay(2000);
    ESP.restart();

    return true;
}
