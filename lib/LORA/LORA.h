#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// ─────────────────────────────────────────────────────────────
//  Core Zuweisung je Gerät
//  GATEWAY : loop() = Core 1 | WiFi = Core 0 | LoRa RX = Core 1
//  ROUTER  : loop() = Core 1 | LoRa RX = Core 0
//  SENSOR  : loop() = Core 1 | kein RX Task nötig
// ─────────────────────────────────────────────────────────────
#if defined(DEVICE_GATEWAY)
  #define LORA_TASK_CORE   1
  #define LORA_HAS_RX_TASK 1    // Gateway empfängt
#elif defined(DEVICE_ROUTER)
  #define LORA_TASK_CORE   0
  #define LORA_HAS_RX_TASK 1    // Router empfängt und leitet weiter
#elif defined(DEVICE_SENSOR)
  #define LORA_TASK_CORE   0
  #define LORA_HAS_RX_TASK 0    // ← Sensor sendet NUR — kein RX Task!
#else
  #error "Kein DEVICE_* Define gesetzt!"
#endif

// ─────────────────────────────────────────────────────────────
//  Konstanten
// ─────────────────────────────────────────────────────────────
#define QUEUE_SIZE      10
#define TASK_STACK      4096
#define TASK_PRIORITY   2
#define MAX_PACKET_SIZE 255

// ─────────────────────────────────────────────────────────────
//  LoraMode
// ─────────────────────────────────────────────────────────────
enum class LoraMode {
    GATEWAY,
    ROUTER,
    SENSOR
};

// ─────────────────────────────────────────────────────────────
//  LoraConfig — RF Parameter + Pins
// ─────────────────────────────────────────────────────────────
struct LoraConfig {
    // SPI
    int8_t pinSck   = -1;
    int8_t pinMiso  = -1;
    int8_t pinMosi  = -1;
    // LoRa Control
    int8_t pinCs    = -1;
    int8_t pinDio1  = -1;
    int8_t pinReset = -1;
    int8_t pinBusy  = -1;
    // RF Parameter — MÜSSEN auf beiden Seiten gleich sein!
    float   frequency    = 868.1f;
    float   bandwidth    = 125.0f;
    uint8_t spreadFactor = 7;
    uint8_t codingRate   = 5;
    uint8_t syncWord     = 0x12;
    int8_t  power        = 14;
};

// ─────────────────────────────────────────────────────────────
//  LoraPacket
// ─────────────────────────────────────────────────────────────
struct LoraPacket {
    uint8_t  data[MAX_PACKET_SIZE];
    uint8_t  length = 0;
    float    rssi   = 0.0f;
    float    snr    = 0.0f;
};

// ─────────────────────────────────────────────────────────────
//  LORA Klasse
// ─────────────────────────────────────────────────────────────
class LORA {
public:
    LORA(LoraMode mode, const LoraConfig& config);
    ~LORA();

    bool begin();
    bool transmit(const uint8_t* data, uint8_t length);
    bool transmit(const String& text);
    bool hasPacket();
    bool readPacket(LoraPacket& packet);

    float getLastRssi() const { return _lastRssi; }
    float getLastSnr()  const { return _lastSnr;  }
    bool  isReady()     const { return _ready;     }

private:
    LoraMode    _mode;
    LoraConfig  _config;
    SPIClass*   _spi        = nullptr;
    SX1262*     _radio      = nullptr;
    TaskHandle_t    _rxTask     = nullptr;
    QueueHandle_t   _rxQueue    = nullptr;
    SemaphoreHandle_t _radioMutex = nullptr;
    volatile bool _ready    = false;
    float _lastRssi         = 0.0f;
    float _lastSnr          = 0.0f;

    bool _verifyChip();       // ← NEU: Chip-Kommunikation testen
    bool _verifyRxMode();     // ← NEU: RX-Modus verifizieren

    static void _rxTaskWrapper(void* param);
    void _rxLoop();
};
