#pragma once
#include <Arduino.h>

extern unsigned long _ntp_millis_anchor;
extern time_t        _ntp_time_anchor;

void _telnetLog(const char* msg, bool newline);

#ifdef LOG_TELNET
  #include <WiFi.h>

  #define logln(x) do { \
    String _s = String(x); \
    Serial.println(_s); \
    if (WiFi.status() == WL_CONNECTED) \
      _telnetLog(_s.c_str(), true); \
  } while(0)

  #define logf(x) do { \
    String _s = String(x); \
    Serial.print(_s); \
    if (WiFi.status() == WL_CONNECTED) \
      _telnetLog(_s.c_str(), false); \
  } while(0)

#else
  #define logln(x) do { Serial.println(x); } while(0)
  #define logf(x)  do { Serial.print(x);   } while(0)
#endif