#include "config_LORA_SENSOR.h"
#include <Arduino.h>
#include "LORA.h"
#include "BME680_Sensor.h"
#include <SPI.h>                 // Für SD-Karten-Kommunikation
#include <SD.h>                  // SD-Kartenbibliothek
#include <IniFile.h>             // Bibliothek zum Auslesen von INI-Dateien





IniFile ini("/INIT.ini", FILE_READ, true); // INI-Datei-Objekt


SPIClass spi_sd(HSPI);    // SD-Karte  → HSPI Bus
SPIClass spi_lora(FSPI);  // LoRa      → FSPI Bus (Standard)

BME680_Sensor bme;           // Sensorobjekt

LORA Lora_sensor(
    "SENSOR01",
    LoraMode::SENSOR,
    LORA_NSS,              // CS
    LORA_DIO1,             // DIO1
    LORA_RESET,            // Reset
    LORA_BUSY,             // Busy
    spi_lora,              // ← eigener SPI Bus
    LORA_SCK,              // ← SCK  = 7
    LORA_MISO,             // ← MISO = 8
    LORA_MOSI,             // ← MOSI = 9
    "",                    // targetName (spaeter per setTargetName)
    3, 500, 2000
);

float temperature = 0.0;     // Temperatur
float pressure = 0.0;        // Luftdruck
float humidity = 0.0;        // Luftfeuchtigkeit
float gas_resistance = 0.0;  // Gaswiderstand
float battery_voltage = 0.0; // Akkuspannung

// Offsets aus INI-Datei für Sensorwerte
float temperature_offset = 0.0;
float Pressure_offset = 0.0;
float Huminity_offset = 0.0;
float Gas_offset = 0.0;


//ThingsBoard-Zugangsdaten
char accessToken[64];
char DeviceID[64];
char SenderID[64];


void InitSD() {
    spi_sd.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, spi_sd)) {           // ← spi_sd hinzugefuegt!
        Serial.println("Fehler: SD-Karte konnte nicht initialisiert werden!");
        while (1);
    }
    Serial.println("SD-Karte erfolgreich initialisiert!");
}




void get_ini_values() {
    if (!ini.open()) {
        Serial.println("❌ INI-Datei konnte nicht geöffnet werden!");
        while (1);
    }


    char buffer[128];
    if (ini.getValue("THINGSBOARD", "TB_TOKKEN", buffer, sizeof(buffer))) strcpy(accessToken, buffer);
    if (ini.getValue("SEND_TO", "NAME", buffer, sizeof(buffer))) strcpy(SenderID, buffer);
    if (ini.getValue("ID", "NAME", buffer, sizeof(buffer))) strcpy(DeviceID, buffer);
    if (ini.getValue("BME680", "TEMPERATURE_OFFSET", buffer, sizeof(buffer))) temperature_offset = strtof(buffer, nullptr);
    if (ini.getValue("BME680", "PRESSURE_OFFSET", buffer, sizeof(buffer))) Pressure_offset = strtof(buffer, nullptr);
    if (ini.getValue("BME680", "HUMINITY_OFFSET", buffer, sizeof(buffer))) Huminity_offset = strtof(buffer, nullptr);
    if (ini.getValue("BME680", "GAS_OFFSET", buffer, sizeof(buffer))) Gas_offset = strtof(buffer, nullptr);

    // Debug-Ausgabe
    Serial.println("✅ INI-Werte gelesen:");
    Serial.print("TB Token: "); Serial.println(accessToken);
    Serial.print("Temperature Offset: "); Serial.println(temperature_offset);
    Serial.print("Pressure Offset: "); Serial.println(Pressure_offset);
    Serial.print("Huminity Offset: "); Serial.println(Huminity_offset);
    Serial.print("Gas Offset: "); Serial.println(Gas_offset);
    ini.close();
    SD.end(); // SD-Karte beenden, da INI-Datei nicht mehr benötigt wird
}


float getBatteryVoltage() {
    const uint8_t N_POINTS = 8;
    const float voltVals[N_POINTS] = {3.5, 3.6, 3.7, 3.8, 3.9, 4.0, 4.1, 4.2};
    const uint16_t adcVals[N_POINTS] = {2644, 2720, 2801, 2889, 2972, 3060, 3155, 3249};

    uint16_t adcValue = analogRead(BATTERY_VOLTAGE);

    for (uint8_t i = 0; i < N_POINTS - 1; i++) {
        if (adcValue >= adcVals[i] && adcValue <= adcVals[i + 1]) {
            float a1 = adcVals[i];
            float a2 = adcVals[i + 1];
            float v1 = voltVals[i];
            float v2 = voltVals[i + 1];
            return v1 + (adcValue - a1) * (v2 - v1) / (a2 - a1); // lineare Interpolation
        }
    }

    // Extrapolation: unten
    if (adcValue < adcVals[0]) {
        float a1 = adcVals[0];
        float a2 = adcVals[1];
        float v1 = voltVals[0];
        float v2 = voltVals[1];
        return v1 + (adcValue - a1) * (v2 - v1) / (a2 - a1);
    }

    // Extrapolation: oben
    {
        float a1 = adcVals[N_POINTS - 2];
        float a2 = adcVals[N_POINTS - 1];
        float v1 = voltVals[N_POINTS - 2];
        float v2 = voltVals[N_POINTS - 1];
        return v1 + (adcValue - a1) * (v2 - v1) / (a2 - a1);
    }
}




void setup()
{
    Serial.begin(115200);
    delay(5000); // Warte 5 Sekunden für Serial Debugging

    Serial.println("CAI_MINI LoRa Sensor gestartet.");
    
    InitSD(); // SD-Karte initialisieren

    // LED-Pins initialisieren
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_BLUE, LOW);


    bme.begin(); // Sensor initialisieren

    get_ini_values(); // INI-Werte lesen
    bme.set_offset(temperature_offset,Pressure_offset,Huminity_offset,Gas_offset); // Offsets setzen    

    Lora_sensor.begin();
    Lora_sensor.setTargetName(SenderID); // Zielname aus INI-Datei setzen
}

void loop()
{
    if (bme.readSensor()) {                   // ← nur einmal lesen!
        temperature    = bme.getTemperature();
        pressure       = bme.getPressure();
        humidity       = bme.getHumidity();
        gas_resistance = bme.getGasResistance();
        battery_voltage = getBatteryVoltage();

        Serial.print("Temperature = ");    Serial.print(temperature);    Serial.println(" C");
        Serial.print("Pressure = ");       Serial.print(pressure);       Serial.println(" hPa");
        Serial.print("Humidity = ");       Serial.print(humidity);       Serial.println(" %");
        Serial.print("Gas Resistance = "); Serial.print(gas_resistance); Serial.println(" kOhms");
        Serial.print("Battery Voltage = ");Serial.print(battery_voltage);Serial.println(" V");
        Serial.println("------------------------------------");
    } else {
        Serial.println("Fehler beim Lesen des BME680 Sensors.");
    }

    digitalWrite(LED_BLUE, HIGH);
    Lora_sensor.transmit(SenderID,
        "Token:"       + String(accessToken) +
        ";Temperature:" + String(round(temperature    * 100.0) / 100.0) +
        ";Pressure:"    + String(round(pressure        * 100.0) / 100.0) +
        ";Humidity:"    + String(round(humidity        * 100.0) / 100.0) +
        ";Gas_Resistance:"         + String(round(gas_resistance  * 100.0) / 100.0) +
        ";Battery_Voltage:"        + String(round(battery_voltage * 100.0) / 100.0)
    );

    digitalWrite(LED_BLUE, LOW);

    delay(5000);
}

