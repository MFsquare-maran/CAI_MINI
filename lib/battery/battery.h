#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include "log.h"

class Battery {
public:
    Battery();
    ~Battery();
    void begin(uint8_t pin);
    float getVoltage();
    uint8_t getPercentage();
   

private:
    uint8_t voltage_pin;
    const uint8_t  N = 8;
    const float    voltVals[8] = { 3.5, 3.6, 3.7, 3.8, 3.9, 4.0, 4.1, 4.2 };
    const uint16_t adcVals[8]  = { 2644, 2720, 2801, 2889, 2972, 3060, 3155, 3249 };
    float voltage;
    uint8_t percentage;
    
};

#endif // BATTERY_H