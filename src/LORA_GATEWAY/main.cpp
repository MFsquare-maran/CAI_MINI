/*
 * ============================================================
 *  CAI_MINI — LoRa Gateway
 * ============================================================
 *  Beschreibung : Empfängt LoRa Pakete von Nodes/Routern
 *                 und leitet Daten weiter (z.B. WLAN / MQTT)
 *  Board        : Seeed XIAO ESP32-S3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 * ============================================================
 */

 #include <Arduino.h>

void setup()
{
    Serial.begin(115200);
    delay(5000); // Warte 5 Sekunden für Serial Debugging

    Serial.println("CAI_MINI LoRa Gateway gestartet.");
}

void loop()
{
    // Hier würden die Sensorwerte gelesen und per LoRa gesendet werden
    Serial.println("Sensorwerte lesen und senden...");

    delay(5000); // Warte 5 Sekunden bis zum nächsten Lesen/Senden
}