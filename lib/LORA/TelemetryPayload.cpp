// ============================================================
//  TelemetryPayload.cpp
//  LoRa-Telemetrieformat CAI_MINI
// ============================================================

#include "TelemetryPayload.h"
#include <math.h>
#include <ctype.h>

// ============================================================
//  Reservierte Keys
// ============================================================
static const char* const TP_RESERVED[] = { TK_TOKEN, TK_RSSI, TK_SNR };

static bool isReserved(const char* key)
{
    for (const char* r : TP_RESERVED)
        if (strcmp(key, r) == 0) return true;
    return false;
}

// ============================================================
//  Hilfsfunktionen
// ============================================================
bool tp_isValidKey(const char* key)
{
    if (!key) return false;

    size_t len = strlen(key);
    if (len == 0 || len > TP_MAX_KEY_LEN) return false;

    for (size_t i = 0; i < len; i++)
    {
        char c = key[i];
        if (!(isalnum((unsigned char)c) || c == '_')) return false;
    }
    return !isReserved(key);
}

bool tp_parseNumber(const char* s, double& out)
{
    if (!s || *s == '\0') return false;

    char*  end = nullptr;
    double v   = strtod(s, &end);

    if (end == s || *end != '\0' || !isfinite(v)) return false;

    out = v;
    return true;
}

// ============================================================
//  TelemetryPayload — Builder
// ============================================================
TelemetryPayload::TelemetryPayload()
    : m_len(0),
      m_error(false)
{
    m_buf[0] = '\0';
}

bool TelemetryPayload::append(const char* s)
{
    size_t n = strlen(s);
    if (m_len + n > TP_SEND_MAX) return false;

    memcpy(m_buf + m_len, s, n + 1);
    m_len += n;
    return true;
}

bool TelemetryPayload::begin(const char* token)
{
    m_len    = 0;
    m_buf[0] = '\0';
    m_error  = false;

    if (!token || token[0] == '\0')
    {
        logln("[TP] ❌ Kein Token!");
        m_error = true;
        return false;
    }

    if (!append(TK_TOKEN ":") || !append(token))
    {
        logln("[TP] ❌ Token passt nicht ins Paket!");
        m_error = true;
        return false;
    }
    return true;
}

bool TelemetryPayload::add(const char* key, float value, uint8_t decimals)
{
    if (!tp_isValidKey(key))
    {
        logln("[TP] ❌ Ungueltiger/reservierter Key: " + String(key ? key : "(null)"));
        m_error = true;
        return false;
    }

    if (!tk_toTbKey(key))
        logln("[TP] ⚠️ Key '" + String(key) + "' fehlt in TelemetryKeys.h");

    if (!isfinite(value))
    {
        logln("[TP] ⚠️ " + String(key) + " = NaN/Inf – Feld uebersprungen.");
        return false;
    }

    char num[24];
    int  w = snprintf(num, sizeof(num), "%.*f", decimals, (double)value);
    if (w <= 0 || w >= (int)sizeof(num))
    {
        logln("[TP] ❌ " + String(key) + ": Wert nicht formatierbar.");
        m_error = true;
        return false;
    }

    char piece[TP_MAX_KEY_LEN + sizeof(num) + 3];
    snprintf(piece, sizeof(piece), ";%s:%s", key, num);

    if (!append(piece))
    {
        logln("[TP] ❌ " + String(key) + " passt nicht mehr ins Paket (" +
              String(m_len) + "/" + String(TP_SEND_MAX) + ") – verworfen.");
        m_error = true;
        return false;
    }
    return true;
}

// ============================================================
//  tp_parse — Gateway
// ============================================================
bool tp_parse(const char* payload, TelemetryData& out)
{
    memset(&out, 0, sizeof(out));

    char buf[TP_RX_MAX + 1];
    if (strlcpy(buf, payload, sizeof(buf)) >= sizeof(buf))
    {
        logln("[TP] ❌ Payload zu lang – verworfen.");
        return false;
    }

    char* save = nullptr;
    for (char* field = strtok_r(buf, ";", &save);
         field != nullptr;
         field = strtok_r(nullptr, ";", &save))
    {
        char* sep = strchr(field, ':');
        if (!sep)
        {
            logln("[TP] ⚠️ Feld ohne ':' ignoriert: " + String(field));
            continue;
        }
        *sep = '\0';
        const char* key = field;
        const char* val = sep + 1;

        // ── Token ────────────────────────────────────────────
        if (strcmp(key, TK_TOKEN) == 0)
        {
            strlcpy(out.token, val, sizeof(out.token));
            continue;
        }

        if (*val == '\0') continue;

        double num;
        if (!tp_parseNumber(val, num))
        {
            logln("[TP] ⚠️ " + String(key) + ": keine Zahl ('" + String(val) + "') – ignoriert.");
            continue;
        }

        // ── Hop-Werte ────────────────────────────────────────
        if (strcmp(key, TK_RSSI) == 0) { out.hasRssi = true; out.rssi = (float)num; continue; }
        if (strcmp(key, TK_SNR)  == 0) { out.hasSnr  = true; out.snr  = (float)num; continue; }

        // ── Telemetrie ───────────────────────────────────────
        if (!tp_isValidKey(key))
        {
            logln("[TP] ⚠️ Ungueltiger Key ignoriert: " + String(key));
            continue;
        }
        if (out.count >= TP_MAX_FIELDS)
        {
            logln("[TP] ⚠️ Max. " + String(TP_MAX_FIELDS) + " Felder – " + String(key) + " ignoriert.");
            continue;
        }

        const char* tbKey = tk_toTbKey(key);
        if (!tbKey)
        {
            logln("[TP] ⚠️ Unbekannter Key '" + String(key) + "' – wird unveraendert weitergeleitet.");
            tbKey = key;
        }

        strlcpy(out.fields[out.count].key, tbKey, sizeof(out.fields[out.count].key));
        out.fields[out.count].value = num;
        out.count++;
    }

    if (out.token[0] == '\0')
    {
        logln("[TP] ❌ Kein Token im Paket.");
        return false;
    }
    return true;
}

const TelemetryField* tp_find(const TelemetryData& d, const char* tbKey)
{
    for (uint8_t i = 0; i < d.count; i++)
        if (strcmp(d.fields[i].key, tbKey) == 0) return &d.fields[i];
    return nullptr;
}

// ============================================================
//  tp_addHopInfo — Router
// ============================================================
String tp_addHopInfo(const String& payload, float rssi, float snr)
{
    String out;
    int    len   = payload.length();
    int    start = 0;

    while (start < len)
    {
        int end = payload.indexOf(';', start);
        if (end < 0) end = len;

        String field = payload.substring(start, end);
        start = end + 1;

        if (field.length() == 0) continue;

        int    c   = field.indexOf(':');
        String key = (c < 0) ? field : field.substring(0, c);
        String val = (c < 0) ? ""    : field.substring(c + 1);

        if (key == TK_RSSI)
        {
            if (val.length() > 0) return payload;   // erster Hop bereits eingetragen
            continue;
        }
        if (key == TK_SNR) continue;

        if (out.length() > 0) out += ';';
        out += field;
    }

    out += ";" TK_RSSI ":";
    out += String(rssi, 1);
    out += ";" TK_SNR ":";
    out += String(snr, 1);
    return out;
}
