//TODO

/*
// In setup(), nach InitWiFi():
TelnetStream.begin();       // 2. Telnet starten

// Überall wo du Serial.println(...) schreibst:
#define LOG(x) { Serial.println(x); TelnetStream.println(x); }

LOG("SENSORDATEN");

*/

#include <Arduino.h> 

#pragma once

#ifdef LOG_TELNET
  #include <TelnetStream.h>
  #define logln(x) do { Serial.println(x); if(WiFi.status()==WL_CONNECTED) TelnetStream.println(x); } while(0)
  #define logf(x)  do { Serial.print(x);   if(WiFi.status()==WL_CONNECTED) TelnetStream.print(x);   } while(0)
#else
  #define logln(x) do { Serial.println(x); } while(0)
  #define logf(x)  do { Serial.print(x);   } while(0)
#endif