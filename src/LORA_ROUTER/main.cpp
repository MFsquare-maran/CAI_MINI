/*
 * ============================================================
 *  CAI_MINI — LoRa Router
 * ============================================================
 *  Beschreibung : Empfängt LoRa Pakete von Sensoren und
 *                 leitet diese an das Gateway weiter
 *  Board        : Seeed XIAO ESP32-S3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 * ============================================================
 */

#include <Arduino.h>
#include "config_LORA_ROUTER.h"

void setup()
{
    Serial.begin(115200);
    delay(5000); // Warte 5 Sekunden für Serial Debugging

    Serial.println("╔══════════════════════════════╗");
    Serial.println("║    CAI_MINI LoRa Route       ║");
    Serial.println("╚══════════════════════════════╝");
    Serial.print("Firmware Version: ");
    Serial.println(FW_VERSION);

    Serial.println("CAI_MINI LoRa Router gestartet.");
}

void loop()
{
    // Hier würden die Sensorwerte gelesen und per LoRa gesendet werden
    Serial.println("Sensorwerte lesen und senden...");

    delay(5000); // Warte 5 Sekunden bis zum nächsten Lesen/Senden
}