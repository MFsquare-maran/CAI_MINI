//TODO

/*
// In setup(), nach InitWiFi():
TelnetStream.begin();       // 2. Telnet starten

// Überall wo du Serial.println(...) schreibst:
#define LOG(x) { Serial.println(x); TelnetStream.println(x); }

LOG("SENSORDATEN");

*/

#include <Arduino.h> 

#ifdef LOG_TELNET


#include <TelnetStream.h>  

#define logln(x) { Serial.println(x); TelnetStream.println(x);}
#define logf(x)  { Serial.print(x); TelnetStream.print(x);}


#else

#define logln(x) Serial.println(x);
#define logf(x) Serial.print(x);

#endif