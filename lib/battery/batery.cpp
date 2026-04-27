#include "battery.h"
#include <Arduino.h>

Battery::Battery(){}


Battery::~Battery(){}

void Battery::begin(uint8_t pin) 
{
  voltage_pin = pin;
}


float Battery::getVoltage()
{
    uint16_t adc = (uint16_t)analogRead(voltage_pin);

    // Interpolation
    for (uint8_t i = 0; i < N - 1; i++)
    {
        if (adc >= adcVals[i] && adc <= adcVals[i + 1])
        {
            float t = (float)(adc - adcVals[i]) /
                      (float)(adcVals[i + 1] - adcVals[i]);

            voltage = voltVals[i] + t * (voltVals[i + 1] - voltVals[i]);
            return voltage; 
        }
    }

    // Extrapolation unten
    if (adc < adcVals[0])
    {
        float t = (float)(adc - adcVals[0]) /
                  (float)(adcVals[1] - adcVals[0]);

        voltage = voltVals[0] + t * (voltVals[1] - voltVals[0]);
        return voltage;
    }

    // Extrapolation oben
    float t = (float)(adc - adcVals[N - 2]) /
              (float)(adcVals[N - 1] - adcVals[N - 2]);

    voltage = voltVals[N - 2] + t * (voltVals[N - 1] - voltVals[N - 2]);
    return voltage;
}

uint8_t Battery::getPercentage()
{
    float v = getVoltage();

    float p = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;

    // Begrenzen auf 0–100%
    if (p < 0.0f)   p = 0.0f;
    if (p > 100.0f) p = 100.0f;

    percentage = (uint8_t)p;
    return percentage;
}