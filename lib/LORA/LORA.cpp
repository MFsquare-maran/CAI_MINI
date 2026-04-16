// ============================================================
//  LORA.cpp
//  Implementierung der LORA Klasse mit RadioLib / SX1262
//  Interrupt-basierter Empfang via DIO1
//  Datum:        2026-04-16
//  Fixes:        - s_packetFlag korrekt genutzt
//                - ISR in begin() + sendRaw() registriert
//                - Deadlock-Fix in waitForAck()
//                - Deep Sleep via deepSleepUntilPacket()
// ============================================================

#include "LORA.h"

// ============================================================
//  Statisches Member definieren (genau einmal in .cpp!)
// ============================================================
volatile bool LORA::s_packetFlag = false;

// ============================================================
//  Statische ISR — wird von DIO1 ausgeloest
// ============================================================
void IRAM_ATTR LORA::onDio1Interrupt()
{
    s_packetFlag = true;
}

// ============================================================
//  Konstruktor
// ============================================================
LORA::LORA(int            nssPin,
           int            dio1Pin,
           int            resetPin,
           int            busyPin,
           SPIClass&      spi,
           int            sckPin,
           int            misoPin,
           int            mosiPin,
           int            maxRetries,
           int            retryDelay,
           int            ackTimeout)
    : m_maxRetries(maxRetries),
      m_retryDelay(retryDelay),
      m_ackTimeout(ackTimeout),
      m_dio1Pin(dio1Pin),
      m_newPacket(false),
      m_sentCount(0),
      m_receivedCount(0),
      m_lastRSSI(0.0f),
      m_lastSNR(0.0f),
      m_spi(&spi)
{
    m_spi->begin(sckPin, misoPin, mosiPin, nssPin);
    m_radio = new SX1262(new Module(nssPin, dio1Pin, resetPin, busyPin, *m_spi));
}

// ============================================================
//  Destruktor
// ============================================================
LORA::~LORA()
{
    delete m_radio;
}

// ============================================================
//  begin() — Initialisierung
// ============================================================
bool LORA::begin(const String& ownName)
{
    m_ownName = ownName;

    gpio_install_isr_service(0);

    Serial.println("[LORA] Initialisiere SX1262...");
    Serial.println("[LORA] Konfiguration:");
    Serial.println("       Eigenname:    " + m_ownName);
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

    // DIO2 als RF-Switch (noetig bei den meisten SX1262-Modulen)
    m_radio->setDio2AsRfSwitch(true);

    // ── ISR auf DIO1 registrieren ─────────────────────────────
    m_radio->setDio1Action(onDio1Interrupt);
    s_packetFlag = false;

    // ── Empfangsmodus starten ─────────────────────────────────
    state = m_radio->startReceive();
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.println("[LORA] FEHLER bei startReceive()! Code: " +
                       String(state));
        return false;
    }

    Serial.println("[LORA] Initialisierung erfolgreich! (Interrupt-Modus)");
    return true;
}

// ============================================================
//  transmit — sendet mit ACK-Handshake
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

// ============================================================
//  packetReceived — fuer Gateway/Router im loop()
// ============================================================
bool LORA::packetReceived()
{
    String raw = tryReceive();
    if (raw.length() == 0) return false;

    Serial.println("[LORA] Rohpaket empfangen: " + raw);

    if (!parsePacket(raw))
    {
        Serial.println("[LORA] Paket nicht fuer mich oder ungueltig. Ignoriere.");
        return false;
    }

    sendAck(m_lastSender);

    m_newPacket = true;
    m_receivedCount++;
    return true;
}

// ============================================================
//  Getter
// ============================================================
String   LORA::readData()       { m_newPacket = false; return m_lastData;      }
String   LORA::readRawPacket()  { return m_lastRawPacket; }
String   LORA::readSender()     { return m_lastSender;    }
uint32_t LORA::getSentCount()     const { return m_sentCount;     }
uint32_t LORA::getReceivedCount() const { return m_receivedCount; }
float    LORA::getLastRSSI()            { return m_lastRSSI;       }
float    LORA::getLastSNR()             { return m_lastSNR;        }

