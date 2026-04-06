#pragma once

// ============================================================
//  LORA.h
//  Beschreibung: LoRa Kommunikationsklasse fuer ESP32
//                Unterstuetzt: SENSOR, ROUTER, GATEWAY
//  Library:      RadioLib (SX1262)
//  Datum:        2026-04-06
// ============================================================

#include <RadioLib.h>
#include <Arduino.h>

// ------------------------------------------------------------
//  LoRa Funk-Konfiguration (fest definiert)
// ------------------------------------------------------------
#define LORA_FREQ       868.0   // Frequenz in MHz
#define LORA_BW         125.0   // Bandbreite in kHz (Standard EU)
#define LORA_SF         12      // Spreading Factor (7=schnell, 12=max Reichweite)
#define LORA_CR         5       // Coding Rate
#define LORA_TX_POWER   22      // Sendeleistung in dBm (max = 22)
#define SYNC_WORD       0xB6    // Sync Word (privates Netzwerk)

// ------------------------------------------------------------
//  Paket-Konfiguration
// ------------------------------------------------------------
#define LORA_PACKET_SEPARATOR   '|'
#define LORA_TYPE_DATA          "DATA"
#define LORA_TYPE_ACK           "ACK"
#define LORA_ACK_PAYLOAD        "received"

// ------------------------------------------------------------
//  Geraete-Modi
// ------------------------------------------------------------
enum class LoraMode
{
    SENSOR,
    ROUTER,
    GATEWAY
};

// ============================================================
//  Klasse LORA
// ============================================================
class LORA
{
public:

    // ----------------------------------------------------------
    //  Konstruktor
    //
    //  Parameter:
    //    ownName     - Eigener Geraetename      z.B. "SENSOR01"
    //    mode        - Geraete-Modus            SENSOR / ROUTER / GATEWAY
    //    nssPin      - SPI Chip-Select Pin
    //    dio1Pin     - DIO1 Interrupt Pin
    //    resetPin    - Reset Pin
    //    busyPin     - Busy Pin
    //    targetName  - Zielgeraet               z.B. "ROUTER01" oder "GATEWAY01"
    //                  (Fuer ROUTER: Name des Gateways wohin weitergeleitet wird)
    //    maxRetries  - Maximale Sendeversuche   (default: 3)
    //    retryDelay  - Wartezeit zw. Versuchen  (default: 500 ms)
    //    ackTimeout  - Timeout fuer ACK-Warten  (default: 2000 ms)
    // ----------------------------------------------------------
    LORA(const String& ownName,
         LoraMode       mode,
         int            nssPin,
         int            dio1Pin,
         int            resetPin,
         int            busyPin,
         const String&  targetName,
         int            maxRetries = 3,
         int            retryDelay = 500,
         int            ackTimeout = 2000);

    // Kein Kopieren erlaubt (Hardwarereferenz)
    LORA(const LORA&)            = delete;
    LORA& operator=(const LORA&) = delete;

    ~LORA();

    // ----------------------------------------------------------
    //  Initialisierung — muss in setup() aufgerufen werden
    //  Alle Funk-Parameter kommen aus den Defines oben
    //
    //  Rueckgabe: true wenn Initialisierung erfolgreich
    // ----------------------------------------------------------
    bool begin();

    // ----------------------------------------------------------
    //  Oeffentliche Methoden
    // ----------------------------------------------------------

    /// Sendet Daten mit ACK-Logik
    /// Rueckgabe: true = ACK erhalten, false = alle Versuche fehlgeschlagen
    bool transmit(const String& empfaenger, const String& daten);

    /// Prueft ob ein neues Paket eingegangen ist
    /// Intern wird ACK versendet wenn Paket fuer dieses Geraet ist
    /// ROUTER leitet Paket automatisch an m_targetName weiter
    bool packetReceived();

    /// Gibt die Nutzdaten des letzten gueltigen Pakets zurueck
    String readData();

    /// Gibt den kompletten rohen Paket-String zurueck
    String readRawPacket();

    /// Gibt den Absender des letzten Pakets zurueck
    String readSender();

    /// Setzt einen neuen Ziel-Namen zur Laufzeit
    void setTargetName(const String& targetName);

    /// Gibt aktuellen Geraete-Modus als String zurueck
    String getModeString() const;

    /// Gibt Anzahl erfolgreicher Sendungen zurueck
    uint32_t getSentCount() const;

    /// Gibt Anzahl empfangener Pakete zurueck
    uint32_t getReceivedCount() const;

private:

    // ----------------------------------------------------------
    //  RadioLib Objekte
    // ----------------------------------------------------------
    SX1262* m_radio;            ///< Zeiger auf das SX1262 Modul

    // ----------------------------------------------------------
    //  Konfiguration
    // ----------------------------------------------------------
    String   m_ownName;         ///< Eigener Geraetename  z.B. "SENSOR01"
    String   m_targetName;      ///< Ziel-Geraetename     z.B. "GATEWAY01"
    LoraMode m_mode;            ///< Aktueller Geraete-Modus

    int      m_maxRetries;      ///< Maximale Sendeversuche
    int      m_retryDelay;      ///< Wartezeit zwischen Versuchen (ms)
    int      m_ackTimeout;      ///< Maximale Wartezeit auf ACK (ms)

    // ----------------------------------------------------------
    //  Zustandsvariablen
    // ----------------------------------------------------------
    String   m_lastRawPacket;   ///< Letztes rohe empfangenes Paket
    String   m_lastData;        ///< Letzter Nutzdaten-Teil
    String   m_lastSender;      ///< Absender des letzten Pakets
    bool     m_newPacket;       ///< Flag: neues gueltiges Paket vorhanden

    uint32_t m_sentCount;       ///< Zaehler erfolgreiche Sendungen
    uint32_t m_receivedCount;   ///< Zaehler empfangene Pakete

    // ----------------------------------------------------------
    //  Private Hilfsmethoden
    // ----------------------------------------------------------

    /// Baut einen vollstaendigen Paket-String zusammen
    /// Format: "EMPFAENGER|SENDER|TYP|DATEN"
    String buildPacket(const String& empfaenger,
                       const String& typ,
                       const String& daten) const;

    /// Sendet einen rohen String ueber LoRa
    bool sendRaw(const String& packet);

    /// Wartet auf ein ACK-Paket von einem bestimmten Sender
    /// Rueckgabe: true = ACK erhalten innerhalb des Timeouts
    bool waitForAck(const String& expectedSender);

    /// Sendet ein ACK zurueck an den Absender
    void sendAck(const String& empfaenger);

    /// Prueft ob ein Paket fuer dieses Geraet bestimmt ist
    bool isForMe(const String& packet) const;

    /// Parst einen Paket-String und befuellt interne Felder
    /// Rueckgabe: true wenn Paket gueltig und fuer dieses Geraet
    bool parsePacket(const String& raw);

    /// Versucht ein Paket zu empfangen (nicht-blockierend)
    /// Rueckgabe: empfangener String oder leerer String
    String tryReceive();

    /// Gibt einen Split-Teil eines Strings zurueck (Separator '|')
    String splitGet(const String& str, int index) const;
};
