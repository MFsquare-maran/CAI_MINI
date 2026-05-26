#pragma once
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern unsigned long _ntp_millis_anchor;
extern time_t        _ntp_time_anchor;
extern SemaphoreHandle_t _log_mutex;

void _telnetLog(const char* msg, bool newline);

#ifdef LOG_TELNET
  #include <WiFi.h>

  #define logln(x) do { \
    String _s = String(x); \
    if (_log_mutex && xSemaphoreTake(_log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) { \
      Serial.println(_s); \
      if (WiFi.status() == WL_CONNECTED) \
        _telnetLog(_s.c_str(), true); \
      xSemaphoreGive(_log_mutex); \
    } \
  } while(0)

  #define logf(x) do { \
    String _s = String(x); \
    if (_log_mutex && xSemaphoreTake(_log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) { \
      Serial.print(_s); \
      if (WiFi.status() == WL_CONNECTED) \
        _telnetLog(_s.c_str(), false); \
      xSemaphoreGive(_log_mutex); \
    } \
  } while(0)

#else
  #define logln(x) do { \
    if (_log_mutex && xSemaphoreTake(_log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) { \
      Serial.println(x); \
      xSemaphoreGive(_log_mutex); \
    } \
  } while(0)

  #define logf(x) do { \
    if (_log_mutex && xSemaphoreTake(_log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) { \
      Serial.print(x); \
      xSemaphoreGive(_log_mutex); \
    } \
  } while(0)
#endif