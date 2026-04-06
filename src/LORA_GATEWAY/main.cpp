#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "LORA_config.h"
#include "config_LORA_GATEWAY.h"

// ─────────────────────────────────────────────
//  SPI Mutex — schuetzt alle RadioLib-Zugriffe
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
//  RX Flag (von ISR gesetzt)
// ─────────────────────────────────────────────
static volatile bool rxFlag = false;

void IRAM_ATTR onReceive() {
    rxFlag = true;
}

// ─────────────────────────────────────────────
//  Hilfsfunktion: RadioLib init mit Mutex
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
    Serial.printf("[LORA] begin() OK\n");

    // DIO1 Interrupt registrieren
    radio.setDio1Action(onReceive);

    // Ersten RX starten
    xSemaphoreTake(spiMutex, portMAX_DELAY);
    state = radio.startReceive();
    xSemaphoreGive(spiMutex);

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA] startReceive() FEHLER: %d\n", state);
        return false;
    }
    Serial.println("[LORA] startReceive() OK");
    return true;
}

// ─────────────────────────────────────────────
//  RX Task
// ─────────────────────────────────────────────
static void rxTask(void* param) {
    Serial.printf("[LORA] RX Task laeuft auf Core %d\n", xPortGetCoreID());

    for (;;) {
        // Warte bis ISR das Flag setzt
        if (rxFlag) {
            rxFlag = false;

            String received = "";
            float rssi = 0.0f;
            float snr  = 0.0f;

            xSemaphoreTake(spiMutex, portMAX_DELAY);
            int state = radio.readData(received);
            if (state == RADIOLIB_ERR_NONE) {
                rssi = radio.getRSSI();
                snr  = radio.getSNR();
            }
            // Sofort wieder in RX
            radio.startReceive();
            xSemaphoreGive(spiMutex);

            if (state == RADIOLIB_ERR_NONE) {
                Serial.printf("[GATEWAY] Empfangen: \"%s\"  RSSI=%.1f dBm  SNR=%.1f dB\n",
                    received.c_str(), rssi, snr);
                    Serial.println("");
            } else {
                Serial.printf("[GATEWAY] readData() FEHLER: %d\n", state);
                Serial.println("");
            }
        }

        // Kurze Pause — gibt anderen Tasks CPU-Zeit
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ─────────────────────────────────────────────
//  setup / loop
// ─────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("[LORA] ==================== BEGIN ====================");
    Serial.println("[LORA] Modus  : GATEWAY");
    Serial.printf("[LORA] Core   : %d\n", xPortGetCoreID());

    // Mutex erstellen — VOR allem anderen!
    spiMutex = xSemaphoreCreateMutex();
    configASSERT(spiMutex);

    if (!loraInit()) {
        Serial.println("[LORA] INIT FEHLGESCHLAGEN — Neustart in 5s");
        delay(5000);
        ESP.restart();
    }

    // RX Task auf Core 1, Stack 4096
    xTaskCreatePinnedToCore(
        rxTask,
        "LoRa_RX",
        4096,
        nullptr,
        2,          // Prioritaet 2 — hoeher als loop()
        nullptr,
        1           // Core 1
    );

    Serial.println("[LORA] ==================== BEREIT ====================");
    Serial.println("[GATEWAY] Warte auf Pakete...");
}

void loop() {
    // loop() bleibt leer — alles laeuft im RX Task
    vTaskDelay(pdMS_TO_TICKS(1000));
}
