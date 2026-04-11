#include "wind_rain.h"
#include "config_WIND.h"

// ── Statische Member ──────────────────────────────────────────────────────────
volatile uint32_t wind_rain::_wind_pulse_count  = 0;
volatile uint32_t wind_rain::_rain_pulse_count  = 0;
volatile uint32_t wind_rain::_wind_last_count   = 0;
volatile uint32_t wind_rain::_wind_last_time_ms = 0;

// ── ISR ───────────────────────────────────────────────────────────────────────
void IRAM_ATTR isr_wind_speed() { wind_rain::_wind_pulse_count++; }
void IRAM_ATTR isr_rain_gauge() { wind_rain::_rain_pulse_count++; }

// ── Konstruktor ───────────────────────────────────────────────────────────────
wind_rain::wind_rain(float wind_direction_offset, float wind_speed_offset,
                     float rain_offset, float device_direction)
    : _wind_direction_offset(wind_direction_offset),
      _wind_speed_offset(wind_speed_offset),
      _rain_offset(rain_offset),
      _device_direction(device_direction),
      _wind_current(0.0f),
      _wind_sum(0.0f),
      _wind_sample_count(0),
      _gust_window_index(0),
      _gust_sum(0.0f),
      _gust_sample_count(0)
{
    pinMode(WIND_VANE,  INPUT);
    pinMode(WIND_SPEED, INPUT_PULLDOWN);
    pinMode(RAIN_GAUGE, INPUT);

    attachInterrupt(digitalPinToInterrupt(WIND_SPEED), isr_wind_speed, RISING);
    attachInterrupt(digitalPinToInterrupt(RAIN_GAUGE), isr_rain_gauge, RISING);

    // Böen-Ringpuffer initialisieren
    for (uint8_t i = 0; i < GUST_WINDOW_SEC; i++) {
        _gust_window[i] = 0.0f;
    }

    _wind_last_count   = 0;
    _wind_last_time_ms = millis();
}

// ── update() — jede Sekunde aufrufen ─────────────────────────────────────────
void wind_rain::update() {

    // 1. Windgeschwindigkeit der letzten Sekunde messen
    _wind_current = _measure_wind_speed();

    // 2. Dauerwind Akkumulator
    _wind_sum += _wind_current;
    _wind_sample_count++;

    // 3. Ringpuffer befüllen (3s gleitendes Fenster)
    _gust_window[_gust_window_index] = _wind_current;
    _gust_window_index = (_gust_window_index + 1) % GUST_WINDOW_SEC;

    // 4. Böe (Max im Ringpuffer) in Akkumulator addieren
    _gust_sum += _get_current_gust();
    _gust_sample_count++;
}

// ── get_wind_average() — Durchschnittlicher Dauerwind ────────────────────────
float wind_rain::get_wind_average() {
    if (_wind_sample_count == 0) return 0.0f;
    return _wind_sum / (float)_wind_sample_count;
}

// ── get_gust_average() — Durchschnittliche Böe ───────────────────────────────
float wind_rain::get_gust_average() {
    if (_gust_sample_count == 0) return 0.0f;
    return _gust_sum / (float)_gust_sample_count;
}

// ── get_rain() ────────────────────────────────────────────────────────────────
float wind_rain::get_rain() {
    noInterrupts();
    uint32_t count = _rain_pulse_count;
    interrupts();
    return (float)count * 0.2794f + _rain_offset;
}

// ── get_wind_current() — Sofortwert ──────────────────────────────────────────
float wind_rain::get_wind_current() {
    return _wind_current;
}

// ── get_wind_direction_deg() ──────────────────────────────────────────────────
float wind_rain::get_wind_direction_deg() {
    float deg = calculate_wind_direction_deg(analogRead(WIND_VANE));
    if (deg < 0.0f) return -1.0f; // ungültiger Sensorwert
    deg += _wind_direction_offset;
    if (deg >= 360.0f) deg -= 360.0f;
    if (deg <    0.0f) deg += 360.0f;
    return deg;
}

// ── reset_all() — nach dem 10min Auslesen aufrufen ───────────────────────────
void wind_rain::reset_all() {
    // Dauerwind zurücksetzen
    _wind_sum          = 0.0f;
    _wind_sample_count = 0;

    // Böen zurücksetzen
    _gust_sum          = 0.0f;
    _gust_sample_count = 0;
    for (uint8_t i = 0; i < GUST_WINDOW_SEC; i++) {
        _gust_window[i] = 0.0f;
    }

    // Regen zurücksetzen
    noInterrupts();
    _rain_pulse_count = 0;
    interrupts();
}

// ── Private: Windmessung per Interrupt-Zähler ─────────────────────────────────
float wind_rain::_measure_wind_speed() {
    noInterrupts();
    uint32_t current_count = _wind_pulse_count;
    interrupts();

    uint32_t current_time_ms = millis();
    uint32_t delta_pulses    = current_count   - _wind_last_count;
    uint32_t delta_ms        = current_time_ms - _wind_last_time_ms;

    _wind_last_count   = current_count;
    _wind_last_time_ms = current_time_ms;

    if (delta_ms == 0) return 0.0f;

    // delta_ms statt fester 1000ms → korrigiert automatisch WiFi-Verzögerungen!
    float hz    = (float)delta_pulses / ((float)delta_ms / 1000.0f);
    float speed = hz * 2.4f + _wind_speed_offset;

    return (speed < 0.0f) ? 0.0f : speed;
}

// ── Private: Böe = Maximum im 3s Ringpuffer ───────────────────────────────────
float wind_rain::_get_current_gust() {
    float max_val = 0.0f;
    for (uint8_t i = 0; i < GUST_WINDOW_SEC; i++) {
        if (_gust_window[i] > max_val) {
            max_val = _gust_window[i];
        }
    }
    return max_val;
}

// ── Private: Windrichtung berechnen ───────────────────────────────────────────
float wind_rain::calculate_wind_direction_deg(int sensorValue) {
    const uint8_t  N_POINTS = 16;
    const float    deg[N_POINTS]     = { 
        0.0f, 22.5f,  45.0f,  67.5f,
        90.0f, 112.5f, 135.0f, 157.5f,
        180.0f, 202.5f, 225.0f, 247.5f,
        270.0f, 292.5f, 315.0f, 337.5f };

    const uint16_t adcVals[3][N_POINTS] = {
        { 3143, 1624, 1845,  335,
        372,  264,  739,  506,
        1149,  979, 2521, 2398,
        3781, 3310, 3549, 2811 },

        { 3227, 1735, 2122,  354,
        439,  299,  859,  622,
        1387, 1064, 2666, 2459,
        4095, 3430, 3665, 2977 },

        { 2976, 1386, 1734,  298,
        353,    0,  621,  438,
        1063,  858, 2458, 2121,
        3664, 3226, 3429, 2665 }
    };

    const uint16_t tolerance = 50;

    for (uint8_t i = 0; i < N_POINTS; i++) {
        if ((uint16_t)sensorValue <= adcVals[1][i]  &&
            (uint16_t)sensorValue >= adcVals[2][i] ) {
            return deg[i];
        }
    }
    return -1.0f; // kein passender Wert gefunden
}
