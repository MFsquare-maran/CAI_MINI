// ============================================================
//  LORA.cpp
//  Implementierung der LORA Klasse mit RadioLib / SX1262
//  Datum:        2026-04-06
// ============================================================

#include "LORA.h"

// ============================================================
//  Konstruktor & Destruktor
// ============================================================

LORA::LORA(const String& ownName,
           LoraMode       mode,
           int            nssPin,
           int            dio1Pin,
           int            resetPin,
           int            busyPin,
           const String&  targetName,
           int            maxRetries,
           int            retryDelay,
           int            ackTimeout)
    : m_ownName(ownName),
      m_targetName(targetName),
      m_mode(mode),
      m_maxRetries(maxRetries),
      m_retryDelay(retryDelay),
      m_ackTimeout(ackTimeout),
      m_newPacket(false),
      m_sentCount(0),
      m_receivedCount(0)
{
    // SX1262 Modul mit den uebergebenen Pins erstellen
    m_radio = new SX1262(new Module(nssPin, dio1Pin, resetPin, busyPin));

    Serial.println("[LORA] Instanz erstellt: " + m_ownName +
                   " | Modus: " + getModeString() +
                   " | Ziel: "  + m_targetName);
}

LORA::~LORA()
{
    delete m_radio;
}

// ============================================================
//  Initialisierung
// ============================================================

bool LORA::begin()
{
    Serial.println("[LORA] Initialisiere SX1262...");
    Serial.println("       Frequenz:     " + String(LORA_FREQ)     + " MHz");
    Serial.println("       Bandbreite:   " + String(LORA_BW)       + " kHz");
    Serial.println("       SF:           " + String(LORA_SF));
    Serial.println("       CR:           " + String(LORA_CR));
    Serial.println("       Leistung:     " + String(LORA_TX_POWER) + " dBm");
    Serial.println("       Sync Word:    0x" + String(SYNC_WORD, HEX));

    int state = m_radio->begin(LORA_FREQ,
                                LORA_BW,
                                LORA_SF,
                                LORA_CR,
                                SYNC_WORD,
                                LORA_TX_POWER);

    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.println("[LORA] FEHLER bei Initialisierung! Code: " +
                       String(state));
        return false;
    }

    Serial.println("[LORA] Initialisierung erfolgreich!");
    return true;
}

// ============================================================
//  Oeffentliche Methoden
// ============================================================

bool LORA::transmit(const String& empfaenger, const String& daten)
{
    String packet = buildPacket(empfaenger, LORA_TYPE_DATA, daten);

    for (int versuch = 1; versuch <= m_maxRetries; versuch++)
    {
        Serial.println("[LORA] Sende (Versuch " + String(versuch) +
                       "/" + String(m_maxRetries) + "): " + packet);

        if (!sendRaw(packet))
        {
            Serial.println("[LORA] Senden fehlgeschlagen (RadioLib Fehler)");
            delay(m_retryDelay);
            continue;
        }

        // Warte auf ACK vom Empfaenger
        if (waitForAck(empfaenger))
        {
            m_sentCount++;
            Serial.println("[LORA] ACK erhalten von: " + empfaenger);
            return true;
        }

        Serial.println("[LORA] Kein ACK. Warte " +
                       String(m_retryDelay) + "ms...");
        delay(m_retryDelay);
    }

    Serial.println("[LORA] Alle " + String(m_maxRetries) +
                   " Versuche fehlgeschlagen fuer: " + empfaenger);
    return false;
}

bool LORA::packetReceived()
{
    String raw = tryReceive();

    if (raw.length() == 0)
    {
        return false;
    }

    Serial.println("[LORA] Rohpaket empfangen: " + raw);

    if (!parsePacket(raw))
    {
        Serial.println("[LORA] Paket nicht fuer mich oder ungueltig. Ignoriere.");
        return false;
    }

    // ACK zurueck an Sender schicken
    sendAck(m_lastSender);

    // Bei ROUTER: Daten automatisch an Gateway weiterleiten
    if (m_mode == LoraMode::ROUTER)
    {
        Serial.println("[LORA] ROUTER leitet weiter an: " + m_targetName);
        transmit(m_targetName, m_lastData);
    }

    m_newPacket = true;
    m_receivedCount++;
    return true;
}

String LORA::readData()
{
    m_newPacket = false;
    return m_lastData;
}

String LORA::readRawPacket()
{
    return m_lastRawPacket;
}

String LORA::readSender()
{
    return m_lastSender;
}

void LORA::setTargetName(const String& targetName)
{
    m_targetName = targetName;
    Serial.println("[LORA] Neues Ziel gesetzt: " + m_targetName);
}

