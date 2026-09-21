
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

#define gateway_send_interval 10 // 10 minutes

 #pragma once

#if HW_VERSION == 1

    #define FW_VERSION "1.2.2_GATEWAY_HW1" // Firmware-Version

#elif HW_VERSION == 2

    #define FW_VERSION "1.2.2_GATEWAY_HW2" // Firmware-Version

#endif





