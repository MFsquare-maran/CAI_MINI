/*
 * ============================================================
 *  CAI_MINI — WLAN (REFactored like WIND)
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "config_WLAN.h"
#include "time.h"
#include "BME680_Sensor.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "FirmwareUpdater.h"
#include <IniFile.h>

#include "wifi_functions.h"
#include "time_functions.h"
#include "sdcard.h"
#include "battery.h"

#include "log.h"

// ============================================================
// Konstanten
// ============================================================
constexpr uint32_t MAX_MESSAGE_SIZE  = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

// ============================================================
// Netzwerk
// ============================================================
char ssid[64];
char password[64];
char thingsboardServer[64];
char accessToken[64];
uint16_t THINGSBOARD_PORT;

// ============================================================
// Sensor / Daten
// ============================================================
BME680_Sensor bme;
Battery battery;

float temperature_offset = 0.0;
float Pressure_offset    = 0.0;
float Huminity_offset    = 0.0;
float Gas_offset         = 0.0;

// ============================================================
// Zeit
// ============================================================
struct tm timeinfo;
char datetime[30];

// ============================================================
// SD + Logging
// ============================================================
SDCard  sdcard;
LogEntry data;

// ============================================================
// MQTT / TB
// ============================================================
WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoardSized<32, 10> tb(mqttClient, MAX_MESSAGE_SIZE);

// ============================================================
// Sonstiges
// ============================================================
FirmwareUpdater updater;
IniFile ini("/INIT.ini", FILE_READ, true);

// ============================================================
// Shutdown Funktion (wieder integriert)
// ============================================================
void system_shutdown() {
    logln("System shutdown.");
    Serial.flush();
    delay(1000);

    pinMode(SHUTDOWN_PIN, OUTPUT);
    delay(200);
    digitalWrite(SHUTDOWN_PIN, LOW);
    delay(50);
    digitalWrite(SHUTDOWN_PIN, HIGH);
}

// ============================================================
// ThingsBoard
// ============================================================
void InitTB() {
    logf("Connecting to: ");
    logf(sdcard.cfg.thingsboardServer);
    logf(" with token ");
    logln(sdcard.cfg.accessToken);

    if (!tb.connect(sdcard.cfg.thingsboardServer, sdcard.cfg.accessToken, sdcard.cfg.THINGSBOARD_PORT)) {
        logln("Failed to connect to ThingsBoard");
    } else {
        logln("Connected to ThingsBoard");
    }
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(SERIAL_DEBUG_BAUD);
    delay(5000);

    logln("╔══════════════════════════════╗");
    logln("║   CAI_MINI WLAN              ║");
    logln("╚══════════════════════════════╝");
    logf("Firmware Version: ");
    logln(FW_VERSION);

    // --- Pins ---
    pinMode(LED_BLUE, OUTPUT);
    pinMode(LED_ORANGE, OUTPUT);
    pinMode(SHUTDOWN_PIN, OUTPUT);

    digitalWrite(LED_BLUE, LOW);
    digitalWrite(LED_ORANGE, HIGH);
    digitalWrite(SHUTDOWN_PIN, LOW);

    // --- SD ---
    sdcard.init(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    sdcard.readIni("/INIT.ini");

    // --- Sensor ---
    bme.begin();
    bme.set_offset(temperature_offset, Pressure_offset, Huminity_offset, Gas_offset);

    // --- Battery ---
    battery.begin(BATTERY_VOLTAGE);

    digitalWrite(LED_BLUE, HIGH);

    // ============================================================
    // WiFi
    // ============================================================
    if (InitWiFi(sdcard.cfg.ssid, sdcard.cfg.password) == false) {
        logln("WiFi failed -> shutdown");
        delay(300);
        system_shutdown();
        return;
    }

    // ============================================================
    // Zeit
    // ============================================================
    LocalTime(datetime, &timeinfo);
    data.datetime = datetime;

    // ============================================================
    // Firmware Update
    // ============================================================
    logln("\n🔧 Checking for firmware updates...");
    updater.checkAndUpdate(thingsboardServer, accessToken, FW_VERSION, 1);

    // ============================================================
    // Sensor lesen
    // ============================================================
    bme.enable();

    if (bme.readSensor()) {
        data.temperature     = bme.getTemperature();
        data.pressure        = bme.getPressure();
        data.humidity        = bme.getHumidity();
        data.gas_resistance  = bme.getGasResistance();
        data.battery_voltage = battery.getVoltage();

        logln("------------------------------------");
        logf("Temperature = "); logln(data.temperature);
        logf("Pressure    = "); logln(data.pressure);
        logf("Humidity    = "); logln(data.humidity);
        logf("Gas         = "); logln(data.gas_resistance);
        logf("Battery     = "); logln(data.battery_voltage);
        logln("------------------------------------");
    } else {
        logln("Fehler beim Lesen des BME680 Sensors.");
    }

    bme.disable();

    // ============================================================
    // SD Logging
    // ============================================================
    sdcard.writeLog(data, "data.csv");

    // ============================================================
    // ThingsBoard
    // ============================================================
    InitTB();

    tb.sendAttributeData("rssi", WiFi.RSSI());
    tb.sendAttributeData("channel", WiFi.channel());
    tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
    tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
    tb.sendAttributeData("ssid", WiFi.SSID().c_str());
    tb.sendAttributeData("fwversion", FW_VERSION);

    tb.sendTelemetryData("Temperature",     round(data.temperature * 100.0) / 100.0);
    tb.sendTelemetryData("Pressure",        round(data.pressure * 100.0) / 100.0);
    tb.sendTelemetryData("Humidity",        round(data.humidity * 100.0) / 100.0);
    tb.sendTelemetryData("Gas_Resistance",  round(data.gas_resistance * 100.0) / 100.0);
    tb.sendTelemetryData("Battery_Voltage", round(data.battery_voltage * 100.0) / 100.0);

    tb.loop();
    tb.disconnect();

    disconnectWiFi(&wifiClient);

    digitalWrite(LED_BLUE, LOW);

    // ============================================================
    // Shutdown
    // ============================================================
    delay(300);
    system_shutdown();
}

// ============================================================
// Loop
// ============================================================
void loop() {
    // leer
}