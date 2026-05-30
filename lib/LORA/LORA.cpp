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

    // ── ISR Service nur einmal installieren ──────────────────
    static bool isr_service_installed = false;
    if (!isr_service_installed)
    {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }

    logln("[LORA] Initialisiere SX1262...");
    logln("[LORA] Konfiguration:");
    logln("       Eigenname:    " + m_ownName);
    logln("       Frequenz:     " + String(LORA_FREQ)     + " MHz");
    logln("       Bandbreite:   " + String(LORA_BW)       + " kHz");
    logln("       SF:           " + String(LORA_SF));
    logln("       CR:           " + String(LORA_CR));
    logln("       Leistung:     " + String(LORA_TX_POWER) + " dBm");
    logln("       Sync Word:    0x" + String(SYNC_WORD, HEX));

    int state = m_radio->begin(LORA_FREQ,
                               LORA_BW,
                               LORA_SF,
                               LORA_CR,
                               SYNC_WORD,
                               LORA_TX_POWER);

    if (state != RADIOLIB_ERR_NONE)
    {
        logln("[LORA] FEHLER bei Initialisierung! Code: " +
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
        logln("[LORA] FEHLER bei startReceive()! Code: " +
                       String(state));
        return false;
    }

    logln("[LORA] Initialisierung erfolgreich!");
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
        logln("[LORA] Sende (Versuch " + String(versuch) +
               "/" + String(m_maxRetries) + "): " + 
               empfaenger + "|" + m_ownName + "|DATA|" + daten);

        if (!sendRaw(packet))
        {
            logln("[LORA] Senden fehlgeschlagen (RadioLib Fehler)");
            delay(m_retryDelay);
            continue;
        }

        if (waitForAck(empfaenger))
        {
            m_sentCount++;
            logln("[LORA] ACK erhalten von: " + empfaenger);
            return true;
        }

        logln("[LORA] Kein ACK. Warte " +
                       String(m_retryDelay) + "ms...");
        delay(m_retryDelay);
    }

    logln("[LORA] Alle " + String(m_maxRetries) +
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

    logln("[LORA] Rohpaket empfangen: " + raw);

    if (!parsePacket(raw))
    {
        logln("[LORA] Paket nicht fuer mich oder ungueltig. Ignoriere.");
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
    logln("[LORA] forcePacketFlag() gesetzt (Wake-Up Pfad)");
}

void LORA::sleepRadio()
{
    m_radio->clearDio1Action();
    m_radio->sleep();
    logln("[LORA] Radio → Sleep.");
}


// ============================================================
//  sendRaw — ISR waehrend TX deaktivieren, danach neu setzen
// ============================================================
bool LORA::sendRaw(const String& packet)
{
    // ── CSMA: Kanal prüfen vor dem Senden ────────────────────
    const int MAX_ROUNDS = 3;

    for (int round = 0; round < MAX_ROUNDS; round++)
    {
        m_radio->clearDio1Action();
        s_packetFlag = false;

        int state = m_radio->scanChannel();

        m_radio->setDio1Action(onDio1Interrupt);
        s_packetFlag = false;
        m_radio->startReceive();

        if (state == RADIOLIB_LORA_DETECTED)
        {   uint16_t ca = random(2000,3000);

            logln("[LORA] CSMA: Kanal besetzt (Runde " + String(round + 1) + ") – warte " + String(ca) + "ms");
            
            delay(ca);
            continue;
        }

        // Kanal frei → senden
        logln("[LORA] CSMA: Kanal frei (Runde " + String(round + 1) + ") – sende");

        m_radio->clearDio1Action();
        s_packetFlag = false;

        String packetCopy = packet;
        int txState = m_radio->transmit(packetCopy);

        m_radio->setDio1Action(onDio1Interrupt);
        s_packetFlag = false;
        m_radio->startReceive();

        if (txState == RADIOLIB_ERR_NONE) return true;

        logln("[LORA] sendRaw Fehler: " + String(txState));
        return false;
    }

    // Nach 3× besetzt → abbrechen, nicht senden
    logln("[LORA] CSMA: Kanal 3× besetzt – Abbruch.");
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
            logln("[LORA] ACK empfangen von: " + sender);
            return true;
        }

        // ── Fall 2: DATA-Paket fuer mich waehrend ACK-Warten ─
        // → sofort ACK-en damit Gegenseite nicht im Deadlock haengt!
        if (dest == m_ownName && typ == LORA_TYPE_DATA)
        {
            logln("[LORA] DATA waehrend ACK-Wait von: " +
                           sender + " → sende ACK zurueck");
            parsePacket(incoming);
            m_newPacket = true;
            m_receivedCount++;
            sendAck(sender);
            // Weiter auf unser eigenes ACK warten
            continue;
        }

        // ── Fall 3: Wirklich fremdes Paket ───────────────────
        logln("[LORA] Fremdes Paket ignoriert: " + incoming);
    }

    logln("[LORA] ACK Timeout fuer: " + expectedSender);
    return false;
}

