#include "time_functions.h"

// ============================================================
//  Zeit (NTP)
// ============================================================

const char *ntpServer        = "ch.pool.ntp.org";
const long  gmtOffset_sec    = 3600;   // Zeitzone UTC+1
const int   daylightOffset_sec = 3600; // Sommerzeit

bool LocalTime(char datetime[30], tm* timeinfo) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    unsigned long start = millis();
    const unsigned long timeout = 10000;

    while (!getLocalTime(timeinfo)) {   // ← kein & davor, timeinfo ist schon Pointer
        if (millis() - start > timeout) {
            Serial.println("⚠️ NTP Timeout – keine Zeit empfangen");
            datetime[0] = '\0';
            return false;
        }
        Serial.println("Warte auf NTP...");
        delay(500);
    }

    strftime(datetime, 30, "%Y-%m-%d %H:%M:%S", timeinfo);  // ← kein &, und sizeof-Problem (s.u.)
    Serial.print("Aktuelle Zeit: ");
    Serial.println(datetime);
    return true;
}