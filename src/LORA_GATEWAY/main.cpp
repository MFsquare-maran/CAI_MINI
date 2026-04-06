#include "LORA_config.h"
#include "config_LORA_GATEWAY.h"
#include <Arduino.h>
#include "LORA.h"

// ------------------------------------------------------------
//  GATEWAY Instanz
//  - Kein Zielgeraet noetig (Gateway ist Endpunkt)
//  - targetName = "" da Gateway nichts weiterleitet
// ------------------------------------------------------------
LORA Lora_gateway("GATEWAY01", LoraMode::GATEWAY, LORA_NSS, LORA_DIO1, LORA_RESET, LORA_BUSY, "", 3, 500, 2000);


void setup()
{
    Serial.begin(115200);
    delay(5000); // Warte 5 Sekunden fuer Serial Debugging

    Serial.println("CAI_MINI LoRa Gateway gestartet.");

    Lora_gateway.begin();
}




void loop()
{
    // Auf eingehende Pakete warten
    if (Lora_gateway.packetReceived())
    {
        String sender = Lora_gateway.readSender();
        String data   = Lora_gateway.readData();

        Serial.println("------------------------------------------");
        Serial.println("[GATEWAY] Neues Paket empfangen!");
        Serial.println("          Von:   " + sender);
        Serial.println("          Daten: " + data);
        Serial.println("          Pakete gesamt: " + String(Lora_gateway.getReceivedCount()));
        Serial.println("------------------------------------------");

        // Hier kannst du die Daten weiterverarbeiten:
        // z.B. MQTT, HTTP, Datenbank, Display...
    }
}
