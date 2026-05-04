// ============================================================
//  SensorPacket.cpp
// ============================================================
#include "SensorPacket.h"

// ── Konstruktor ──────────────────────────────────────────────
SensorPacket::SensorPacket()
    : temperature(0.0f),
      pressure(0.0f),
      humidity(0.0f),
      gasResistance(0.0f),
      batteryVoltage(0.0f),
      valid(false)
{
    memset(sender, 0, sizeof(sender));
    memset(token,  0, sizeof(token));
}

// ── Hilfsfunktion: Wert aus Key:Value String extrahieren ─────
static String extractValue(const String& payload, const String& key)
{
    String search = key + ":";
    int startIndex = payload.indexOf(search);

    if (startIndex == -1) return "";

    startIndex += search.length();
    int endIndex = payload.indexOf(";", startIndex);

    if (endIndex == -1)
        return payload.substring(startIndex);
    else
        return payload.substring(startIndex, endIndex);
}

// ── Haupt Parser Funktion ────────────────────────────────────
SensorPacket parseSensorPacket(const String& sender, const String& payload)
{
    SensorPacket p;

    // Sender befüllen
    strlcpy(p.sender, sender.c_str(), sizeof(p.sender));

    // Token extrahieren
    String token = extractValue(payload, "Token");
    if (token.isEmpty()) return p;
    strlcpy(p.token, token.c_str(), sizeof(p.token));

    // Sensorwerte extrahieren
    String tempStr = extractValue(payload, "Temperature");
    String presStr = extractValue(payload, "Pressure");
    String humStr  = extractValue(payload, "Humidity");
    String gasStr  = extractValue(payload, "Gas_Resistance");
    String batStr  = extractValue(payload, "Battery_Voltage");

    // Pflichtfeld prüfen
    if (tempStr.isEmpty()) return p;

    // Konvertieren
    p.temperature    = tempStr.toFloat();
    p.pressure       = presStr.toFloat();
    p.humidity       = humStr.toFloat();
    p.gasResistance  = gasStr.toFloat();
    p.batteryVoltage = batStr.toFloat();

    p.valid = true;
    return p;
}
