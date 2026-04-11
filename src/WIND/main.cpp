/*
 * ============================================================
 *  CAI_MINI — WIND
 * ============================================================
 *  Beschreibung : Verbindet sich per WLAN und sendet
 *                 Sensordaten (BME680) via MQTT / ThingsBoard
 *                 (WIND Inklusive, das gerät ist permanent mit 
 *                 Strom versorgt, daher wird es nicht heruntergefahren)
 *  Board        : Seeed XIAO ESP32-S3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 * ============================================================
 */

#include <Arduino.h>
#include "config_WIND.h"

uint16_t cnt = 0;


float calculate_wind_direction_deg(int sensorValue) {
    const uint8_t  N_POINTS = 16;
    const float    deg[N_POINTS]     = { 
        0.0f, 22.5f,  45.0f,  67.5f,
        90.0f, 112.5f, 135.0f, 157.5f,
        180.0f, 202.5f, 225.0f, 247.5f,
        270.0f, 292.5f, 315.0f, 337.5f };

    const uint16_t adcVals[N_POINTS] = 
        { 2937, 1464, 1682,  274,
        312,  210,  645,  431,
        1021,  861, 2322, 2201,
        3782, 3125, 3435, 2603 };

    

    const uint16_t tolerance = 20;

    for (uint8_t i = 0; i < N_POINTS; i++) {
        if ((uint16_t)sensorValue <= adcVals[i] + tolerance &&
            (uint16_t)sensorValue >= adcVals[i] - tolerance) {
            return deg[i];
        }
    }
    return -1.0f; // kein passender Wert gefunden
}

void setup()
{
    Serial.begin(115200);
    delay(5000); // Warte 5 Sekunden für Serial Debugging

    Serial.println("CAI_MINI WIND gestartet.");

    pinMode(WIND_VANE, INPUT);
    pinMode(WIND_SPEED, INPUT_PULLDOWN); 
    pinMode(RAIN_GAUGE, INPUT_PULLDOWN);


    pinMode(LED_BLUE, OUTPUT);
    pinMode(LED_ORANGE, OUTPUT);
    digitalWrite(LED_BLUE, LOW);
    digitalWrite(LED_ORANGE, HIGH);
}

void loop()
{
    // Hier würden die Sensorwerte gelesen und per LoRa gesendet werden



    if(digitalRead(WIND_SPEED) == HIGH) {
    
        digitalWrite(LED_BLUE, HIGH);
    } else {
        digitalWrite(LED_BLUE, LOW);
    }

    if(digitalRead(RAIN_GAUGE) == HIGH) {
        digitalWrite(LED_ORANGE, LOW);
    } else {
        digitalWrite(LED_ORANGE, HIGH);
    }


    cnt++;
    if(cnt >=1000)
    {
        cnt = 0;
        Serial.println("Wind Vane deg: " + String(calculate_wind_direction_deg(analogRead(WIND_VANE))));
        Serial.println("Wind VANE: " + String(analogRead(WIND_VANE)));
    }

    delay(1); // Warte 5 Sekunden bis zum nächsten Lesen/Senden
}