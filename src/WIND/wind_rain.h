#ifndef WIND_RAIN_H
#define WIND_RAIN_H

#include <Arduino.h>


// 1. Wind böe stärke --> timer zeit messen --> kleinste zeit = höchste böe
// 2. Wind stärke --> Anzahl Impulse pro Zeiteinheit (z.B. 10min) zählen

// 10 min messen dann interupt ausschalten und werte nach iot Platform  senden 


// Böen-Fenster in Sekunden
#define GUST_WINDOW_SEC 3

class wind_rain {
public:
    // ── Konstruktor ───────────────────────────────────────────────────────────
    wind_rain(float wind_direction_offset, float wind_speed_offset,
              float rain_offset, float device_direction);

    // ── Hauptfunktion: jede Sekunde aufrufen ──────────────────────────────────
    void  update();

    // ── Auslesen: alle 10 Minuten ─────────────────────────────────────────────
    float get_wind_average();       // Durchschnittlicher Dauerwind (km/h)
    float get_gust_average();       // Durchschnittliche Böe        (km/h)
    float get_rain();               // Regenmenge seit letztem Reset (mm)

    // ── Sofortwerte: jederzeit lesbar ─────────────────────────────────────────
    float get_wind_current();       // Letzter Sekunden-Messwert    (km/h)
    float get_wind_direction_deg(); // Aktuelle Windrichtung        (°)

    // ── Reset: nach dem 10min Auslesen aufrufen ───────────────────────────────
    void  reset_all();

    // ── ISR Zähler (müssen public & static sein) ──────────────────────────────
    static volatile uint32_t _wind_pulse_count;
    static volatile uint32_t _rain_pulse_count;

private:
    // ── Konfiguration ─────────────────────────────────────────────────────────
    float _wind_direction_offset;
    float _wind_speed_offset;
    float _rain_offset;
    float _device_direction;

    // ── Interrupt Hilfsvariablen ──────────────────────────────────────────────
    static volatile uint32_t _wind_last_count;
    static volatile uint32_t _wind_last_time_ms;

    // ── Aktueller Messwert ────────────────────────────────────────────────────
    float _wind_current;

    // ── Dauerwind Akkumulator ─────────────────────────────────────────────────
    float    _wind_sum;
    uint32_t _wind_sample_count;

    // ── Böen: 3s Ringpuffer ───────────────────────────────────────────────────
    float   _gust_window[GUST_WINDOW_SEC];
    uint8_t _gust_window_index;

    // ── Böen Akkumulator ──────────────────────────────────────────────────────
    float    _gust_sum;
    uint32_t _gust_sample_count;

    // ── Private Hilfsfunktionen ───────────────────────────────────────────────
    float _measure_wind_speed();
    float _get_current_gust();
    float calculate_wind_direction_deg(int sensorValue);
};

// ── ISR Prototypen ────────────────────────────────────────────────────────────
void IRAM_ATTR isr_wind_speed();
void IRAM_ATTR isr_rain_gauge();

#endif // WIND_RAIN_H