// ============================================================
//  forcePacketFlag — fuer Deep Sleep Wake-Up
// ============================================================
void LORA::forcePacketFlag()
{
    s_packetFlag = true;
    Serial.println("[LORA] forcePacketFlag() gesetzt (Wake-Up Pfad)");
}

// ============================================================
//  deepSleepUntilPacket — ESP32 schlaeft bis DIO1 HIGH
// ============================================================
void LORA::deepSleepUntilPacketorTimer(uint16_t sleepTimeSeconds)
{
    Serial.println("[LORA] Bereite Deep Sleep vor...");
    Serial.println("[LORA] SX1262 bleibt im RX-Modus (DIO1 = Wake-Up)");
    Serial.println("[LORA] Wake-Up Pin: GPIO" + String(m_dio1Pin));
    Serial.flush();

    // SX1262 bleibt aktiv im RX-Modus — DIO1 geht HIGH wenn Paket ankommt
    // ISR deaktivieren (laeuft im Sleep sowieso nicht)
    m_radio->clearDio1Action();

    // ── EXT0: ESP32 wacht auf HIGH-Signal an DIO1 auf ────────
    esp_sleep_enable_ext0_wakeup((gpio_num_t)m_dio1Pin, 1); // 1 = HIGH

    esp_sleep_enable_timer_wakeup( 30 * 1000 * 1000ULL); // Backup: nach 10 Minuten aufwachen
    
    Serial.println("[LORA] Gehe in Deep Sleep... Tschuess!");
    Serial.flush();

    esp_deep_sleep_start();
    // ← ab hier laeuft NICHTS mehr. Naechste Zeile: setup() nach Wake-Up
}


// ============================================================
//  deepSleepUntilPacket — ESP32 schlaeft bis DIO1 HIGH
// ============================================================
void LORA::deepSleepUntilTimer(uint16_t sleepTimeSeconds)
{
    Serial.println("[LORA] Bereite Deep Sleep vor...");
    Serial.flush();

    // SX1262 bleibt aktiv im RX-Modus — DIO1 geht HIGH wenn Paket ankommt
    // ISR deaktivieren (laeuft im Sleep sowieso nicht)
    m_radio->clearDio1Action();
    m_radio->sleep();

    // ── EXT0: ESP32 wacht auf HIGH-Signal an DIO1 auf ────────


    esp_sleep_enable_timer_wakeup( sleepTimeSeconds * 1000 * 1000ULL); // Backup: nach sleepTimeSeconds aufwachen

    Serial.println("[LORA] Gehe in Deep Sleep... Tschuess!");
    Serial.flush();

    esp_deep_sleep_start();
    // ← ab hier laeuft NICHTS mehr. Naechste Zeile: setup() nach Wake-Up
}

// ============================================================
//  buildPacket
// ============================================================
String LORA::buildPacket(const String& empfaenger,
                          const String& typ,
                          const String& daten) const
{
    return empfaenger + LORA_PACKET_SEPARATOR +
           m_ownName  + LORA_PACKET_SEPARATOR +
           typ        + LORA_PACKET_SEPARATOR +
           daten;
}

// ============================================================
//  sendRaw — ISR waehrend TX deaktivieren, danach neu setzen
// ============================================================
bool LORA::sendRaw(const String& packet)
{
    // ISR deaktivieren damit TX-Done nicht faelschlicherweise
    // als RX-Paket interpretiert wird
    m_radio->clearDio1Action();
    s_packetFlag = false;

    // ── FIX: nicht-const Kopie fuer RadioLib transmit() ──────
    String packetCopy = packet;  // ← das war vorher gefehlt / entfernt
    int state = m_radio->transmit(packetCopy);  // ← packetCopy statt packet

    // ISR nach TX wieder aktivieren
    m_radio->setDio1Action(onDio1Interrupt);
    s_packetFlag = false;

    // Sofort wieder in Empfangsmodus
    m_radio->startReceive();

    if (state == RADIOLIB_ERR_NONE)
    {
        return true;
    }

    Serial.println("[LORA] sendRaw Fehler: " + String(state));
    return false;
}


