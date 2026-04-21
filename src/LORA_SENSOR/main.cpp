// ============================================================
//  main.cpp
//  CAI_MINI LoRa Sensor
//  BME680 + SX1262 via RadioLib
//  Datum: 2026-04-16
// ============================================================

#include "config_LORA_SENSOR.h"
#include <Arduino.h>
#include "LORA.h"
#include "BME680_Sensor.h"
#include <SPI.h>
#include <SD.h>
#include <IniFile.h>

// ============================================================
//  Objekte
// ============================================================
SPIClass spi_sd(HSPI);    // SD-Karte  → HSPI Bus
SPIClass spi_lora(FSPI);  // LoRa      → FSPI Bus

BME680_Sensor bme;

LORA Lora_sensor(
    LORA_NSS,
    LORA_DIO1,
    LORA_RESET,
    LORA_BUSY,
    spi_lora,
    LORA_SCK,
    LORA_MISO,
    LORA_MOSI,
    3,      // maxRetries
    500,    // retryDelay  [ms]
    2000    // ackTimeout  [ms]
);

// ============================================================
//  Messwerte
// ============================================================
float temperature    = 0.0f;
float pressure       = 0.0f;
float humidity       = 0.0f;
float gas_resistance = 0.0f;
float battery_voltage = 0.0f;

// ============================================================
//  Konfiguration aus INI
// ============================================================
float temperature_offset = 0.0f;
float Pressure_offset    = 0.0f;
float Huminity_offset    = 0.0f;
float Gas_offset         = 0.0f;

char accessToken[64] = {0};
char DeviceID[64]    = {0};
char SenderID[64]    = {0};

// ============================================================
//  SD-Karte initialisieren
// ============================================================
void InitSD()
{
    spi_sd.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS, spi_sd))
    {
        Serial.println("[SD] FEHLER: SD-Karte konnte nicht initialisiert werden!");
        while (1) { delay(1000); } // blockiert mit 1s Pause statt Spin
    }

    Serial.println("[SD] SD-Karte erfolgreich initialisiert.");
}

// ============================================================
//  INI-Werte lesen
// ============================================================
void get_ini_values()
{
    IniFile ini("/INIT.ini", FILE_READ, true);

    if (!ini.open())
    {
        Serial.println("[INI] FEHLER: INI-Datei konnte nicht geöffnet werden!");
        while (1) { delay(1000); }
    }

    char buffer[128];

    if (ini.getValue("THINGSBOARD", "TB_TOKKEN",          buffer, sizeof(buffer))) strcpy(accessToken,       buffer);
    if (ini.getValue("SEND_TO",     "NAME",               buffer, sizeof(buffer))) strcpy(SenderID,          buffer);
    if (ini.getValue("ID",          "NAME",               buffer, sizeof(buffer))) strcpy(DeviceID,          buffer);
    if (ini.getValue("BME680",      "TEMPERATURE_OFFSET", buffer, sizeof(buffer))) temperature_offset = strtof(buffer, nullptr);
    if (ini.getValue("BME680",      "PRESSURE_OFFSET",    buffer, sizeof(buffer))) Pressure_offset    = strtof(buffer, nullptr);
    if (ini.getValue("BME680",      "HUMINITY_OFFSET",    buffer, sizeof(buffer))) Huminity_offset    = strtof(buffer, nullptr);
    if (ini.getValue("BME680",      "GAS_OFFSET",         buffer, sizeof(buffer))) Gas_offset         = strtof(buffer, nullptr);

    Serial.println("[INI] Werte gelesen:");
    Serial.println("      TB Token:           " + String(accessToken));
    Serial.println("      Sender ID:          " + String(SenderID));
    Serial.println("      Device ID:          " + String(DeviceID));
    Serial.println("      Temp  Offset:       " + String(temperature_offset));
    Serial.println("      Press Offset:       " + String(Pressure_offset));
    Serial.println("      Humi  Offset:       " + String(Huminity_offset));
    Serial.println("      Gas   Offset:       " + String(Gas_offset));

    ini.close();

    // SD nicht mehr nötig → freigeben
    SD.end();
    Serial.println("[SD] SD-Karte freigegeben.");
}

