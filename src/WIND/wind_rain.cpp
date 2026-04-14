#include "wind_rain.h"

// ── statics ─────────────────────────────────────────────
volatile uint32_t wind_rain::_wind_pulse_count = 0;
volatile uint32_t wind_rain::_rain_pulse_count = 0;

volatile uint32_t wind_rain::_last_wind_pulse_time = 0;
volatile uint32_t wind_rain::_min_pulse_interval = 0xFFFFFFFF;

volatile uint32_t wind_rain::_last_wind_count_time = 0;
volatile uint32_t wind_rain::_wind_last_count = 0;

volatile bool wind_rain::_interrupts_enabled = true;

// ── ISRs ────────────────────────────────────────────────
void IRAM_ATTR isr_wind_speed() {
    if (!wind_rain::_interrupts_enabled) return;

    uint32_t now = micros();

    wind_rain::_wind_pulse_count++;

    uint32_t dt = now - wind_rain::_last_wind_pulse_time;

    if (wind_rain::_last_wind_pulse_time != 0) {
        if (dt < wind_rain::_min_pulse_interval) {
            wind_rain::_min_pulse_interval = dt;
        }
    }

    wind_rain::_last_wind_pulse_time = now;
}

void IRAM_ATTR isr_rain_gauge() {
    if (!wind_rain::_interrupts_enabled) return;
    wind_rain::_rain_pulse_count++;
}

// ── constructor ─────────────────────────────────────────
wind_rain::wind_rain()
: _wind_direction_offset(0),
  _wind_speed_offset(0),
  _rain_offset(0),
  _device_direction(0),
  _adc_table(nullptr),
  _n_points(0),
  _wind_current(0),
  _wind_sum(0),
  _wind_sample_count(0)
{}

// ── begin ───────────────────────────────────────────────
void wind_rain::begin(float wind_direction_offset,
                       float wind_speed_offset,
                       float rain_offset,
                       float device_direction,
                       const uint16_t *adc_table)
{
    _wind_direction_offset = wind_direction_offset;
    _wind_speed_offset = wind_speed_offset;
    _rain_offset = rain_offset;
    _device_direction = device_direction;

    _adc_table = adc_table;
    

    pinMode(WIND_VANE, INPUT);
    pinMode(WIND_SPEED, INPUT_PULLDOWN);
    pinMode(RAIN_GAUGE, INPUT);

    attachInterrupt(digitalPinToInterrupt(WIND_SPEED), isr_wind_speed, RISING);
    attachInterrupt(digitalPinToInterrupt(RAIN_GAUGE), isr_rain_gauge, RISING);

    _wind_pulse_count = 0;
    _rain_pulse_count = 0;
    _min_pulse_interval = 0xFFFFFFFF;
    _last_wind_pulse_time = micros();
}

// ── interrupt control ───────────────────────────────────
void wind_rain::enable_interrupts() {
    _interrupts_enabled = true;
}

void wind_rain::disable_interrupts() {
    _interrupts_enabled = false;
}

// ── wind speed average ──────────────────────────────────
float wind_rain::_calc_wind_speed() {
    uint32_t now = millis();

    uint32_t count = _wind_pulse_count;
    uint32_t dt = now - _last_wind_count_time;

    _last_wind_count_time = now;
    _wind_last_count = count;

    if (dt == 0) return 0;

    float hz = (float)count / ((float)dt / 1000.0f);
    return hz * 2.4f + _wind_speed_offset;
}

// ── gust = min pulse interval ───────────────────────────
float wind_rain::_calc_gust() {
    if (_min_pulse_interval == 0xFFFFFFFF) return 0;

    float seconds = _min_pulse_interval / 1000000.0f;

    if (seconds <= 0) return 0;

    float speed = (1.0f / seconds) * 2.4f;

    return speed + _wind_speed_offset;
}

// ── wind direction ──────────────────────────────────────
float wind_rain::_calc_direction(int adc) {

    for (uint8_t i = 0; i < _n_points; i++) {
        if (abs((int)_adc_table[i] - adc) < 20) {
            float deg = _wind_deg_table[i] + _wind_direction_offset;
            return deg;
        }
    }
    return -1;
}

// ── public getters ──────────────────────────────────────
float wind_rain::get_wind_average() {
    return _calc_wind_speed();
}

float wind_rain::get_wind_gust() {
    return _calc_gust();
}

float wind_rain::get_rain() {
    noInterrupts();
    uint32_t c = _rain_pulse_count;
    interrupts();

    return c * 0.2794f + _rain_offset;
}

float wind_rain::get_wind_current() {
    return _calc_wind_speed();
}

float wind_rain::get_wind_direction_deg() {
    return _calc_direction(analogRead(WIND_VANE));
}

uint16_t wind_rain::get_wind_direction_raw() {
    return analogRead(WIND_VANE);
}

// ── reset ───────────────────────────────────────────────
void wind_rain::reset_all() {
    noInterrupts();
    _wind_pulse_count = 0;
    _rain_pulse_count = 0;
    interrupts();

    _min_pulse_interval = 0xFFFFFFFF;
}