// ============================================================
//  waitForAck — FIX: Deadlock durch DATA-During-Wait behoben
// ============================================================
bool LORA::waitForAck(const String& expectedSender)
{
    unsigned long startTime = millis();

    while (millis() - startTime < (unsigned long)m_ackTimeout)
    {
        String incoming = tryReceive();

        if (incoming.length() == 0)
        {
            delay(5);
            continue;
        }

        String dest    = splitGet(incoming, 0);
        String sender  = splitGet(incoming, 1);
        String typ     = splitGet(incoming, 2);
        String payload = splitGet(incoming, 3);

        // ── Fall 1: Erwartetes ACK ────────────────────────────
        if (dest    == m_ownName      &&
            sender  == expectedSender &&
            typ     == LORA_TYPE_ACK  &&
            payload == LORA_ACK_PAYLOAD)
        {
            Serial.println("[LORA] ACK empfangen von: " + sender);
            return true;
        }

        // ── Fall 2: DATA-Paket fuer mich waehrend ACK-Warten ─
        // → sofort ACK-en damit Gegenseite nicht im Deadlock haengt!
        if (dest == m_ownName && typ == LORA_TYPE_DATA)
        {
            Serial.println("[LORA] DATA waehrend ACK-Wait von: " +
                           sender + " → sende ACK zurueck");
            parsePacket(incoming);
            m_newPacket = true;
            m_receivedCount++;
            sendAck(sender);
            // Weiter auf unser eigenes ACK warten
            continue;
        }

        // ── Fall 3: Wirklich fremdes Paket ───────────────────
        Serial.println("[LORA] Fremdes Paket ignoriert: " + incoming);
    }

    Serial.println("[LORA] ACK Timeout fuer: " + expectedSender);
    return false;
}

// ============================================================
//  sendAck
// ============================================================
void LORA::sendAck(const String& empfaenger)
{
    String ackPacket = buildPacket(empfaenger, LORA_TYPE_ACK, LORA_ACK_PAYLOAD);
    Serial.println("[LORA] Sende ACK an: " + empfaenger);
    delay(20); // kurze Pause — Sender muss in RX sein
    sendRaw(ackPacket);
}

// ============================================================
//  tryReceive — korrekt interrupt-basiert via s_packetFlag
// ============================================================
String LORA::tryReceive()
{
    // ── Nur lesen wenn ISR das Flag gesetzt hat ───────────────
    if (!s_packetFlag)
    {
        return "";
    }

    // Flag sofort loeschen (vor readData, nicht danach!)
    s_packetFlag = false;

    String received = "";
    int state = m_radio->readData(received);

    // ISR neu registrieren + RX wieder starten
    m_radio->setDio1Action(onDio1Interrupt);
    m_radio->startReceive();

    if (state == RADIOLIB_ERR_NONE)
    {
        m_lastRSSI = m_radio->getRSSI();
        m_lastSNR  = m_radio->getSNR();
        Serial.println("[LORA] RSSI: " + String(m_lastRSSI, 1) +
                       " dBm | SNR: "  + String(m_lastSNR,  1) + " dB");
        return received;
    }

    if (state != RADIOLIB_ERR_RX_TIMEOUT)
    {
        Serial.println("[LORA] Empfangsfehler: " + String(state));
    }

    return "";
}

// ============================================================
//  isForMe / parsePacket / splitGet
// ============================================================
bool LORA::isForMe(const String& packet) const
{
    int firstSep = packet.indexOf(LORA_PACKET_SEPARATOR);
    if (firstSep < 0) return false;
    return (packet.substring(0, firstSep) == m_ownName);
}

bool LORA::parsePacket(const String& raw)
{
    if (!isForMe(raw)) return false;

    String sender  = splitGet(raw, 1);
    String typ     = splitGet(raw, 2);
    String payload = splitGet(raw, 3);

    if (sender.length() == 0 || typ.length() == 0)
    {
        Serial.println("[LORA] Paket ungueltig (fehlende Felder)");
        return false;
    }

    if (typ == LORA_TYPE_ACK) return false;

    m_lastRawPacket = raw;
    m_lastSender    = sender;
    m_lastData      = payload;

    Serial.println("[LORA] Paket gueltig!");
    Serial.println("       Von:   " + m_lastSender);
    Serial.println("       Daten: " + m_lastData);

    return true;
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
                return str.substring(start, i);
            found++;
            start = i + 1;
        }
    }
    return "";
}
