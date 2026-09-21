
/*
 * ============================================================
 *  CAI_MINI — WIND
 * ============================================================
 *  Beschreibung : Verbindet sich per WLAN und sendet Sensordaten (BME680 + Wind/Rain) via MQTT / ThingsBoard
 *  Board        : Seeed XIAO ESP32-S3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 * ============================================================
 */

#if HW_VERSION == 1

    #define FW_VERSION "1.2.3_WIND_HW1" // Firmware-Version

#elif HW_VERSION == 2

    #define FW_VERSION "1.2.3_WIND_HW2" // Firmware-Version

#endif

