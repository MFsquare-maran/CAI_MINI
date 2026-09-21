#include "sdcard.h"
#include <Preferences.h>   // NVS – im ESP32-Core enthalten, kein lib_deps nötig
#include <string.h>        // memcmp, memcpy

// NVS-Ablageort für die Config-Spiegelung
static const char *NVS_NAMESPACE = "caimini";
static const char *NVS_KEY       = "cfg";

// ============================================================
//  Konstruktor
// ============================================================
SDCard::SDCard() {}

// ============================================================
//  SD-Karte initialisieren – Standard SPI Bus
// ============================================================
bool SDCard::init(uint8_t sd_clk, uint8_t sd_miso, uint8_t sd_mosi, uint8_t sd_cs) {
    SPI.begin(sd_clk, sd_miso, sd_mosi, sd_cs);
    if (!SD.begin(sd_cs)) {
        logln("⚠️  Keine SD-Karte gefunden – Fallback (Flash/Test) wird genutzt.");
        _initialized = false;
        return false;
    }
    logln("✅ SD-Karte erfolgreich initialisiert!");
    _initialized = true;
    return true;
}

// ============================================================
//  SD-Karte initialisieren – eigener SPI Bus (z.B. HSPI/FSPI)
// ============================================================
bool SDCard::init(uint8_t sd_clk, uint8_t sd_miso, uint8_t sd_mosi, uint8_t sd_cs, SPIClass &spi) {
    spi.begin(sd_clk, sd_miso, sd_mosi, sd_cs);
    if (!SD.begin(sd_cs, spi)) {
        logln("⚠️  Keine SD-Karte gefunden – Fallback (Flash/Test) wird genutzt.");
        _initialized = false;
        return false;
    }
    logln("✅ SD-Karte erfolgreich initialisiert!");
    _initialized = true;
    return true;
}

// ============================================================
//  Config beschaffen – 3-stufiger Fallback
//    1) echte SD-Karte  → INI lesen, danach in Flash spiegeln
//    2) Flash (NVS)     → zuletzt gespeicherte Config
//    3) Testdaten       → nur wenn nichts davon vorhanden
//  Gibt immer true zurück (cfg ist danach in jedem Fall gefüllt).
// ============================================================
bool SDCard::readIni(const char *path) {
    // --- 1) Echte SD-Karte ---
    if (_initialized) {
        if (_readIniFromSD(path)) {
            _source = ConfigSource::SD_CARD;
            saveToFlash();   // Config für spätere Boots ohne Karte sichern
            return true;
        }
        logln("[INI] SD vorhanden, aber INI nicht lesbar – versuche Flash …");
    }

    // --- 2) Flash (NVS) ---
    if (loadFromFlash()) {
        _source = ConfigSource::FLASH;
        logln("[INI] Config aus Flash geladen:");
        _logSummary();
        return true;
    }

    // --- 3) Testdaten (allererster Start ohne SD/Flash) ---
    logln("🧪 [INI] Weder SD noch Flash-Config – Testdaten aktiv:");
    loadSimDefaults();
    _source = ConfigSource::TEST;
    _logSummary();
    return true;
}

