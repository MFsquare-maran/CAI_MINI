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

void setup()
{
    Serial.begin(115200);
    delay(5000); // Warte 5 Sekunden für Serial Debugging

    Serial.println("CAI_MINI WIND gestartet.");

    pinMode(WIND_VANE, INPUT);
    pinMode(WIND_SPEED, INPUT_PULLDOWN); 


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


    cnt++;
    if(cnt >=1000)
    {
        cnt = 0;
        Serial.println("Wind Vane: " + String(analogRead(WIND_VANE)));
    }

    delay(1); // Warte 5 Sekunden bis zum nächsten Lesen/Senden
}