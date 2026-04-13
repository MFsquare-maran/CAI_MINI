#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"

class BME680_Sensor {
public:
    BME680_Sensor(uint8_t addr = 0x76);
    bool begin();
    bool readSensor();
    float getTemperature() const;
    float getPressure() const;
    float getHumidity() const;
    float getGasResistance() const;
    void set_offset(float temperature,float pressure,float huminity,float gas);
    void enable();
    void disable();
    bool isEnabled();

private:
    Adafruit_BME680 _bme;
    uint8_t _address;
    bool _enabled = true;

    float temperature_Offset = 0.0;
    float pressure_Offset = 0.0;
    float humidity_Offset = 0.0;
    float gas_resistance_Offset = 0.0;
};
