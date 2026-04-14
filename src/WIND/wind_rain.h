#ifndef WIND_RAIN_H
#define WIND_RAIN_H

#include <Arduino.h>
#include "config_WIND.h"

#define GUST_WINDOW_SEC 3
#define MAX_DIR_POINTS 16

class wind_rain {
public:
    wind_rain();

    // ── Initialisierung ─────────────────────────────
    void begin(float wind_direction_offset,
               float wind_speed_offset,
               float rain_offset,
               float device_direction,
               const uint16_t *adc_table);

    // ── Interrupt Control ────────────────────────────
    void enable_interrupts();
    void disable_interrupts();

    // ── Auswertung ───────────────────────────────────
    float get_wind_average();
    float get_wind_gust();
    float get_rain();

    float get_wind_current();
    float get_wind_direction_deg();
    uint16_t get_wind_direction_raw();

    void reset_all();

    // ── ISR counters ────────────────────────────────
    static volatile uint32_t _wind_pulse_count;
    static volatile uint32_t _rain_pulse_count;

private:
    // ── Config ──────────────────────────────────────
    float _wind_direction_offset;
    float _wind_speed_offset;
    float _rain_offset;
    float _device_direction;

    float _wind_factor = 2.4; // Umrechnungsfaktor von Pulsfrequenz zu Windgeschwindigkeit (m/s pro Hz)
    float _rain_factor = 0.2794; // Umrechnungsfaktor von Puls zu Regenmenge (mm pro Puls)

    const float _wind_deg_table[16] = {
    0.0f, 22.5f, 45.0f, 67.5f,
    90.0f, 112.5f, 135.0f, 157.5f,
    180.0f, 202.5f, 225.0f, 247.5f,
    270.0f, 292.5f, 315.0f, 337.5f};

    // ── ADC calibration ─────────────────────────────
    const uint16_t *_adc_table;
    uint8_t _n_points = 16;

    // ── Interrupt timing ────────────────────────────
    static volatile uint32_t _last_wind_pulse_time;
    static volatile uint32_t _min_pulse_interval;

    static volatile uint32_t _last_wind_count_time;
    static volatile uint32_t _wind_last_count;

    // ── Control flag ────────────────────────────────
    static volatile bool _interrupts_enabled;

    // ── wind calc ────────────────────────────────────
    float _wind_current;
    float _wind_sum;
    uint32_t _wind_sample_count;

    // ── internals ────────────────────────────────────
    float _calc_wind_speed();
    float _calc_gust();
    float _calc_direction(int adc);

    // ── IMPORTANT FIX: ISR access ────────────────────
    friend void IRAM_ATTR isr_wind_speed();
    friend void IRAM_ATTR isr_rain_gauge();
};

// ── ISR declarations ────────────────────────────────
void IRAM_ATTR isr_wind_speed();
void IRAM_ATTR isr_rain_gauge();

#endif