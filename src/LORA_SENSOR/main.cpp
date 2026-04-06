#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "LORA_config.h"
#include "config_LORA_SENSOR.h"

// ─────────────────────────────────────────────
//  SPI Mutex
// ─────────────────────────────────────────────
static SemaphoreHandle_t spiMutex = nullptr;

// ─────────────────────────────────────────────
//  RadioLib Objekte
// ─────────────────────────────────────────────
static SPIClass loraSPI(HSPI);
static SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);

static SX1262 radio = new Module(
    LORA_CS,
    LORA_DIO1,
    LORA_RESET,
    LORA_BUSY,
    loraSPI,
    spiSettings
);

// ─────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────
static bool loraInit() {
    loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    Serial.println("[LORA] SPI OK");

    xSemaphoreTake(spiMutex, portMAX_DELAY);
    int state = radio.begin(
        LORA_FREQ,
        LORA_BW,
        LORA_SF,
        LORA_CR,
        LORA_SYNC_WORD,
        LORA_POWER,
        LORA_PREAMBLE
    );
    xSemaphoreGive(spiMutex);

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA] begin() FEHLER: %d\n", state);
        return false;
    }
    Serial.println("[LORA] begin() OK");
    return true;
}

// ─────────────────────────────────────────────
//  TX Task
// ─────────────────────────────────────────────
static void txTask(void* param) {
    Serial.printf("[LORA] TX Task laeuft auf Core %d\n", xPortGetCoreID());
    int counter = 1;

    for (;;) {
        char payload[64];
        snprintf(payload, sizeof(payload), "SENSOR01|Sensorwert %d", counter);

        Serial.printf("[LORA] TX: %d Bytes: \"%s\"\n", strlen(payload), payload);
        Serial.println("");
        xSemaphoreTake(spiMutex, portMAX_DELAY);
        int state = radio.transmit(payload);
        xSemaphoreGive(spiMutex);

        if (state == RADIOLIB_ERR_NONE) {
            Serial.printf("[SENSOR] #%d gesendet: %s\n", counter, payload);
            Serial.println("");
        } else {
            Serial.printf("[LORA] TX FEHLER: %d\n", state);
            Serial.println("");
        }

        counter++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ─────────────────────────────────────────────
//  setup / loop
// ─────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("[LORA] ==================== BEGIN ====================");
    Serial.println("[LORA] Modus  : SENSOR");
    Serial.printf("[LORA] Core   : %d\n", xPortGetCoreID());

    // Mutex erstellen — VOR allem anderen!
    spiMutex = xSemaphoreCreateMutex();
    configASSERT(spiMutex);

    if (!loraInit()) {
        Serial.println("[LORA] INIT FEHLGESCHLAGEN — Neustart in 5s");
        delay(5000);
        ESP.restart();
    }

    // TX Task auf Core 0, Stack 4096
    xTaskCreatePinnedToCore(
        txTask,
        "LoRa_TX",
        4096,
        nullptr,
        2,
        nullptr,
        0           // Core 0
    );

    Serial.println("[LORA] ==================== BEREIT ====================");
    Serial.println("[SENSOR] Sende alle 5 Sekunden...");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
