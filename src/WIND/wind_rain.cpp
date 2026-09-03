#include "wind_rain.h"

// ── Debounce-Schwellen ──────────────────────────────────
#define WIND_DEBOUNCE_US   10000UL   // Cap ~240 m/s -> killt Prell-Artefakte
#define RAIN_DEBOUNCE_US  100000UL   // Cap ~10 Kippungen/s

// ── Plausibilitäts-Grenzen (CH-Extreme mit Reserve) ─────
#define GUST_MAX_MS         75.0f    // = 270 km/h, deckt Extremböen ab
#define RAIN_MAX_MM         60.0f    // > CH-Rekord 41 mm/10min, mit Reserve

// ── statics ─────────────────────────────────────────────
volatile uint32_t wind_rain::_wind_pulse_count = 0;
volatile uint32_t wind_rain::_rain_pulse_count = 0;

volatile uint32_t wind_rain::_last_wind_pulse_time = 0;
volatile uint32_t wind_rain::_min_pulse_interval = 0xFFFFFFFF;

volatile uint32_t wind_rain::_last_wind_count_time = 0;
volatile uint32_t wind_rain::_wind_last_count = 0;

volatile uint32_t wind_rain::_last_rain_pulse_time = 0;   // NEU

volatile bool wind_rain::_interrupts_enabled = true;

// ── ISRs ────────────────────────────────────────────────
void IRAM_ATTR isr_wind_speed() {
    if (!wind_rain::_interrupts_enabled) return;

    uint32_t now = micros();
    uint32_t dt  = now - wind_rain::_last_wind_pulse_time;

    // Preller verwerfen: Flanke ignorieren, Zeitbasis NICHT verschieben
    if (wind_rain::_last_wind_pulse_time != 0 && dt < WIND_DEBOUNCE_US) return;

    wind_rain::_wind_pulse_count++;

    if (wind_rain::_last_wind_pulse_time != 0) {
        if (dt < wind_rain::_min_pulse_interval) {
            wind_rain::_min_pulse_interval = dt;
        }
    }

    wind_rain::_last_wind_pulse_time = now;
}

void IRAM_ATTR isr_rain_gauge() {
    if (!wind_rain::_interrupts_enabled) return;

    uint32_t now = micros();

    // Preller verwerfen
    if (wind_rain::_last_rain_pulse_time != 0 &&
        (now - wind_rain::_last_rain_pulse_time) < RAIN_DEBOUNCE_US) return;

    wind_rain::_rain_pulse_count++;
    wind_rain::_last_rain_pulse_time = now;
}

// ── constructor ─────────────────────────────────────────
wind_rain::wind_rain()
: _wind_direction_offset(0),
  _wind_speed_offset(0),
  _rain_offset(0),
  _device_direction(0),
  _adc_table(nullptr),
  _n_points(16),
  _wind_current(0),
  _wind_sum(0)
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
    pinMode(RAIN_GAUGE, INPUT_PULLDOWN);

    attachInterrupt(digitalPinToInterrupt(WIND_SPEED), isr_wind_speed, RISING);
    attachInterrupt(digitalPinToInterrupt(RAIN_GAUGE), isr_rain_gauge, RISING);

    _wind_pulse_count = 0;
    _rain_pulse_count = 0;
    _min_pulse_interval = 0xFFFFFFFF;
    _last_wind_pulse_time = micros();
    _last_rain_pulse_time = 0;   // NEU
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
    return hz * _wind_factor + _wind_speed_offset;
}

// ── gust = min pulse interval ───────────────────────────
float wind_rain::_calc_gust() {
    if (_min_pulse_interval == 0xFFFFFFFF) return 0;

    float seconds = _min_pulse_interval / 1000000.0f;

    if (seconds <= 0) return 0;

    float speed = (1.0f / seconds) * _wind_factor + _wind_speed_offset;

    if (speed > GUST_MAX_MS) return 0;   // Clamp: unplausibel -> 0 senden

    return speed;
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

uint32_t wind_rain::get_wind_count()
{
    return _wind_pulse_count;
}


float wind_rain::get_rain() {
    noInterrupts();
    uint32_t c = _rain_pulse_count;
    interrupts();

    float mm = c * _rain_factor + _rain_offset;

    if (mm > RAIN_MAX_MM) return 0;   // Clamp: unplausibel -> 0 senden

    return mm;
}

float wind_rain::get_wind_current() {
    return _calc_wind_speed();
}

uint32_t wind_rain::get_rain_count()
{
    return _rain_pulse_count;
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