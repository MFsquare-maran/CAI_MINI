#pragma once

// ============================================================
//  LORA.h
//  Beschreibung: LoRa Kommunikationsklasse fuer ESP32
//                Unterstuetzt: SENSOR, ROUTER, GATEWAY
//  Library:      RadioLib (SX1262)
//  Datum:        2026-04-06
// ============================================================



#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

// ============================================================
//  Konstanten (in config_LORA_*.h definieren oder hier)
// ============================================================
#ifndef LORA_FREQ
  #define LORA_FREQ      868.0f
#endif
#ifndef LORA_BW
  #define LORA_BW        125.0f
#endif
#ifndef LORA_SF
  #define LORA_SF        9
#endif
#ifndef LORA_CR
  #define LORA_CR        7
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  14
#endif
#ifndef SYNC_WORD
  #define SYNC_WORD      0xB6
#endif

#define LORA_PACKET_SEPARATOR  '|'
#define LORA_TYPE_DATA         "DATA"
#define LORA_TYPE_ACK          "ACK"
#define LORA_ACK_PAYLOAD       "received"

// ============================================================
//  Betriebsmodi
// ============================================================
enum class LoraMode
{
    SENSOR,
    ROUTER,
    GATEWAY
};

// ============================================================
//  LORA Klasse
// ============================================================
class LORA
{
public:
    // ── Konstruktor / Destruktor ──────────────────────────────
    LORA(const String& ownName,
         LoraMode       mode,
         int            nssPin,
         int            dio1Pin,
         int            resetPin,
         int            busyPin,
         SPIClass&      spi,
         int            sckPin,
         int            misoPin,
         int            mosiPin,
         const String&  targetName  = "",
         int            maxRetries  = 3,
         int            retryDelay  = 1000,
         int            ackTimeout  = 2000);

    ~LORA();

    // ── Initialisierung ───────────────────────────────────────
    bool begin();

    // ── Oeffentliche Methoden ─────────────────────────────────
    bool    transmit(const String& empfaenger, const String& daten);
    bool    packetReceived();

    String  readData();
    String  readRawPacket();
    String  readSender();

    void    setTargetName(const String& targetName);
    String  getModeString() const;

    uint32_t getSentCount()     const;
    uint32_t getReceivedCount() const;

    // ── Statische ISR (muss public sein fuer setDio1Action) ───
    static void IRAM_ATTR onDio1Interrupt();

private:
    // ── Interne Hilfsmethoden ─────────────────────────────────
    String  buildPacket(const String& empfaenger,
                        const String& typ,
                        const String& daten) const;

    bool    sendRaw(const String& packet);
    bool    waitForAck(const String& expectedSender);
    void    sendAck(const String& empfaenger);
    bool    isForMe(const String& packet) const;
    bool    parsePacket(const String& raw);
    String  tryReceive();
    String  splitGet(const String& str, int index) const;

    // ── Member-Variablen ──────────────────────────────────────
    String      m_ownName;
    String      m_targetName;
    LoraMode    m_mode;

    int         m_maxRetries;
    int         m_retryDelay;
    int         m_ackTimeout;

    bool        m_newPacket;
    uint32_t    m_sentCount;
    uint32_t    m_receivedCount;

    String      m_lastRawPacket;
    String      m_lastSender;
    String      m_lastData;

    SX1262*     m_radio;
    SPIClass*   m_spi;

    // ── Statisches Interrupt-Flag (geteilt ueber alle Instanzen) ──
    // Wird in der ISR gesetzt und in tryReceive() ausgewertet
    static volatile bool s_packetFlag;
};
