#pragma once

// ============================================================
//  TelemetryPayload.h
//  LoRa-Telemetrieformat CAI_MINI
//
//  Format (Klartext, vor Verschluesselung):
//      K:<token>;<Kurzkey>:<Wert>;<Kurzkey>:<Wert>;...
//
//  - K (Token) steht immer an erster Stelle
//  - Kurzkeys und ThingsBoard-Namen: TelemetryKeys.h
//  - Werte: nur Zahlen
//  - RS/SN haengt der erste Router als Paar an,
//    weitere Router lassen die Werte unveraendert.
//    Sensoren senden kein RS/SN.
//
//  Budget pro Paket (verschluesselt, IDs bis 10 Zeichen):
//    144 Zeichen Klartext, davon K:<token> = 22
//    → ca. 120 Zeichen fuer Telemetrie
//    Feld = 1 + Kurzkey + 1 + Wert
//    z.B. ";T:21.50" = 8 Zeichen
// ============================================================

#include <Arduino.h>
#include "TelemetryKeys.h"
#include "log.h"

// ============================================================
//  Limits
// ============================================================
#define TP_SEND_MAX      144   // Klartext-Puffer Sender
#define TP_RX_MAX        255   // Empfangspuffer Gateway
#define TP_MAX_KEY_LEN   31
#define TP_MAX_FIELDS    24    // max. Felder pro Paket (Gateway)

// ============================================================
//  Builder — Sensor / Router
//
//  TelemetryPayload tp;
//  tp.begin(token);
//  tp.add(TK_TEMPERATURE, temperature, 2);
//  Lora.transmit(ziel, tp.toString());
// ============================================================
class TelemetryPayload
{
public:
    TelemetryPayload();

    bool begin(const char* token);
    bool add(const char* key, float value, uint8_t decimals);

    bool        ok()       const { return !m_error; }
    size_t      length()   const { return m_len;    }
    const char* c_str()    const { return m_buf;    }
    String      toString() const { return String(m_buf); }

private:
    bool append(const char* s);

    char   m_buf[TP_SEND_MAX + 1];
    size_t m_len;
    bool   m_error;
};

// ============================================================
//  Parser — Gateway
//  Kurzkeys werden beim Parsen in ThingsBoard-Namen uebersetzt
// ============================================================
struct TelemetryField
{
    char   key[TP_MAX_KEY_LEN + 1];   // ThingsBoard-Name
    double value;
};

struct TelemetryData
{
    char           token[64];
    TelemetryField fields[TP_MAX_FIELDS];
    uint8_t        count;
    bool           hasRssi;
    float          rssi;
    bool           hasSnr;
    float          snr;
};

bool                  tp_parse(const char* payload, TelemetryData& out);
const TelemetryField* tp_find(const TelemetryData& d, const char* tbKey);

// ============================================================
//  Router — RS/SN anhaengen (nur erster Hop)
//  Gibt Payload unveraendert zurueck, wenn RS schon befuellt.
// ============================================================
String tp_addHopInfo(const String& payload, float rssi, float snr);

// ============================================================
//  Hilfsfunktionen
// ============================================================
bool tp_isValidKey(const char* key);
bool tp_parseNumber(const char* s, double& out);
