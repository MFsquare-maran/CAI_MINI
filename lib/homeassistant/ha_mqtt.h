#pragma once
#include <WiFiClient.h>
#include <PubSubClient.h>

class HA_MQTT {
public:
    bool connect(const char* broker, uint16_t port,
             const char* user, const char* pass);
    void publishDiscovery(const char* device_id);
    void publishState(const char* device_id, float temp, float pressure,
                  float humidity, float gas, float battery, float battery_pct);
    void disconnect();

private:
    WiFiClient _wifiClient;
    PubSubClient _client{_wifiClient};

    void _publishSensorConfig(const char* device_id,
                              const char* name,
                              const char* unique_id_suffix,
                              const char* unit,
                              const char* device_class,
                              const char* state_topic,
                              const char* value_template);
};