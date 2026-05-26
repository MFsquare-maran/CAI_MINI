
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




#ifdef HELTEC_WSL_V3

#define LED_BOARD 35



#define VBAT_PIN     1
#define ADC_CTRL_PIN 37


#define LORA_DIO1    14
#define LORA_RESET   12
#define LORA_BUSY    13

#define LORA_SCK      9
#define LORA_MISO    11
#define LORA_MOSI    10
#define LORA_NSS      8


#define ON HIGH
#define OFF LOW


#endif

#ifdef SEED_XIAO_ESP32S3

#define LED_BOARD 21



#define LORA_DIO1   39
#define LORA_RESET  42
#define LORA_BUSY   40

#define LORA_SCK     7
#define LORA_MISO    8
#define LORA_MOSI    9
#define LORA_NSS      41


#define ON LOW
#define OFF HIGH

#endif


#define gateway_send_interval 10 // 10 minutes



#define FW_VERSION "1.1.8_GATEWAY" // Firmware-Version


