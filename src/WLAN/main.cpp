/*
 * ============================================================
 *  CAI_MINI — WLAN
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include "ha_mqtt.h"
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "config_WLAN.h"
#include "pins_WLAN.h"
#include "time.h"
#include "BME680_Sensor.h"
#include <math.h>
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "FirmwareUpdater.h"
#include <IniFile.h>

#include "wifi_functions.h"
#include "time_functions.h"
#include "sdcard.h"
#include "battery.h"


#include "esp_task_wdt.h"

// ============================================================
// Konstanten
// ============================================================
constexpr uint32_t MAX_MESSAGE_SIZE  = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;
constexpr uint32_t WDT_TIMEOUT_S     = 180U;   // Watchdog: Reset nach 3 min

// ============================================================
// Netzwerk
// ============================================================
char ssid[64];
char password[64];
char thingsboardServer[64];
char accessToken[64];
uint16_t THINGSBOARD_PORT;

// ============================================================
// HOME ASSISTANT
// ============================================================
HA_MQTT ha_mqtt;

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
// Shutdown Funktion
// ============================================================
void system_shutdown() {
    Serial.println("System shutdown.");

    #if HW_VERSION == 1

            delay(1000);
            pinMode(SHUTDOWN_PIN, OUTPUT);
            delay(200);
            digitalWrite(SHUTDOWN_PIN, LOW);
            delay(50);
            digitalWrite(SHUTDOWN_PIN, HIGH);

    #endif

    #if HW_VERSION == 2
    
        delay(1000);
        Serial.println("Deep Sleep für 10 Minuten.");
        Serial.flush();                          // Log noch rausschreiben, bevor CPU schläft
        esp_sleep_enable_timer_wakeup(CYCLE_TIME_MIN*60ULL * 1000000ULL);  // 10 min in µs

        // Button: aufwachen, wenn ON_BUTTON auf den aktiven Pegel geht
        esp_sleep_enable_ext0_wakeup((gpio_num_t)ON_BUTTON, 1);  // 0 = LOW aktiv, 1 = HIGH aktiv

        // Ruhepegel im Sleep halten, sonst floatet der Pin und weckt zufällig
        rtc_gpio_pullup_en((gpio_num_t)ON_BUTTON);       // bei aktiv-LOW (Taster gegen GND)
        // rtc_gpio_pulldown_en((gpio_num_t)ON_BUTTON);   // bei aktiv-HIGH (Taster gegen 3V3)


        esp_deep_sleep_start();                  // kehrt nie zurück – Neustart via setup()

    #endif


}

// ============================================================
// ThingsBoard
// ============================================================
bool InitTB() {
    logf("Connecting to: "); logf(sdcard.cfg.thingsboardServer);
    logf(" with token ");    logln(sdcard.cfg.accessToken);

    if (!tb.connect(sdcard.cfg.thingsboardServer, sdcard.cfg.accessToken, sdcard.cfg.THINGSBOARD_PORT)) {
        logln("Failed to connect to ThingsBoard");
        return false;
    }
    logln("Connected to ThingsBoard");
    return true;
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(SERIAL_DEBUG_BAUD);
    delay(5000);

    // --- Watchdog: Reset nach WDT_TIMEOUT_S falls Ablauf hängt ---
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    _log_mutex = xSemaphoreCreateMutex();

    logln("╔══════════════════════════════╗");
    logln("║   CAI_MINI WLAN              ║");
    logln("╚══════════════════════════════╝");
    logf("Firmware Version: ");
    logln(FW_VERSION);

    // --- Pins ---
    pinMode(LED_BLUE, OUTPUT);
    pinMode(LED_ORANGE, OUTPUT);

    


    #if HW_VERSION == 1
        pinMode(SHUTDOWN_PIN, OUTPUT);
        digitalWrite(SHUTDOWN_PIN, LOW);
    #endif

    #if HW_VERSION == 2
        pinMode(LORA_ENABLE, OUTPUT);
        digitalWrite(LORA_ENABLE, LOW);
        pinMode(BATTERY_CHARGING, INPUT);
    #endif
    

    digitalWrite(LED_BLUE, LOW);
    digitalWrite(LED_ORANGE, HIGH);
    

    // --- SD ---
    sdcard.init(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    sdcard.readIni("/INIT.ini");

    // --- Sensor ---
    bme.begin();
    bme.set_offset(sdcard.cfg.temperature_offset, sdcard.cfg.Pressure_offset, sdcard.cfg.Huminity_offset, sdcard.cfg.Gas_offset);

    // --- Battery ---
    battery.begin(BATTERY_VOLTAGE);

    digitalWrite(LED_BLUE, HIGH);

    // ============================================================
    // WiFi
    // ============================================================
    if (InitWiFi(sdcard.cfg.ssid, sdcard.cfg.password) == false) {
        logln("WiFi failed -> shutdown");
        delay(300);
        while(true) {
            system_shutdown();
            digitalWrite(LED_BLUE, HIGH);
            delay(3);
            digitalWrite(LED_BLUE, LOW);
            delay(1000);
        }
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
    updater.checkAndUpdate(sdcard.cfg.thingsboardServer, sdcard.cfg.accessToken, FW_VERSION, 1);

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
        #if HW_VERSION == 2
            logf("Charging    = "); logln(!digitalRead(BATTERY_CHARGING));
        #endif
        logln("------------------------------------");
    } else {
        logln("Fehler beim Lesen des BME680 Sensors.");
    }

    bme.disable();

    // ============================================================
    // SD Logging
    // ============================================================
    sdcard.writeLog(data, "/data.csv");

    // Battery Percentage berechnen (vor TB, damit HA-Block ihn auch bei TB-Fehler hat)
    float battery_pct = constrain((data.battery_voltage - 3.0f) / 1.2f * 100.0f, 0.0f, 100.0f);

    // ============================================================
    // ThingsBoard
    // ============================================================
    if (InitTB()) {
        tb.sendAttributeData("rssi",      WiFi.RSSI());
        tb.sendAttributeData("channel",   WiFi.channel());
        tb.sendAttributeData("bssid",     WiFi.BSSIDstr().c_str());
        tb.sendAttributeData("localIp",   WiFi.localIP().toString().c_str());
        tb.sendAttributeData("ssid",      WiFi.SSID().c_str());
        tb.sendAttributeData("fwversion", FW_VERSION);

        tb.sendTelemetryData("Temperature",        round(data.temperature * 100.0) / 100.0);
        tb.sendTelemetryData("Pressure",           round(data.pressure * 100.0) / 100.0);
        tb.sendTelemetryData("Humidity",           round(data.humidity * 100.0) / 100.0);
        tb.sendTelemetryData("Gas_Resistance",     round(data.gas_resistance * 100.0) / 100.0);
        tb.sendTelemetryData("Battery_Voltage",    round(data.battery_voltage * 100.0) / 100.0);
        tb.sendTelemetryData("Battery_Percentage", round(battery_pct * 100.0f) / 100.0f);

        #if HW_VERSION == 2
            tb.sendTelemetryData("Battery_Charging",   !digitalRead(BATTERY_CHARGING) );
        #endif

        // FLUSH: Puffer rausschreiben lassen, bevor getrennt/abgeschaltet wird
        logln("TB Daten senden ...");
        delay(1000);
        
        tb.disconnect();
    } else {
        logln("TB skip – nicht verbunden");
    }

    // ============================================================
    // Home Assistant MQTT
    // ============================================================
    if (sdcard.cfg.ha_enabled) {
        if (ha_mqtt.connect(sdcard.cfg.ha_broker, sdcard.cfg.ha_port, sdcard.cfg.ha_user, sdcard.cfg.ha_pass)) {
            ha_mqtt.publishDiscovery(sdcard.cfg.ha_device_id);
            delay(300);
            ha_mqtt.publishState(sdcard.cfg.ha_device_id, data.temperature, data.pressure, data.humidity, data.gas_resistance, data.battery_voltage, battery_pct);
            ha_mqtt.disconnect();
        }
    }

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
    delay(500);
    digitalWrite(LED_BLUE, HIGH);
    delay(3);
    digitalWrite(LED_BLUE, LOW);
    system_shutdown();
}