// ============================================================
//  Reines SD-Lesen (füllt cfg) – true bei Erfolg
// ============================================================
bool SDCard::_readIniFromSD(const char *path) {
    IniFile ini(path, FILE_READ, true);
    if (!ini.open()) {
        logln("[INI] FEHLER: INI-Datei konnte nicht geöffnet werden!");
        return false;
    }

    char buffer[128];
    logln("[INI] Werte gelesen:");

    // ---------- GENERAL ----------
    if (ini.getValue("GENERAL", "SENDING_PERIOD", buffer, sizeof(buffer))) {
        cfg.sending_period = (uint32_t)atoi(buffer) * 1000UL * 60UL;
        logf("  Sending Period (ms): "); logln(cfg.sending_period);
    }

    // ---------- WIFI ----------
    if (ini.getValue("WIFI", "SSID", buffer, sizeof(buffer))) {
        strncpy(cfg.ssid, buffer, sizeof(cfg.ssid) - 1);
        logf("  SSID:                "); logln(cfg.ssid);
    }
    if (ini.getValue("WIFI", "PW", buffer, sizeof(buffer))) {
        strncpy(cfg.password, buffer, sizeof(cfg.password) - 1);
        logf("  Password:            "); logln(cfg.password);
    }

    // ---------- THINGSBOARD ----------
    if (ini.getValue("THINGSBOARD", "TB_ADRESS", buffer, sizeof(buffer))) {
        strncpy(cfg.thingsboardServer, buffer, sizeof(cfg.thingsboardServer) - 1);
        logf("  TB Server:           "); logln(cfg.thingsboardServer);
    }
    if (ini.getValue("THINGSBOARD", "TB_TOKKEN", buffer, sizeof(buffer))) {
        strncpy(cfg.accessToken, buffer, sizeof(cfg.accessToken) - 1);
        logf("  TB Token:            "); logln(cfg.accessToken);
    }
    if (ini.getValue("THINGSBOARD", "TB_PORT", buffer, sizeof(buffer))) {
        cfg.THINGSBOARD_PORT = atoi(buffer);
        logf("  TB Port:             "); logln(cfg.THINGSBOARD_PORT);
    }

    // ---------- IDENTIFIKATION ----------
    if (ini.getValue("SEND_TO", "NAME", buffer, sizeof(buffer))) {
        strncpy(cfg.SenderID, buffer, sizeof(cfg.SenderID) - 1);
        logf("  Sender ID:           "); logln(cfg.SenderID);
    }
    if (ini.getValue("ID", "NAME", buffer, sizeof(buffer))) {
        strncpy(cfg.DeviceID, buffer, sizeof(cfg.DeviceID) - 1);
        logf("  Device ID:           "); logln(cfg.DeviceID);
    }

    // ---------- BME680 ----------
    if (ini.getValue("BME680", "TEMPERATURE_OFFSET", buffer, sizeof(buffer))) {
        cfg.temperature_offset = strtof(buffer, nullptr);
        logf("  Temp  Offset:        "); logln(cfg.temperature_offset);
    }
    if (ini.getValue("BME680", "PRESSURE_OFFSET", buffer, sizeof(buffer))) {
        cfg.Pressure_offset = strtof(buffer, nullptr);
        logf("  Press Offset:        "); logln(cfg.Pressure_offset);
    }
    if (ini.getValue("BME680", "HUMINITY_OFFSET", buffer, sizeof(buffer))) {
        cfg.Huminity_offset = strtof(buffer, nullptr);
        logf("  Humi  Offset:        "); logln(cfg.Huminity_offset);
    }
    if (ini.getValue("BME680", "GAS_OFFSET", buffer, sizeof(buffer))) {
        cfg.Gas_offset = strtof(buffer, nullptr);
        logf("  Gas   Offset:        "); logln(cfg.Gas_offset);
    }

    // ---------- WIND ----------
    if (ini.getValue("WIND", "DEVICE_DIRECTION", buffer, sizeof(buffer))) {
        cfg.device_direction = strtof(buffer, nullptr);
        logf("  Device Direction:    "); logln(cfg.device_direction);
    }
    if (ini.getValue("WIND", "WIND_VANE_OFFSET", buffer, sizeof(buffer))) {
        cfg.wind_vane_offset = strtof(buffer, nullptr);
        logf("  Wind Vane Offset:    "); logln(cfg.wind_vane_offset);
    }
    if (ini.getValue("WIND", "WIND_SPEED_OFFSET", buffer, sizeof(buffer))) {
        cfg.wind_speed_offset = strtof(buffer, nullptr);
        logf("  Wind Speed Offset:   "); logln(cfg.wind_speed_offset);
    }

    // ---------- RAIN ----------
    if (ini.getValue("RAIN", "RAIN_OFFSET", buffer, sizeof(buffer))) {
        cfg.rain_offset = strtof(buffer, nullptr);
        logf("  Rain Offset:         "); logln(cfg.rain_offset);
    }

    // ---------- WINDADCVALUES ----------
    bool any_adc = false;
    for (size_t i = 0; i < 16; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", (int)i);
        if (ini.getValue("WINDADCVALUES", key, buffer, sizeof(buffer))) {
            cfg.wind_adc_table[i] = strtof(buffer, nullptr);
            if (!any_adc) {
                logln("  Wind ADC Table:");
                any_adc = true;
            }
            logf("    ");
            logf(i * 22.5);
            logf("° -> ADC: ");
            logln(cfg.wind_adc_table[i]);
        }
    }
    if (ini.getValue("WINDADCVALUES", "WIND_Direction_TEST", buffer, sizeof(buffer))) {
        cfg.wind_direction_test = strtof(buffer, nullptr);
        logf("  Wind Direction Test: "); logln(cfg.wind_direction_test);
    }

    // ---------- HOMEASSISTANT ----------
    cfg.ha_enabled = false;

    if (ini.getValue("HOMEASSISTANT", "HA_BROKER", buffer, sizeof(buffer))) {
        strlcpy(cfg.ha_broker, buffer, sizeof(cfg.ha_broker));
        cfg.ha_enabled = true;
        logf("  HA Broker:           "); logln(cfg.ha_broker);
    }
    if (ini.getValue("HOMEASSISTANT", "HA_PORT", buffer, sizeof(buffer))) {
        cfg.ha_port = atoi(buffer);
        logf("  HA Port:             "); logln(cfg.ha_port);
    }
    if (ini.getValue("HOMEASSISTANT", "HA_DEVICE_ID", buffer, sizeof(buffer))) {
        strlcpy(cfg.ha_device_id, buffer, sizeof(cfg.ha_device_id));
        logf("  HA Device ID:        "); logln(cfg.ha_device_id);
    }
    if (ini.getValue("HOMEASSISTANT", "HA_USER", buffer, sizeof(buffer))) {
        strlcpy(cfg.ha_user, buffer, sizeof(cfg.ha_user));
        logf("  HA User:             "); logln(cfg.ha_user);
    }
    if (ini.getValue("HOMEASSISTANT", "HA_PASS", buffer, sizeof(buffer))) {
        strlcpy(cfg.ha_pass, buffer, sizeof(cfg.ha_pass));
        logf("  HA Pass:             "); logln(cfg.ha_pass);
    }

    ini.close();
    return true;
}