// ============================================================
//  sendAck
// ============================================================
void LORA::sendAck(const String& empfaenger)
{
    String ackPacket = buildPacket(empfaenger, LORA_TYPE_ACK, LORA_ACK_PAYLOAD);
    logln("[LORA] Sende ACK an: " + empfaenger);
    delay(100); 
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
        logln("[LORA] RSSI: " + String(m_lastRSSI, 1) +
                       " dBm | SNR: "  + String(m_lastSNR,  1) + " dB");
        return received;
    }

    if (state != RADIOLIB_ERR_RX_TIMEOUT)
    {
        logln("[LORA] Empfangsfehler: " + String(state));
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





// ============================================================
//  buildPacket
// ============================================================
String LORA::buildPacket(const String& empfaenger,
                          const String& typ,
                          const String& daten) const
{
    String payload = daten;

#ifdef LORA_ENCRYPTION_ENABLED
    if (typ == LORA_TYPE_DATA)
    {
        payload = encrypt(daten);
        if (payload.length() == 0)
        {
            logln("[LORA] Verschlüsselung fehlgeschlagen");
            return "";
        }
    }
#endif

    return empfaenger + LORA_PACKET_SEPARATOR +
           m_ownName  + LORA_PACKET_SEPARATOR +
           typ        + LORA_PACKET_SEPARATOR +
           payload;
}

// ============================================================
//  parsePacket
// ============================================================
bool LORA::parsePacket(const String& raw)
{
    if (!isForMe(raw)) return false;

    String sender  = splitGet(raw, 1);
    String typ     = splitGet(raw, 2);
    String payload = splitGet(raw, 3);

    if (sender.length() == 0 || typ.length() == 0)
    {
        logln("[LORA] Paket ungueltig (fehlende Felder)");
        return false;
    }

    if (typ == LORA_TYPE_ACK) return false;

#ifdef LORA_ENCRYPTION_ENABLED
    String decrypted = decrypt(payload);
    if (decrypted.length() == 0)
    {
        logln("[LORA] Entschlüsselung fehlgeschlagen – verworfen");
        return false;
    }
    if (!decrypted.startsWith("Token:"))
    {
        logln("[LORA] Paket verworfen – ungueltiges Format");
        return false;
    }
    payload = decrypted;
#endif

    m_lastRawPacket = raw;
    m_lastSender    = sender;
    m_lastData      = payload;

    logln("[LORA] Paket gueltig!");
    logln("       Von:   " + m_lastSender);
    logln("       Daten: " + m_lastData);

    return true;
}

#ifdef LORA_ENCRYPTION_ENABLED

// ============================================================
//  encrypt — ChaCha20 + Base64
// ============================================================
String LORA::encrypt(const String& plaintext) const
{
    int len = plaintext.length();

    // ── IV zufällig generieren (8 Bytes) ─────────────────────
    uint8_t iv[CHACHA_IV_LENGTH];
    for (int i = 0; i < CHACHA_IV_LENGTH; i++)
        iv[i] = (uint8_t)random(0, 256);

    // ── ChaCha20 verschlüsseln ────────────────────────────────
    uint8_t ciphertext[len];
    ChaCha chacha;
    chacha.setKey(CHACHA_KEY, CHACHA_KEY_LENGTH);
    chacha.setIV(iv, CHACHA_IV_LENGTH);
    chacha.encrypt(ciphertext, (const uint8_t*)plaintext.c_str(), len);

    // ── IV + Ciphertext zusammenführen ────────────────────────
    int combinedLen = CHACHA_IV_LENGTH + len;
    uint8_t combined[combinedLen];
    memcpy(combined,                   iv,         CHACHA_IV_LENGTH);
    memcpy(combined + CHACHA_IV_LENGTH, ciphertext, len);

    // ── Base64 kodieren ───────────────────────────────────────
    int b64Len = Base64.encodedLength(combinedLen);
    char b64[b64Len + 1];
    Base64.encode(b64, (char*)combined, combinedLen);
    b64[b64Len] = '\0';

    return String(b64);
}

// ============================================================
//  decrypt — Base64 + ChaCha20
// ============================================================
String LORA::decrypt(const String& ciphertext) const
{
    // ── Base64 dekodieren ─────────────────────────────────────
    int decodedLen = Base64.decodedLength((char*)ciphertext.c_str(), ciphertext.length());
    uint8_t decoded[decodedLen];
    Base64.decode((char*)decoded, (char*)ciphertext.c_str(), ciphertext.length());

    if (decodedLen <= CHACHA_IV_LENGTH)
    {
        logln("[LORA] Entschlüsselung: Paket zu kurz");
        return "";
    }

    // ── IV extrahieren ────────────────────────────────────────
    uint8_t iv[CHACHA_IV_LENGTH];
    memcpy(iv, decoded, CHACHA_IV_LENGTH);

    // ── ChaCha20 entschlüsseln ────────────────────────────────
    int dataLen = decodedLen - CHACHA_IV_LENGTH;
    uint8_t plaintext[dataLen];

    ChaCha chacha;
    chacha.setKey(CHACHA_KEY, CHACHA_KEY_LENGTH);
    chacha.setIV(iv, CHACHA_IV_LENGTH);
    chacha.decrypt(plaintext, decoded + CHACHA_IV_LENGTH, dataLen);

    String result = "";
    for (int i = 0; i < dataLen; i++)
        result += (char)plaintext[i];

    return result;
}

#endif