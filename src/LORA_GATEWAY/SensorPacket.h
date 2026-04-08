// ============================================================
//  SensorPacket.h
// ============================================================
#pragma once

#include <Arduino.h>

struct SensorPacket
{
    char    sender[64];
    char    token[64];
    float   temperature;
    float   pressure;
    float   humidity;
    float   gasResistance;
    float   batteryVoltage;
    bool    valid;

    SensorPacket();
};

SensorPacket parseSensorPacket(const String& sender, const String& payload);