// ============================================================
//  Testdaten in cfg laden (SIM_*-Defaults aus dem Header)
// ============================================================
void SDCard::loadSimDefaults() {
    // GENERAL
    cfg.sending_period = (uint32_t)SIM_SENDING_PERIOD_MIN * 1000UL * 60UL;

    // WIFI
    strncpy(cfg.ssid,     SIM_SSID, sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, SIM_PW,   sizeof(cfg.password) - 1);

    // THINGSBOARD
    strncpy(cfg.thingsboardServer, SIM_TB_ADRESS, sizeof(cfg.thingsboardServer) - 1);
    strncpy(cfg.accessToken,       SIM_TB_TOKKEN, sizeof(cfg.accessToken) - 1);
    cfg.THINGSBOARD_PORT = SIM_TB_PORT;

    // IDENTIFIKATION
    strncpy(cfg.DeviceID, SIM_DEVICE_ID, sizeof(cfg.DeviceID) - 1);
    strncpy(cfg.SenderID, SIM_SENDER_ID, sizeof(cfg.SenderID) - 1);

    // BME680-Offsets
    cfg.temperature_offset = SIM_TEMP_OFFSET;
    cfg.Pressure_offset    = SIM_PRESSURE_OFFSET;
    cfg.Huminity_offset    = SIM_HUMINITY_OFFSET;
    cfg.Gas_offset         = SIM_GAS_OFFSET;

    // WIND
    cfg.device_direction    = SIM_DEVICE_DIRECTION;
    cfg.wind_vane_offset    = SIM_WIND_VANE_OFFSET;
    cfg.wind_speed_offset   = SIM_WIND_SPEED_OFFSET;
    cfg.wind_direction_test = SIM_WIND_DIRECTION_TEST;

    static const uint16_t sim_adc[16] = SIM_WIND_ADC_TABLE;
    memcpy(cfg.wind_adc_table, sim_adc, sizeof(cfg.wind_adc_table));

    // RAIN
    cfg.rain_offset = SIM_RAIN_OFFSET;

    // HOME ASSISTANT
    cfg.ha_enabled = (SIM_HA_ENABLED != 0);
    strncpy(cfg.ha_broker,    SIM_HA_BROKER,    sizeof(cfg.ha_broker) - 1);
    cfg.ha_port = SIM_HA_PORT;
    strncpy(cfg.ha_device_id, SIM_HA_DEVICE_ID, sizeof(cfg.ha_device_id) - 1);
    strncpy(cfg.ha_user,      SIM_HA_USER,      sizeof(cfg.ha_user) - 1);
    strncpy(cfg.ha_pass,      SIM_HA_PASS,      sizeof(cfg.ha_pass) - 1);
}


