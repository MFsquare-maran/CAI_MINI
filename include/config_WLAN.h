/*
 * ============================================================
 *  CAI_MINI — WLAN
 * ============================================================
 *  Beschreibung : Verbindet sich per WLAN und sendet Sensordaten (BME680) via MQTT / ThingsBoard
 *  Board        : Seeed XIAO ESP32-S3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 * ============================================================
 */
#define TIMER_LIMIT_SEC 60


#if HW_VERSION == 1

    #define FW_VERSION "1.1.5_WLAN_HW1" // Firmware-Version

#elif HW_VERSION == 2

    #define CYCLE_TIME_MIN 10 // 10 Minuten
    #define FW_VERSION "1.1.5_WLAN_HW2" // Firmware-Version

#endif



