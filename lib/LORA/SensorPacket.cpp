#include "SensorPacket.h"

SensorPacket::SensorPacket()
    : temperature(0.0f),
      pressure(0.0f),
      humidity(0.0f),
      gasResistance(0.0f),
      batteryVoltage(0.0f),
      rssi(-1.0f),       // ← -1.0f = kein Wert
      valid(false)
{
    memset(sender, 0, sizeof(sender));
    memset(token,  0, sizeof(token));
}

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

SensorPacket parseSensorPacket(const String& sender, const String& payload)
{
    SensorPacket p;

    strlcpy(p.sender, sender.c_str(), sizeof(p.sender));

    String token = extractValue(payload, "Token");
    if (token.isEmpty()) return p;
    strlcpy(p.token, token.c_str(), sizeof(p.token));

    String tempStr = extractValue(payload, "Temperature");
    String presStr = extractValue(payload, "Pressure");
    String humStr  = extractValue(payload, "Humidity");
    String gasStr  = extractValue(payload, "Gas_Resistance");
    String batStr  = extractValue(payload, "Battery_Voltage");
    String rssiStr = extractValue(payload, "RSSI");  // ← neu

    if (tempStr.isEmpty()) return p;

    p.temperature    = tempStr.toFloat();
    p.pressure       = presStr.toFloat();
    p.humidity       = humStr.toFloat();
    p.gasResistance  = gasStr.toFloat();
    p.batteryVoltage = batStr.toFloat();

    // RSSI nur setzen wenn vorhanden
    if (!rssiStr.isEmpty())
        p.rssi = rssiStr.toFloat();

    p.valid = true;
    return p;
}