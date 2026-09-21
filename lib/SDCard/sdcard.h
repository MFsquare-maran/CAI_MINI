#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <IniFile.h>
#include <math.h>     // NAN, isnan()
#include "log.h"

// ============================================================
//  Test-/Fallback-Defaults
//  → nur genutzt, wenn KEINE SD-Karte UND KEINE Flash-Config da ist
//  → jeweils per -DSIM_xxx in platformio.ini überschreibbar
// ============================================================
// --- GENERAL ---
#ifndef SIM_SENDING_PERIOD_MIN
  #define SIM_SENDING_PERIOD_MIN 1
#endif
// --- WIFI ---
#ifndef SIM_SSID
  #define SIM_SSID        "TEST"
#endif
#ifndef SIM_PW
  #define SIM_PW          "123456789"
#endif
// --- THINGSBOARD ---
#ifndef SIM_TB_ADRESS
  #define SIM_TB_ADRESS   "iot.mfsquare.ch"
#endif
#ifndef SIM_TB_TOKKEN
  #define SIM_TB_TOKKEN   ""
#endif
#ifndef SIM_TB_PORT
  #define SIM_TB_PORT     1884
#endif
// --- IDENTIFIKATION ---
#ifndef SIM_DEVICE_ID
  #define SIM_DEVICE_ID   "CAI-MINI-SIM"
#endif
#ifndef SIM_SENDER_ID
  #define SIM_SENDER_ID   "SIM_SENDER"
#endif
// --- BME680-Offsets ---
#ifndef SIM_TEMP_OFFSET
  #define SIM_TEMP_OFFSET       -2.05f
#endif
#ifndef SIM_PRESSURE_OFFSET
  #define SIM_PRESSURE_OFFSET   0.0f
#endif
#ifndef SIM_HUMINITY_OFFSET
  #define SIM_HUMINITY_OFFSET   0.0f
#endif
#ifndef SIM_GAS_OFFSET
  #define SIM_GAS_OFFSET        0.0f
#endif
// --- WIND ---
#ifndef SIM_DEVICE_DIRECTION
  #define SIM_DEVICE_DIRECTION    0.0f
#endif
#ifndef SIM_WIND_VANE_OFFSET
  #define SIM_WIND_VANE_OFFSET    0.0f
#endif
#ifndef SIM_WIND_SPEED_OFFSET
  #define SIM_WIND_SPEED_OFFSET   0.0f
#endif
#ifndef SIM_WIND_DIRECTION_TEST
  #define SIM_WIND_DIRECTION_TEST 0.0f
#endif
// 16 ADC-Stützpunkte (0°..337.5° in 22.5°-Schritten) – PLATZHALTER, anpassen!
#ifndef SIM_WIND_ADC_TABLE
  #define SIM_WIND_ADC_TABLE { 0, 273, 546, 819, 1092, 1365, 1638, 1911, \
                               2184, 2457, 2730, 3003, 3276, 3549, 3822, 4095 }
#endif
// --- RAIN ---
#ifndef SIM_RAIN_OFFSET
  #define SIM_RAIN_OFFSET   0.0f
#endif
// --- HOME ASSISTANT ---
#ifndef SIM_HA_ENABLED
  #define SIM_HA_ENABLED    0            // 0 = aus (kein Broker-Verbindungsversuch im Test)
#endif
#ifndef SIM_HA_BROKER
  #define SIM_HA_BROKER     "192.168.1.10"
#endif
#ifndef SIM_HA_PORT
  #define SIM_HA_PORT       1883
#endif
#ifndef SIM_HA_DEVICE_ID
  #define SIM_HA_DEVICE_ID  "cai-mini-sim"
#endif
#ifndef SIM_HA_USER
  #define SIM_HA_USER       "mqtt"
#endif
#ifndef SIM_HA_PASS
  #define SIM_HA_PASS       "mqtt"
#endif

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
    uint16_t wind_adc_table[16]    = {0};
    float    wind_direction_test   = 0.0f;

    // HOME ASSISTANT
    char     ha_broker[64]         = "";
    uint16_t ha_port               = 0;
    char     ha_device_id[32]      = "";
    bool     ha_enabled            = false;
    char     ha_user[32]           = "";
    char     ha_pass[32]           = "";
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
    // Woher stammt die aktive Config?
    enum class ConfigSource { NONE, SD_CARD, FLASH, TEST };

    // alle ausgelesenen INI-Werte – nach readIni() zugreifbar
    IniConfig cfg;

    SDCard();

    // Initialisierung der SD-Karte
    bool init(uint8_t sd_clk, uint8_t sd_miso, uint8_t sd_mosi, uint8_t sd_cs);
    bool init(uint8_t sd_clk, uint8_t sd_miso, uint8_t sd_mosi, uint8_t sd_cs, SPIClass &spi);

    // Config beschaffen: SD → Flash → Testdaten (füllt cfg, immer true)
    bool readIni(const char *path = "/INIT.ini");

    // Datenzeile in CSV-Datei schreiben (ohne SD: Ausgabe auf Konsole)
    bool writeLog(const LogEntry &entry, const char *path = "/data.csv");

    // SD-Karte wieder freigeben
    void release();

    // Flash-Persistenz (NVS)
    bool saveToFlash();     // spiegelt cfg in den Flash (nur bei Änderung)
    bool loadFromFlash();   // lädt cfg aus dem Flash (false = nichts gespeichert)

    // Statusabfragen
    bool         isReady()     const { return _initialized; }
    ConfigSource source()      const { return _source; }
    bool         isSimulated() const { return _source == ConfigSource::TEST; }

private:
    bool         _initialized = false;
    ConfigSource _source      = ConfigSource::NONE;

    bool _readIniFromSD(const char *path);   // reines SD-Lesen (true = ok)
    void loadSimDefaults();                   // füllt cfg mit SIM_*-Werten
    void _logSummary();                       // kurze Ausgabe der Kernwerte
    void _printIfValid(File &f, float value);
};

#endif // SDCARD_H