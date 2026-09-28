#pragma once

// ============================================================
//  TelemetryKeys.h
//  Zentrale Tabelle: LoRa-Kurzkey  ↔  ThingsBoard-Key
//  Datum: 2026-09-28
//
//  Neuen Wert hinzufuegen:
//    1) #define TK_... hier eintragen
//    2) Zeile in TELEMETRY_KEYS[] ergaenzen
//    3) Sensor/Router: tp.add(TK_..., wert, nachkommastellen);
//
//  Regeln:
//    - Kurzkeys: nur Grossbuchstaben/Ziffern, 1–3 Zeichen
//    - jeder Kurzkey genau EINMAL
//    - TB-Key exakt wie in ThingsBoard (case-sensitive!)
//    - Unbekannter Kurzkey → Gateway leitet ihn unveraendert
//      weiter und loggt eine Warnung
// ============================================================

#include <Arduino.h>

// ============================================================
//  Reserviert (Protokoll) — NICHT als Telemetrie verwenden
// ============================================================
#define TK_TOKEN          "K"     // ThingsBoard Access Token (immer zuerst)
#define TK_RSSI           "RS"    // RSSI erster Hop  (vom Router angehaengt)
#define TK_SNR            "SN"    // SNR  erster Hop  (vom Router angehaengt)

// ============================================================
//  Telemetrie
// ============================================================
#define TK_TEMPERATURE    "T"     // °C
#define TK_PRESSURE       "P"     // hPa
#define TK_HUMIDITY       "H"     // %
#define TK_GAS            "G"     // kOhm
#define TK_BATT_VOLTAGE   "BV"    // V
#define TK_BATT_CHARGING  "BC"    // 0/1   (HW2 Ladestatus)
#define TK_WIND_SPEED     "WS"    // m/s
#define TK_WIND_GUST      "WG"    // m/s
#define TK_WIND_DIR       "WD"    // °
#define TK_RAIN           "R"     // mm

// ============================================================
//  Uebersetzungstabelle (Gateway)
// ============================================================
struct TelemetryKeyMap
{
    const char* shortKey;
    const char* tbKey;
};

static const TelemetryKeyMap TELEMETRY_KEYS[] =
{
    //  Kurz               ThingsBoard
    { TK_TEMPERATURE,   "Temperature"      },
    { TK_PRESSURE,      "Pressure"         },
    { TK_HUMIDITY,      "Humidity"         },
    { TK_GAS,           "Gas_Resistance"   },
    { TK_BATT_VOLTAGE,  "Battery_Voltage"  },
    { TK_BATT_CHARGING, "Battery_Charging" },
    { TK_WIND_SPEED,    "Wind_Speed"       },
    { TK_WIND_GUST,     "Wind_Gust"        },
    { TK_WIND_DIR,      "Wind_Direction"   },
    { TK_RAIN,          "Rain"             },
};

static constexpr size_t TELEMETRY_KEYS_COUNT =
    sizeof(TELEMETRY_KEYS) / sizeof(TELEMETRY_KEYS[0]);

// ============================================================
//  Kurzkey → ThingsBoard-Key  (nullptr = unbekannt)
// ============================================================
static inline const char* tk_toTbKey(const char* shortKey)
{
    for (size_t i = 0; i < TELEMETRY_KEYS_COUNT; i++)
        if (strcmp(TELEMETRY_KEYS[i].shortKey, shortKey) == 0)
            return TELEMETRY_KEYS[i].tbKey;
    return nullptr;
}
