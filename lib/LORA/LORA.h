#pragma once

// ============================================================
//  LORA.h
//  Beschreibung: LoRa Kommunikationsklasse fuer ESP32
//                Unterstuetzt: SENSOR, ROUTER, GATEWAY
//  Library:      RadioLib (SX1262)
//  Datum:        2026-04-16
//  Fixes:        - s_packetFlag korrekt genutzt
//                - Deadlock-Fix in waitForAck()
//                - Deep Sleep / forcePacketFlag() Support
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "esp_sleep.h"
#include "log.h"

#include "aes_key.h" 

#ifdef LORA_ENCRYPTION_ENABLED
#include <AES.h>
#include <CBC.h>
#include <Base64.h>

#endif
// ============================================================
//  Konstanten
// ============================================================
#ifndef LORA_FREQ
  #define LORA_FREQ      868.0f
#endif
#ifndef LORA_BW
  #define LORA_BW        125.0f
#endif
#ifndef LORA_SF
  #define LORA_SF        12
#endif
#ifndef LORA_CR
  #define LORA_CR        7
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  22
#endif
#ifndef SYNC_WORD
  #define SYNC_WORD      0xB6
#endif

#define LORA_PACKET_SEPARATOR  '|'
#define LORA_TYPE_DATA         "DATA"
#define LORA_TYPE_ACK          "ACK"
#define LORA_ACK_PAYLOAD       "received"

// ============================================================
//  LORA Klasse
// ============================================================
class LORA
{
public:
    // ── Konstruktor / Destruktor ──────────────────────────────
    LORA(int            nssPin,
         int            dio1Pin,
         int            resetPin,
         int            busyPin,
         SPIClass&      spi,
         int            sckPin,
         int            misoPin,
         int            mosiPin,
         int            maxRetries  = 3,
         int            retryDelay  = 1000,
         int            ackTimeout  = 2000);

    ~LORA();

    // ── Initialisierung ───────────────────────────────────────
    bool begin(const String& ownName);
 

    // ── Oeffentliche Methoden ─────────────────────────────────
    bool    transmit(const String& empfaenger, const String& daten);
    bool    packetReceived();

    void sleepRadio();

    String  readData();
    String  readRawPacket();
    String  readSender();

    uint32_t getSentCount()     const;
    uint32_t getReceivedCount() const;

    float   getLastRSSI();
    float   getLastSNR();

    // ── Deep Sleep Support ────────────────────────────────────
    // Setzt s_packetFlag manuell (nach Wake-Up aus Deep Sleep)
    void    forcePacketFlag();



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


    #ifdef LORA_ENCRYPTION_ENABLED

    String  encrypt(const String& plaintext)  const;
    String  decrypt(const String& ciphertext) const;

    #endif

    // ── Member-Variablen ──────────────────────────────────────
    String      m_ownName;

    int         m_maxRetries;
    int         m_retryDelay;
    int         m_ackTimeout;

    int         m_dio1Pin;       // gespeichert fuer Deep Sleep EXT0

    bool        m_newPacket;
    uint32_t    m_sentCount;
    uint32_t    m_receivedCount;

    String      m_lastRawPacket;
    String      m_lastSender;
    String      m_lastData;

    SX1262*     m_radio;
    SPIClass*   m_spi;

    float       m_lastRSSI;
    float       m_lastSNR;

    // ── Statisches Interrupt-Flag ─────────────────────────────
    // Wird in ISR gesetzt, in tryReceive() ausgewertet
    static volatile bool s_packetFlag;
};
