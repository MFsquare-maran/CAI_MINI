#include "LORA.h"

#if defined(ARDUINO_ARCH_ESP32) && !defined(VSPI)
  #define VSPI FSPI
#endif

// ─────────────────────────────────────────────────────────────
//  Konstruktor / Destruktor
// ─────────────────────────────────────────────────────────────

LORA::LORA(LoraMode mode, const LoraConfig& config)
    : _mode(mode), _config(config) {}

LORA::~LORA() {
    if (_rxTask)     { vTaskDelete(_rxTask);          _rxTask     = nullptr; }
    if (_rxQueue)    { vQueueDelete(_rxQueue);        _rxQueue    = nullptr; }
    if (_radioMutex) { vSemaphoreDelete(_radioMutex); _radioMutex = nullptr; }
    if (_radio)      { delete _radio;                 _radio      = nullptr; }
    if (_spi)        { _spi->end(); delete _spi;      _spi        = nullptr; }
}

// ─────────────────────────────────────────────────────────────
//  begin()
// ─────────────────────────────────────────────────────────────

bool LORA::begin() {
    const char* modeStr =
        _mode == LoraMode::GATEWAY ? "GATEWAY" :
        _mode == LoraMode::ROUTER  ? "ROUTER"  : "SENSOR";

    Serial.println("[LORA] ==================== BEGIN ====================");
    Serial.printf ("[LORA] Modus       : %s\n",  modeStr);
    Serial.printf ("[LORA] Core        : %d\n",  LORA_TASK_CORE);
    Serial.printf ("[LORA] RX Task     : %s\n",  LORA_HAS_RX_TASK ? "JA" : "NEIN (nur TX)");
    Serial.println("[LORA] --- SPI Pins ---");
    Serial.printf ("[LORA]   SCK=%d  MISO=%d  MOSI=%d  CS=%d\n",
        _config.pinSck, _config.pinMiso, _config.pinMosi, _config.pinCs);
    Serial.println("[LORA] --- LoRa Control Pins ---");
    Serial.printf ("[LORA]   DIO1=%d  RESET=%d  BUSY=%d\n",
        _config.pinDio1, _config.pinReset, _config.pinBusy);
    Serial.println("[LORA] --- RF Parameter ---");
    Serial.printf ("[LORA]   Freq=%.4f MHz  BW=%.1f kHz  SF=%d  CR=4/%d\n",
        _config.frequency, _config.bandwidth,
        _config.spreadFactor, _config.codingRate);
    Serial.printf ("[LORA]   SyncWord=0x%02X  Power=%d dBm\n",
        _config.syncWord, _config.power);
    Serial.println("[LORA] =================================================");

    // ── Pin Validierung ──────────────────────────────────────
    if (_config.pinSck < 0 || _config.pinMiso < 0 ||
        _config.pinMosi < 0 || _config.pinCs < 0) {
        Serial.println("[LORA] FATAL: SPI Pins nicht gesetzt (-1 gefunden)!");
        return false;
    }
    if (_config.pinDio1 < 0 || _config.pinReset < 0 || _config.pinBusy < 0) {
        Serial.println("[LORA] FATAL: LoRa Control Pins nicht gesetzt!");
        return false;
    }

    // ── SPI ──────────────────────────────────────────────────
    Serial.println("[LORA] SPI init...");
    _spi = new SPIClass(VSPI);
    _spi->begin(_config.pinSck, _config.pinMiso, _config.pinMosi, _config.pinCs);
    delay(10);
    Serial.println("[LORA] SPI OK");

    // ── FreeRTOS ─────────────────────────────────────────────
    _rxQueue    = xQueueCreate(QUEUE_SIZE, sizeof(LoraPacket));
    _radioMutex = xSemaphoreCreateMutex();
    if (!_rxQueue || !_radioMutex) {
        Serial.println("[LORA] FATAL: Queue/Mutex fehlgeschlagen!");
        return false;
    }
    Serial.println("[LORA] FreeRTOS OK");

    // ── RadioLib ─────────────────────────────────────────────
    Serial.println("[LORA] RadioLib Module erstellen...");
    _radio = new SX1262(
        new Module(_config.pinCs, _config.pinDio1,
                   _config.pinReset, _config.pinBusy, *_spi)
    );

    Serial.println("[LORA] RadioLib begin()...");
    int16_t state = _radio->begin(
        _config.frequency,
        _config.bandwidth,
        _config.spreadFactor,
        _config.codingRate,
        _config.syncWord,
        _config.power
    );

    Serial.printf("[LORA] begin() → State: %d ", state);
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("(OK)");
    } else {
        Serial.println("(FEHLER!)");
        // Fehlercodes erklären
        switch (state) {
            case -2:   Serial.println("[LORA] Code -2   : SPI Kommunikation fehlgeschlagen!");
                       Serial.println("[LORA]            → Pins in platformio.ini / pin_config prüfen!"); break;
            case -7:   Serial.println("[LORA] Code -7   : Ungültige Frequenz!"); break;
            case -8:   Serial.println("[LORA] Code -8   : Ungültige Bandwidth!"); break;
            case -9:   Serial.println("[LORA] Code -9   : Ungültiger SpreadFactor!"); break;
            case -706: Serial.println("[LORA] Code -706 : Chip nicht gefunden!");
                       Serial.println("[LORA]            → SPI und RESET Pin prüfen!"); break;
            default:   Serial.printf ("[LORA] Unbekannter Code: %d\n", state); break;
        }
        return false;
    }

    // ── Chip Verifikation via Standby→Frequency Check ────────
    if (!_verifyChip()) {
        Serial.println("[LORA] FATAL: Chip-Verifikation fehlgeschlagen!");
        return false;
    }

    _ready = true;

    // ── RX Task (nur Gateway / Router) ───────────────────────
