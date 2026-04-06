#include "LORA_config.h"
#include "config_LORA_SENSOR.h"
#include <Arduino.h>
#include "LORA.h"

LORA Lora_sensor("SENSOR01",LoraMode::SENSOR,LORA_NSS, LORA_DIO1, LORA_RESET, LORA_BUSY,"ROUTER01",3,500,2000);


u16_t cnt = 0;


void setup()
{
    Serial.begin(115200);
    delay(5000); // Warte 5 Sekunden für Serial Debugging

    Serial.println("CAI_MINI LoRa Sensor gestartet.");

    Lora_sensor.begin();
}

void loop()
{
    cnt++;
    
    Lora_sensor.transmit("GATEWAY01", "Sensorwert " + String(cnt));

    // Hier würden die Sensorwerte gelesen und per LoRa gesendet werden
    Serial.println("Sensorwerte lesen und senden...");

    delay(5000); // Warte 5 Sekunden bis zum nächsten Lesen/Senden
}
