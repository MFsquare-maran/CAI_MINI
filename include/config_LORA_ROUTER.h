
/*
 * ============================================================
 *  CAI_MINI — LoRa Router
 * ============================================================
 *  Beschreibung : Empfängt LoRa Pakete von Sensoren und leitet sie weiter
 *                 (z.B. an Gateway oder andere Router)
 *  Board        : Seeed XIAO ESP32-S3
 *  Framework    : Arduino
 *  Autor        : maran
 *  Erstellt     : 2026-04-01
 * ============================================================
 */


#define BME_680_SCL 6
#define BME_680_SDA 5


#define LED_BLUE 2
#define LED_ORANGE 21

#define BATTERY_VOLTAGE 1


#define SD_CLK 13
#define SD_MISO 12
#define SD_MOSI 11
#define SD_CS 44




#define LORA_DIO1   39
#define LORA_RESET  42
#define LORA_BUSY   40

#define LORA_SCK     7
#define LORA_MISO    8
#define LORA_MOSI    9
#define LORA_NSS     41



#define FW_VERSION "1.0.5_ROUTER" // Firmware-Version