#if LORA_HAS_RX_TASK
    Serial.println("[LORA] startReceive()...");
    state = _radio->startReceive();
    Serial.printf("[LORA] startReceive() → State: %d %s\n",
        state, state == RADIOLIB_ERR_NONE ? "(OK)" : "(FEHLER!)");

    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("[LORA] FATAL: startReceive() fehlgeschlagen!");
        return false;
    }

    Serial.printf("[LORA] RX Task starten auf Core %d...\n", LORA_TASK_CORE);
    BaseType_t taskRes = xTaskCreatePinnedToCore(
        _rxTaskWrapper, "LoRa_RX", TASK_STACK,
        this, TASK_PRIORITY, &_rxTask, LORA_TASK_CORE
    );
    if (taskRes != pdPASS) {
        Serial.println("[LORA] FATAL: RX Task konnte nicht erstellt werden!");
        return false;
    }
    Serial.println("[LORA] RX Task läuft.");
#else
    Serial.println("[LORA] Sensor-Modus: TX only — kein RX Task.");
#endif

    Serial.println("[LORA] ==================== BEREIT ====================");
    return true;
}

// ─────────────────────────────────────────────────────────────
//  _verifyChip()
//  getStatus() ist protected → alternativer Weg:
//  Wir lesen die eingestellte Frequenz zurück und prüfen ob
//  der Chip antwortet (getFrequency() spricht SPI an)
// ─────────────────────────────────────────────────────────────

bool LORA::_verifyChip() {
    Serial.println("[LORA] --- Chip Verifikation ---");

    // Standby aufrufen — state 0 = Chip antwortet auf SPI
    int16_t state = _radio->standby();
    Serial.printf("[LORA]   standby() → State: %d %s\n",
        state, state == RADIOLIB_ERR_NONE ? "(OK)" : "(FEHLER → SPI/Pins prüfen!)");

    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("[LORA]   FEHLER: Chip antwortet nicht!");
        Serial.println("[LORA]   → SCK/MISO/MOSI/CS/RESET Pins prüfen!");
        return false;
    }

    // getFrequencyDeviation gibt Frequenz zurück — zeigt ob SPI korrekt schreibt/liest
    // (setFrequency schreibt Register, danach begin() hat diese schon gesetzt)
    // Wir testen mit einem expliziten set+read des TCXO Wertes
    // Einfachster Test: nochmal setFrequency aufrufen — state 0 = Chip OK
    state = _radio->setFrequency(_config.frequency);
    Serial.printf("[LORA]   setFrequency(%.4f) → State: %d %s\n",
        _config.frequency,
        state, state == RADIOLIB_ERR_NONE ? "(OK)" : "(FEHLER)");

    if (state != RADIOLIB_ERR_NONE) {
        return false;
    }

    Serial.println("[LORA]   Chip Verifikation OK");
    return true;
}

// ─────────────────────────────────────────────────────────────
//  _verifyRxMode() — nur noch via State-Check
// ─────────────────────────────────────────────────────────────

bool LORA::_verifyRxMode() {
    // Ohne getStatus() prüfen wir einfach ob startReceive() State 0 zurückgibt
    // Das wird direkt in begin() gemacht — hier als Stub falls nötig
    return true;
}

// ─────────────────────────────────────────────────────────────
//  transmit()
// ─────────────────────────────────────────────────────────────

