#include "sdcard.h"

// ============================================================
//  Konstruktor
// ============================================================
SDCard::SDCard() {}

// ============================================================
//  SD-Karte initialisieren
// ============================================================
bool SDCard::init(uint8_t sd_clk, uint8_t sd_miso, uint8_t sd_mosi, uint8_t sd_cs) {
    SPI.begin(sd_clk, sd_miso, sd_mosi, sd_cs);
    if (!SD.begin(sd_cs)) {
        Serial.println("❌ Fehler: SD-Karte konnte nicht initialisiert werden!");
        _initialized = false;
        return false;
    }
    Serial.println("✅ SD-Karte erfolgreich initialisiert!");
    _initialized = true;
    return true;
}

// ============================================================
//  INI-Werte lesen (gibt nur tatsächlich gelesene Werte aus)
// ============================================================
bool SDCard::readIni(const char *path) {
    if (!_initialized) {
        Serial.println("[INI] FEHLER: SD-Karte ist nicht initialisiert!");
        return false;
    }

    IniFile ini(path, FILE_READ, true);
    if (!ini.open()) {
        Serial.println("[INI] FEHLER: INI-Datei konnte nicht geöffnet werden!");
        return false;
    }

    char buffer[128];
    Serial.println("[INI] Werte gelesen:");

    // ---------- GENERAL ----------
    if (ini.getValue("GENERAL", "SENDING_PERIOD", buffer, sizeof(buffer))) {
        cfg.sending_period = (uint32_t)atoi(buffer) * 1000UL * 60UL;
        Serial.print("  Sending Period (ms): "); Serial.println(cfg.sending_period);
    }

    // ---------- WIFI ----------
    if (ini.getValue("WIFI", "SSID", buffer, sizeof(buffer))) {
        strncpy(cfg.ssid, buffer, sizeof(cfg.ssid) - 1);
        Serial.print("  SSID:                "); Serial.println(cfg.ssid);
    }
    if (ini.getValue("WIFI", "PW", buffer, sizeof(buffer))) {
        strncpy(cfg.password, buffer, sizeof(cfg.password) - 1);
        Serial.print("  Password:            "); Serial.println(cfg.password);
    }

    // ---------- THINGSBOARD ----------
    if (ini.getValue("THINGSBOARD", "TB_ADRESS", buffer, sizeof(buffer))) {
        strncpy(cfg.thingsboardServer, buffer, sizeof(cfg.thingsboardServer) - 1);
        Serial.print("  TB Server:           "); Serial.println(cfg.thingsboardServer);
    }
    if (ini.getValue("THINGSBOARD", "TB_TOKKEN", buffer, sizeof(buffer))) {
        strncpy(cfg.accessToken, buffer, sizeof(cfg.accessToken) - 1);
        Serial.print("  TB Token:            "); Serial.println(cfg.accessToken);
    }
    if (ini.getValue("THINGSBOARD", "TB_PORT", buffer, sizeof(buffer))) {
        cfg.THINGSBOARD_PORT = atoi(buffer);
        Serial.print("  TB Port:             "); Serial.println(cfg.THINGSBOARD_PORT);
    }

    // ---------- IDENTIFIKATION ----------
    if (ini.getValue("SEND_TO", "NAME", buffer, sizeof(buffer))) {
        strncpy(cfg.SenderID, buffer, sizeof(cfg.SenderID) - 1);
        Serial.print("  Sender ID:           "); Serial.println(cfg.SenderID);
    }
    if (ini.getValue("ID", "NAME", buffer, sizeof(buffer))) {
        strncpy(cfg.DeviceID, buffer, sizeof(cfg.DeviceID) - 1);
        Serial.print("  Device ID:           "); Serial.println(cfg.DeviceID);
    }

    // ---------- BME680 ----------
    if (ini.getValue("BME680", "TEMPERATURE_OFFSET", buffer, sizeof(buffer))) {
        cfg.temperature_offset = strtof(buffer, nullptr);
        Serial.print("  Temp  Offset:        "); Serial.println(cfg.temperature_offset);
    }
    if (ini.getValue("BME680", "PRESSURE_OFFSET", buffer, sizeof(buffer))) {
        cfg.Pressure_offset = strtof(buffer, nullptr);
        Serial.print("  Press Offset:        "); Serial.println(cfg.Pressure_offset);
    }
    if (ini.getValue("BME680", "HUMINITY_OFFSET", buffer, sizeof(buffer))) {
        cfg.Huminity_offset = strtof(buffer, nullptr);
        Serial.print("  Humi  Offset:        "); Serial.println(cfg.Huminity_offset);
    }
    if (ini.getValue("BME680", "GAS_OFFSET", buffer, sizeof(buffer))) {
        cfg.Gas_offset = strtof(buffer, nullptr);
        Serial.print("  Gas   Offset:        "); Serial.println(cfg.Gas_offset);
    }

    // ---------- WIND ----------
    if (ini.getValue("WIND", "DEVICE_DIRECTION", buffer, sizeof(buffer))) {
        cfg.device_direction = strtof(buffer, nullptr);
        Serial.print("  Device Direction:    "); Serial.println(cfg.device_direction);
    }
    if (ini.getValue("WIND", "WIND_VANE_OFFSET", buffer, sizeof(buffer))) {
        cfg.wind_vane_offset = strtof(buffer, nullptr);
        Serial.print("  Wind Vane Offset:    "); Serial.println(cfg.wind_vane_offset);
    }
    if (ini.getValue("WIND", "WIND_SPEED_OFFSET", buffer, sizeof(buffer))) {
        cfg.wind_speed_offset = strtof(buffer, nullptr);
        Serial.print("  Wind Speed Offset:   "); Serial.println(cfg.wind_speed_offset);
    }

    // ---------- RAIN ----------
    if (ini.getValue("RAIN", "RAIN_OFFSET", buffer, sizeof(buffer))) {
        cfg.rain_offset = strtof(buffer, nullptr);
        Serial.print("  Rain Offset:         "); Serial.println(cfg.rain_offset);
    }

    // ---------- WINDADCVALUES ----------
    bool any_adc = false;
    for (size_t i = 0; i < 16; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", (int)i);
        if (ini.getValue("WINDADCVALUES", key, buffer, sizeof(buffer))) {
            cfg.wind_adc_table[i] = strtof(buffer, nullptr);
            if (!any_adc) {
                Serial.println("  Wind ADC Table:");
                any_adc = true;
            }
            Serial.print("    ");
            Serial.print(i * 22.5);
            Serial.print("° -> ADC: ");
            Serial.println(cfg.wind_adc_table[i]);
        }
    }
    if (ini.getValue("WINDADCVALUES", "WIND_Direction_TEST", buffer, sizeof(buffer))) {
        cfg.wind_direction_test = strtof(buffer, nullptr);
        Serial.print("  Wind Direction Test: "); Serial.println(cfg.wind_direction_test);
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
        Serial.println("[LOG] FEHLER: SD-Karte ist nicht initialisiert!");
        return false;
    }

    // Datei mit Header anlegen, falls noch nicht vorhanden
    if (!SD.exists(path)) {
        File f = SD.open(path, FILE_WRITE);
        if (!f) {
            Serial.println("[LOG] FEHLER: CSV konnte nicht angelegt werden!");
            return false;
        }
        f.println("Date,Temperature,Pressure,Humidity,Gas_Resistance,"
                  "Battery_Voltage,Wind_Vane,Wind_Speed_Avg,Wind_Speed_Gust,Rain_Gauge");
        f.close();
        Serial.println("[LOG] CSV neu erstellt mit Header.");
    }

    File f = SD.open(path, FILE_APPEND);
    if (!f) {
        Serial.println("[LOG] FEHLER: CSV konnte nicht geöffnet werden!");
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

    Serial.println("[LOG] Zeile geschrieben.");
    return true;
}

// ============================================================
//  SD-Karte freigeben
// ============================================================
void SDCard::release() {
    SD.end();
    _initialized = false;
    Serial.println("[SD] SD-Karte freigegeben.");
}