String LORA::getModeString() const
{
    switch (m_mode)
    {
        case LoraMode::SENSOR:  return "SENSOR";
        case LoraMode::ROUTER:  return "ROUTER";
        case LoraMode::GATEWAY: return "GATEWAY";
        default:                return "UNBEKANNT";
    }
}

uint32_t LORA::getSentCount() const
{
    return m_sentCount;
}

uint32_t LORA::getReceivedCount() const
{
    return m_receivedCount;
}

// ============================================================
//  Private Hilfsmethoden
// ============================================================

String LORA::buildPacket(const String& empfaenger,
                          const String& typ,
                          const String& daten) const
{
    // Format: "EMPFAENGER|SENDER|TYP|DATEN"
    return empfaenger + LORA_PACKET_SEPARATOR +
           m_ownName  + LORA_PACKET_SEPARATOR +
           typ        + LORA_PACKET_SEPARATOR +
           daten;
}

bool LORA::sendRaw(const String& packet)
{
    // Lokale nicht-const Kopie erstellen,
    // da RadioLib transmit(String&) eine non-const Referenz erwartet
    String packetCopy = packet;

    int state = m_radio->transmit(packetCopy);

    if (state == RADIOLIB_ERR_NONE)
    {
        return true;
    }

    Serial.println("[LORA] sendRaw Fehler: " + String(state));
    return false;
}

bool LORA::waitForAck(const String& expectedSender)
{
    unsigned long startTime = millis();

    while (millis() - startTime < (unsigned long)m_ackTimeout)
    {
        String incoming = tryReceive();

        if (incoming.length() == 0)
        {
            delay(10);
            continue;
        }

        String dest    = splitGet(incoming, 0);  // muss mein Name sein
        String sender  = splitGet(incoming, 1);  // muss expectedSender sein
        String typ     = splitGet(incoming, 2);  // muss ACK sein
        String payload = splitGet(incoming, 3);  // muss "received" sein

        if (dest    == m_ownName      &&
            sender  == expectedSender &&
            typ     == LORA_TYPE_ACK  &&
            payload == LORA_ACK_PAYLOAD)
        {
            return true;
        }

        Serial.println("[LORA] Fremdes Paket waehrend ACK-Warten ignoriert: " +
                       incoming);
    }

    return false;  // Timeout
}

void LORA::sendAck(const String& empfaenger)
{
    String ackPacket = buildPacket(empfaenger, LORA_TYPE_ACK, LORA_ACK_PAYLOAD);

    Serial.println("[LORA] Sende ACK an: " + empfaenger);
    delay(50);  // kurze Pause damit Sender in Empfangsmodus ist
    sendRaw(ackPacket);
}

bool LORA::isForMe(const String& packet) const
{
    int firstSep = packet.indexOf(LORA_PACKET_SEPARATOR);

    if (firstSep < 0)
    {
        return false;
    }

    String dest = packet.substring(0, firstSep);
    return (dest == m_ownName);
}

bool LORA::parsePacket(const String& raw)
{
    if (!isForMe(raw))
    {
        return false;
    }

    String dest    = splitGet(raw, 0);
    String sender  = splitGet(raw, 1);
    String typ     = splitGet(raw, 2);
    String payload = splitGet(raw, 3);

    if (sender.length() == 0 || typ.length() == 0)
    {
        Serial.println("[LORA] Paket ungueltig (fehlende Felder)");
        return false;
    }

    // ACK-Pakete nicht als neue Daten behandeln
    if (typ == LORA_TYPE_ACK)
    {
        return false;
    }

    m_lastRawPacket = raw;
    m_lastSender    = sender;
    m_lastData      = payload;

    Serial.println("[LORA] Paket gueltig!");
    Serial.println("       Von:   " + m_lastSender);
    Serial.println("       Daten: " + m_lastData);

    return true;
}

String LORA::tryReceive()
{
    String received = "";
    int state = m_radio->receive(received);

    if (state == RADIOLIB_ERR_NONE)
    {
        return received;
    }

    if (state != RADIOLIB_ERR_RX_TIMEOUT)
    {
        Serial.println("[LORA] Empfangsfehler: " + String(state));
    }

    return "";
}

String LORA::splitGet(const String& str, int index) const
{
    int found = 0;
    int start = 0;

    for (int i = 0; i <= (int)str.length(); i++)
    {
        if (i == (int)str.length() || str[i] == LORA_PACKET_SEPARATOR)
        {
            if (found == index)
            {
                return str.substring(start, i);
            }
            found++;
            start = i + 1;
        }
    }

    return "";
}