// ============================================================
//  Zusammenfassung ALLER Config-Werte 
// ============================================================
void SDCard::_logSummary() {
    const char *src = "keine";
    switch (_source) {
        case ConfigSource::SD_CARD: src = "SD-Karte";  break;
        case ConfigSource::FLASH:   src = "Flash";     break;
        case ConfigSource::TEST:    src = "Testdaten"; break;
        default: break;
    }
    logf("  Quelle:        "); logln(src);

    logln("  -- GENERAL --");
    logf("  Periode(ms):   "); logln(cfg.sending_period);

    logln("  -- WIFI --");
    logf("  SSID:          "); logln(cfg.ssid);
    logf("  PW:            "); logln(cfg.password);

    logln("  -- THINGSBOARD --");
    logf("  Server:        "); logln(cfg.thingsboardServer);
    logf("  Token:         "); logln(cfg.accessToken);
    logf("  Port:          "); logln(cfg.THINGSBOARD_PORT);

    logln("  -- ID --");
    logf("  Device ID:     "); logln(cfg.DeviceID);
    logf("  Sender ID:     "); logln(cfg.SenderID);

    logln("  -- BME680-Offsets --");
    logf("  Temp:          "); logln(cfg.temperature_offset);
    logf("  Pressure:      "); logln(cfg.Pressure_offset);
    logf("  Humidity:      "); logln(cfg.Huminity_offset);
    logf("  Gas:           "); logln(cfg.Gas_offset);

    logln("  -- WIND --");
    logf("  Device Dir:    "); logln(cfg.device_direction);
    logf("  Vane Offset:   "); logln(cfg.wind_vane_offset);
    logf("  Speed Offset:  "); logln(cfg.wind_speed_offset);
    logf("  Dir Test:      "); logln(cfg.wind_direction_test);
    logln("  ADC-Tabelle:");
    for (size_t i = 0; i < 16; i++) {
        logf("    "); logf(i * 22.5); logf("° -> "); logln(cfg.wind_adc_table[i]);
    }

    logln("  -- RAIN --");
    logf("  Rain Offset:   "); logln(cfg.rain_offset);

    logln("  -- HOME ASSISTANT --");
    logf("  Enabled:       "); logln(cfg.ha_enabled ? "ja" : "nein");
    logf("  Broker:        "); logln(cfg.ha_broker);
    logf("  Port:          "); logln(cfg.ha_port);
    logf("  HA Device ID:  "); logln(cfg.ha_device_id);
    logf("  User:          "); logln(cfg.ha_user);
    logf("  Pass:          "); logln(cfg.ha_pass);
}

