#include "ha_mqtt.h"
#include <ArduinoJson.h>
#include "log.h"

bool HA_MQTT::connect(const char* broker, uint16_t port,
                      const char* user, const char* pass) {
    _client.setServer(broker, port);
    _client.setBufferSize(512, 512);

    if (_client.connect("cai_mini_ha", user, pass)) {
        logln("HA MQTT: connected");
        return true;
    }
    logf("HA MQTT: connect failed, rc=");
    logln(_client.state());
    return false;
}

void HA_MQTT::_publishSensorConfig(const char* device_id,
                                    const char* name,
                                    const char* unique_id_suffix,
                                    const char* unit,
                                    const char* device_class,
                                    const char* state_topic,
                                    const char* value_template) {
    char config_topic[128];
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s/%s/config", device_id, unique_id_suffix);

    DynamicJsonDocument doc(512);

    doc["name"]                 = name;
    doc["unique_id"]            = String(device_id) + "_" + unique_id_suffix;
    doc["state_topic"]          = state_topic;
    doc["value_template"]       = value_template;
    doc["unit_of_measurement"]  = unit;
    if (strlen(device_class) > 0)
        doc["device_class"]     = device_class;

    // Gerät gruppieren in HA
    JsonObject device        = doc.createNestedObject("device");
    device["identifiers"][0] = device_id;
    device["name"]           = device_id;
    device["model"]          = "CAI_MINI_WLAN";
    device["manufacturer"]   = "TFB";

    char payload[512];
    serializeJson(doc, payload, sizeof(payload));

    _client.publish(config_topic, payload, /*retained=*/true);
}

void HA_MQTT::publishDiscovery(const char* device_id) {
    char state_topic[128];
    snprintf(state_topic, sizeof(state_topic), "cai_mini/%s/state", device_id);

    _publishSensorConfig(device_id, "Temperature",       "temperature",
                         "°C",  "temperature",           state_topic,
                         "{{ value_json.temperature | round(2) }}");

    _publishSensorConfig(device_id, "Pressure",        "pressure",
                         "hPa", "atmospheric_pressure",  state_topic,
                         "{{ value_json.pressure | round(2) }}");

    _publishSensorConfig(device_id, "Humidity", "humidity",
                         "%",   "humidity",              state_topic,
                         "{{ value_json.humidity | round(2) }}");

    _publishSensorConfig(device_id, "Gas Resistance",    "gas_resistance",
                         "kΩ",   "",                      state_topic,
                         "{{ value_json.gas_resistance | round(2) }}");

    _publishSensorConfig(device_id, "Battery Voltage", "battery_voltage",
                         "V",   "voltage",               state_topic,
                         "{{ value_json.battery_voltage | round(2) }}");

    _publishSensorConfig(device_id, "Battery Percentage",       "battery_percentage",
                     "%",   "battery",               state_topic,
                     "{{ value_json.battery_percentage | round(2) }}");

    logln("HA MQTT: Discovery publiziert");
}

void HA_MQTT::publishState(const char* device_id, float temp, float pressure,
                           float humidity, float gas, float battery, float battery_pct) {
    char state_topic[128];
    snprintf(state_topic, sizeof(state_topic), "cai_mini/%s/state", device_id);

    DynamicJsonDocument doc(256);

    doc["temperature"]     = (double)round(temp     * 100.0) / 100.0;
    doc["pressure"]        = (double)round(pressure * 100.0) / 100.0;
    doc["humidity"]        = (double)round(humidity * 100.0) / 100.0;
    doc["gas_resistance"]  = (double)round(gas      * 100.0) / 100.0;
    doc["battery_voltage"] = (double)round(battery  * 100.0) / 100.0;
    doc["battery_percentage"] = (double)round(battery_pct * 100.0) / 100.0;

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    _client.publish(state_topic, payload);
    logln("HA MQTT: State publiziert");
}

void HA_MQTT::disconnect() {
    _client.disconnect();
    logln("HA MQTT: disconnected");
}