// ============================================================
//  Akkuspannung messen (lineare Interpolation)
// ============================================================
float getBatteryVoltage()
{
    const uint8_t  N          = 8;
    const float    voltVals[] = { 3.5f, 3.6f, 3.7f, 3.8f, 3.9f, 4.0f, 4.1f, 4.2f };
    const uint16_t adcVals[]  = { 2644, 2720, 2801, 2889, 2972, 3060, 3155, 3249  };

    uint16_t adc = (uint16_t)analogRead(BATTERY_VOLTAGE);

    // Bereich suchen
    for (uint8_t i = 0; i < N - 1; i++)
    {
        if (adc >= adcVals[i] && adc <= adcVals[i + 1])
        {
            float t = (float)(adc - adcVals[i]) /
                      (float)(adcVals[i + 1] - adcVals[i]);
            return voltVals[i] + t * (voltVals[i + 1] - voltVals[i]);
        }
    }

    // Extrapolation unten
    if (adc < adcVals[0])
    {
        float t = (float)(adc - adcVals[0]) /
                  (float)(adcVals[1] - adcVals[0]);
        return voltVals[0] + t * (voltVals[1] - voltVals[0]);
    }

    // Extrapolation oben
    float t = (float)(adc - adcVals[N - 2]) /
              (float)(adcVals[N - 1] - adcVals[N - 2]);
    return voltVals[N - 2] + t * (voltVals[N - 1] - voltVals[N - 2]);
}

// ============================================================
//  Sensordaten lesen + ausgeben
// ============================================================
bool readSensors()
{
    if (!bme.readSensor())
    {
        Serial.println("[BME680] FEHLER beim Lesen!");
        return false;
    }

    temperature    = bme.getTemperature();
    pressure       = bme.getPressure();
    humidity       = bme.getHumidity();
    gas_resistance = bme.getGasResistance();
    battery_voltage = getBatteryVoltage();

    Serial.println("[BME680] Messwerte:");
    Serial.println("         Temperatur:    " + String(temperature,    2) + " °C");
    Serial.println("         Luftdruck:     " + String(pressure,       2) + " hPa");
    Serial.println("         Luftfeuchte:   " + String(humidity,       2) + " %");
    Serial.println("         Gaswiderstand: " + String(gas_resistance, 2) + " kOhm");
    Serial.println("         Akku:          " + String(battery_voltage,2) + " V");
    Serial.println("         --------------------------------");

    return true;
}

// ============================================================
//  LoRa-Paket zusammenbauen
// ============================================================
String buildPayload()
{
    return "Token:"           + String(accessToken)                          +
           ";Temperature:"    + String(round(temperature    * 100.0) / 100.0) +
           ";Pressure:"       + String(round(pressure       * 100.0) / 100.0) +
           ";Humidity:"       + String(round(humidity       * 100.0) / 100.0) +
           ";Gas_Resistance:" + String(round(gas_resistance * 100.0) / 100.0) +
           ";Battery_Voltage:"+ String(round(battery_voltage* 100.0) / 100.0);
}

// ============================================================
//  setup()
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("╔══════════════════════════════╗");
    Serial.println("║   CAI_MINI LoRa Sensor       ║");
    Serial.println("╚══════════════════════════════╝");
    Serial.print("Firmware Version: ");
    Serial.println(FW_VERSION);

    // LED
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_BLUE, LOW);

    // SD + INI
    InitSD();
    get_ini_values();

    // BME680
    bme.begin();
    bme.set_offset(temperature_offset,
                   Pressure_offset,
                   Huminity_offset,
                   Gas_offset);

    // LoRa (ISR wird intern in begin() registriert)
    if (!Lora_sensor.begin(DeviceID))
    {
        Serial.println("[LORA] KRITISCH: Initialisierung fehlgeschlagen!");
        while (1) { delay(1000); }
    }

    Serial.println("[SETUP] Bereit. Starte Messzyklus...");
}

// ============================================================
//  loop()
// ============================================================
void loop()
{
    // ── 1. Sensordaten lesen ──────────────────────────────────
    if (!readSensors())
    {
        Serial.println("[LOOP] Sensor Fehler – überspringe Sendung.");
        delay(5000);
        return;
    }

    // ── 2. Senden ─────────────────────────────────────────────
    digitalWrite(LED_BLUE, HIGH);

    bool ok = Lora_sensor.transmit(SenderID, buildPayload());

    digitalWrite(LED_BLUE, LOW);

    if (ok)
    {
        Serial.println("[LOOP] ✅ Paket erfolgreich gesendet.");
        Serial.println("       Gesendet gesamt: " + String(Lora_sensor.getSentCount()));
    }
    else
    {
        Serial.println("[LOOP] ❌ Senden fehlgeschlagen nach allen Versuchen.");
    }

    // ── 3. Warten bis zum nächsten Zyklus ────────────────────
    Serial.println("[LOOP] Warte 5s bis zum nächsten Zyklus...");
    Serial.println("Gute Nacht! 😴");

    Lora_sensor.deepSleepUntilTimer(10*60); // Geht in Deep Sleep, wacht auf bei neuem LoRa-Paket oder nach 10 Minuten (Backup) 
}
