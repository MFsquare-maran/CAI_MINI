#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <IniFile.h>
#include <math.h>     // NAN, isnan()

// ============================================================
//  Konfigurations-Struct (wird aus der INI gefüllt)
// ============================================================
struct IniConfig {
    // GENERAL
    uint32_t sending_period        = 60UL * 1000UL;   // Default 1 min

    // WIFI
    char     ssid[64]              = "";
    char     password[64]          = "";

    // THINGSBOARD
    char     thingsboardServer[64] = "";
    char     accessToken[64]       = "";
    uint16_t THINGSBOARD_PORT      = 1883;

    // Identifikation
    char     SenderID[32]          = "";
    char     DeviceID[32]          = "";

    // BME680-Offsets
    float    temperature_offset    = 0.0f;
    float    Pressure_offset       = 0.0f;
    float    Huminity_offset       = 0.0f;
    float    Gas_offset            = 0.0f;

    // Wind
    float    device_direction      = 0.0f;
    float    wind_vane_offset      = 0.0f;
    float    wind_speed_offset     = 0.0f;

    // Rain
    float    rain_offset           = 0.0f;

    // Wind-ADC-Tabelle
    uint16_t    wind_adc_table[16]    = {0};
    float    wind_direction_test   = 0.0f;
};

// ============================================================
//  Log-Eintrag für CSV (NAN = nicht gesetzt → leere Zelle)
// ============================================================
struct LogEntry {
    const char *datetime           = "";
    float       temperature        = NAN;
    float       pressure           = NAN;
    float       humidity           = NAN;
    float       gas_resistance     = NAN;
    float       battery_voltage    = NAN;
    float       wind_vane          = NAN;
    float       wind_speed_avg     = NAN;
    float       wind_speed_gust    = NAN;
    float       rain_gauge         = NAN;
};

// ============================================================
//  SDCard-Klasse
// ============================================================
class SDCard {
public:
    // alle ausgelesenen INI-Werte – nach readIni() zugreifbar
    IniConfig cfg;

    SDCard();

    // Initialisierung der SD-Karte
    bool init(uint8_t sd_clk, uint8_t sd_miso, uint8_t sd_mosi, uint8_t sd_cs);

    // INI-Datei lesen (füllt cfg)
    bool readIni(const char *path = "/INIT.ini");

    // Datenzeile in CSV-Datei schreiben
    bool writeLog(const LogEntry &entry, const char *path = "/data.csv");

    // SD-Karte wieder freigeben
    void release();

    // Statusabfrage
    bool isReady() const { return _initialized; }

private:
    bool _initialized = false;

    // Hilfsfunktion: druckt Wert nur, wenn er nicht NAN ist
    void _printIfValid(File &f, float value);
};

#endif // SDCARD_H