#include "sdcard.h"

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
        logln("❌ Fehler: SD-Karte konnte nicht initialisiert werden!");
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
        logln("❌ Fehler: SD-Karte konnte nicht initialisiert werden!");
        _initialized = false;
        return false;
    }
    logln("✅ SD-Karte erfolgreich initialisiert!");
    _initialized = true;
    return true;
}

// ============================================================
//  INI-Werte lesen (gibt nur tatsächlich gelesene Werte aus)
// ============================================================
bool SDCard::readIni(const char *path) {
    if (!_initialized) {
        logln("[INI] FEHLER: SD-Karte ist nicht initialisiert!");
        return false;
    }

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

    if (ini.getValue("homeassistant", "HA_BROKER", buffer, sizeof(buffer))) {
        strlcpy(cfg.ha_broker, buffer, sizeof(cfg.ha_broker));
        cfg.ha_enabled = true;
        logf("  HA Broker:           "); logln(cfg.ha_broker);
    }

    if (ini.getValue("homeassistant", "HA_PORT", buffer, sizeof(buffer))) {
        cfg.ha_port = atoi(buffer);
        logf("  HA Port:             "); logln(cfg.ha_port);
    }

    if (ini.getValue("homeassistant", "HA_DEVICE_ID", buffer, sizeof(buffer))) {
        strlcpy(cfg.ha_device_id, buffer, sizeof(cfg.ha_device_id));
        logf("  HA Device ID:        "); logln(cfg.ha_device_id);
    }

    if (ini.getValue("homeassistant", "HA_USER", buffer, sizeof(buffer))) {
        strlcpy(cfg.ha_user, buffer, sizeof(cfg.ha_user));
        logf("  HA User:             "); logln(cfg.ha_user);
    }

    if (ini.getValue("homeassistant", "HA_PASS", buffer, sizeof(buffer))) {
        strlcpy(cfg.ha_pass, buffer, sizeof(cfg.ha_pass));
        logf("  HA Pass:             "); logln(cfg.ha_pass);
    }

ini.close();
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
//  Datenzeile in CSV schreiben
//  → leere Zellen, wo Werte fehlen (NAN bzw. leerer String)
// ============================================================
bool SDCard::writeLog(const LogEntry &e, const char *path) {
    if (!_initialized) {
        logln("[LOG] FEHLER: SD-Karte ist nicht initialisiert!");
        return false;
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

    // Datum (leer falls nicht gesetzt)
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
    SD.end();
    _initialized = false;
    logln("[SD] SD-Karte freigegeben.");
}