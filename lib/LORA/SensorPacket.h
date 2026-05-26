#pragma once
#include <Arduino.h>
#include "log.h"

struct SensorPacket
{
    char    sender[64];
    char    token[64];
    float   temperature;
    float   pressure;
    float   humidity;
    float   gasResistance;
    float   batteryVoltage;
    float   rssi;           // RSSI vom Router (via Payload), -1.0f = kein Wert
    float   gatewayRSSI;    // RSSI gemessen vom Gateway (Core 0, vor Queue)
    float   gatewaySNR;     // SNR gemessen vom Gateway  (Core 0, vor Queue)
    bool    valid;

    SensorPacket();
};

SensorPacket parseSensorPacket(const String& sender, const String& payload);