bool LORA::transmit(const uint8_t* data, uint8_t length) {
    if (!_ready) {
        Serial.println("[LORA] TX: Abbruch — nicht bereit!");
        return false;
    }

    Serial.printf("[LORA] TX: %d Bytes: \"%.*s\"\n",
        length, length, reinterpret_cast<const char*>(data));

    if (xSemaphoreTake(_radioMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        Serial.println("[LORA] TX: ERROR — Mutex Timeout nach 2000ms!");
        return false;
    }

    int16_t state = _radio->transmit(
        reinterpret_cast<const char*>(data), length
    );
    Serial.printf("[LORA] TX: transmit() → State: %d %s\n",
        state, state == RADIOLIB_ERR_NONE ? "(OK)" : "(FEHLER)");

#if LORA_HAS_RX_TASK
    int16_t rxState = _radio->startReceive();
    Serial.printf("[LORA] TX: startReceive() → State: %d\n", rxState);
#endif

    xSemaphoreGive(_radioMutex);

    return state == RADIOLIB_ERR_NONE;
}

bool LORA::transmit(const String& text) {
    return transmit(
        reinterpret_cast<const uint8_t*>(text.c_str()),
        static_cast<uint8_t>(text.length())
    );
}

// ─────────────────────────────────────────────────────────────
//  hasPacket() / readPacket()
// ─────────────────────────────────────────────────────────────

bool LORA::hasPacket() {
    if (!_rxQueue) return false;
    return uxQueueMessagesWaiting(_rxQueue) > 0;
}

bool LORA::readPacket(LoraPacket& packet) {
    if (!_rxQueue) return false;
    return xQueueReceive(_rxQueue, &packet, 0) == pdTRUE;
}

// ─────────────────────────────────────────────────────────────
//  RX Task — nur bei Gateway / Router
// ─────────────────────────────────────────────────────────────

#if LORA_HAS_RX_TASK

void LORA::_rxTaskWrapper(void* param) {
    static_cast<LORA*>(param)->_rxLoop();
}

void LORA::_rxLoop() {
    Serial.printf("[LORA] RX Task gestartet auf Core %d\n", xPortGetCoreID());

    if (xSemaphoreTake(_radioMutex, portMAX_DELAY) == pdTRUE) {
        int16_t state = _radio->startReceive();
        Serial.printf("[LORA] RX Task: startReceive() → State=%d %s\n",
            state, state == RADIOLIB_ERR_NONE ? "(OK)" : "(FEHLER)");
        xSemaphoreGive(_radioMutex);
    }

    uint32_t loopCount  = 0;
    uint32_t lastReport = millis();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));
        loopCount++;

        if (!_ready) continue;

        // ── Alle 10s Lebenszeichen ────────────────────────────
        if (millis() - lastReport >= 10000) {
            lastReport = millis();
            Serial.printf("[LORA] RX alive | Loop:%lu | Queue:%d | RSSI:%.1f\n",
                loopCount,
                (int)uxQueueMessagesWaiting(_rxQueue),
                _lastRssi
            );
        }

        // ── available() pollen ───────────────────────────────
        bool avail = false;
        if (xSemaphoreTake(_radioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            avail = _radio->available();
            xSemaphoreGive(_radioMutex);
        } else {
            continue;
        }

        if (!avail) continue;

        // ── Paket lesen ──────────────────────────────────────
        Serial.println("[LORA] RX: *** Paket erkannt! ***");
        LoraPacket pkt;
        memset(&pkt, 0, sizeof(pkt));

        if (xSemaphoreTake(_radioMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            pkt.length = _radio->getPacketLength();
            Serial.printf("[LORA] RX: Länge=%d\n", pkt.length);

            if (pkt.length == 0 || pkt.length > sizeof(pkt.data)) {
                Serial.printf("[LORA] RX: Ungültige Länge %d → verwerfen\n", pkt.length);
                _radio->startReceive();
                xSemaphoreGive(_radioMutex);
                continue;
            }

            int16_t state = _radio->readData(pkt.data, pkt.length);
            Serial.printf("[LORA] RX: readData() → State=%d %s\n",
                state, state == RADIOLIB_ERR_NONE ? "(OK)" : "(FEHLER)");

            if (state == RADIOLIB_ERR_NONE) {
                pkt.rssi  = _radio->getRSSI();
                pkt.snr   = _radio->getSNR();
                _lastRssi = pkt.rssi;
                _lastSnr  = pkt.snr;
                Serial.printf("[LORA] RX: RSSI=%.1fdBm SNR=%.1fdB \"%.*s\"\n",
                    pkt.rssi, pkt.snr, pkt.length, pkt.data);
            } else {
                if (state == -2)  Serial.println("[LORA] RX: CRC Fehler!");
                if (state == -10) Serial.println("[LORA] RX: Preamble Timeout!");
            }

            _radio->startReceive();
            xSemaphoreGive(_radioMutex);

            if (state == RADIOLIB_ERR_NONE) {
                if (xQueueSend(_rxQueue, &pkt, 0) != pdTRUE) {
                    LoraPacket discard;
                    xQueueReceive(_rxQueue, &discard, 0);
                    xQueueSend(_rxQueue, &pkt, 0);
                    Serial.println("[LORA] RX: Queue voll → ältestes verworfen");
                }
            }
        }
    }
}

#else
void LORA::_rxTaskWrapper(void*) {}
void LORA::_rxLoop() {}
#endif