// ============================================================
//  cfg in den Flash (NVS) spiegeln – nur bei Änderung
// ============================================================
bool SDCard::saveToFlash() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        logln("[FLASH] FEHLER: NVS konnte nicht geöffnet werden.");
        return false;
    }

    // Nur schreiben, wenn sich etwas geändert hat (schont den Flash)
    bool differs = true;
    if (prefs.getBytesLength(NVS_KEY) == sizeof(IniConfig)) {
        IniConfig stored;
        prefs.getBytes(NVS_KEY, &stored, sizeof(stored));
        differs = (memcmp(&stored, &cfg, sizeof(IniConfig)) != 0);
    }

    bool ok = true;
    if (differs) {
        size_t n = prefs.putBytes(NVS_KEY, &cfg, sizeof(cfg));
        ok = (n == sizeof(cfg));
        logln(ok ? "[FLASH] Config gespeichert." : "[FLASH] FEHLER beim Speichern.");
    } else {
        logln("[FLASH] Config unverändert – kein Schreibzugriff.");
    }

    prefs.end();
    return ok;
}

// ============================================================
//  cfg aus dem Flash (NVS) laden
//  → false, wenn nichts (oder ein anderes Layout) gespeichert ist
// ============================================================
bool SDCard::loadFromFlash() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {   // read-only; false = Namespace existiert nicht
        return false;
    }

    // Layout-Guard: nur laden, wenn Größe exakt passt
    if (prefs.getBytesLength(NVS_KEY) != sizeof(IniConfig)) {
        prefs.end();
        return false;
    }

    prefs.getBytes(NVS_KEY, &cfg, sizeof(cfg));
    prefs.end();
    return true;
}

// ============================================================
//  Hilfsfunktion: druckt Wert nur, wenn er nicht NAN ist
// ============================================================
void SDCard::_printIfValid(File &f, float value) {
    if (!isnan(value)) {
        f.print(value);
    }
    // andernfalls: leere Zelle
}

// ============================================================
//  Datenzeile schreiben – mit SD in CSV, ohne SD auf die Konsole
// ============================================================
bool SDCard::writeLog(const LogEntry &e, const char *path) {
    if (!_initialized) {
        // Kein physischer Speicher → Zeile nur ausgeben (Bench/Test)
        logf("🧪 [LOG-SIM] ");
        logf(e.datetime); logf(" | T=");
        if (!isnan(e.temperature))     logf(e.temperature);
        logf(" P=");
        if (!isnan(e.pressure))        logf(e.pressure);
        logf(" H=");
        if (!isnan(e.humidity))        logf(e.humidity);
        logf(" Bat=");
        if (!isnan(e.battery_voltage)) logf(e.battery_voltage);
        logln("");
        return true;
    }

    // Datei mit Header anlegen, falls noch nicht vorhanden
    if (!SD.exists(path)) {
        File f = SD.open(path, FILE_WRITE);
        if (!f) {
            logln("[LOG] FEHLER: CSV konnte nicht angelegt werden!");
            return false;
        }
        f.println("Date,Temperature,Pressure,Humidity,Gas_Resistance,"
                  "Battery_Voltage,Wind_Vane,Wind_Speed_Avg,Wind_Speed_Gust,Rain_Gauge");
        f.close();
        logln("[LOG] CSV neu erstellt mit Header.");
    }

    File f = SD.open(path, FILE_APPEND);
    if (!f) {
        logln("[LOG] FEHLER: CSV konnte nicht geöffnet werden!");
        return false;
    }

    if (e.datetime && e.datetime[0] != '\0') f.print(e.datetime);
    f.print(",");

    _printIfValid(f, e.temperature);     f.print(",");
    _printIfValid(f, e.pressure);        f.print(",");
    _printIfValid(f, e.humidity);        f.print(",");
    _printIfValid(f, e.gas_resistance);  f.print(",");
    _printIfValid(f, e.battery_voltage); f.print(",");
    _printIfValid(f, e.wind_vane);       f.print(",");
    _printIfValid(f, e.wind_speed_avg);  f.print(",");
    _printIfValid(f, e.wind_speed_gust); f.print(",");
    _printIfValid(f, e.rain_gauge);
    f.println();
    f.close();

    logln("[LOG] Zeile geschrieben.");
    return true;
}

// ============================================================
//  SD-Karte freigeben
// ============================================================
void SDCard::release() {
    if (!_initialized) return;   // ohne Karte nichts zu tun
    SD.end();
    _initialized = false;
    logln("[SD] SD-Karte freigegeben.");
}