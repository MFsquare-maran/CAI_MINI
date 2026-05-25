#include "log.h"

#ifdef LOG_TELNET
  #include <TelnetStream.h>
#endif

unsigned long _ntp_millis_anchor = 0;
time_t        _ntp_time_anchor   = 0;

void _telnetLog(const char* msg, bool newline) {
#ifdef LOG_TELNET
    char ts[23];
    memset(ts, 0, sizeof(ts));

    if (_ntp_time_anchor != 0) {
        unsigned long off = (millis() - _ntp_millis_anchor) / 1000UL;
        time_t now_t      = _ntp_time_anchor + (time_t)off;
        struct tm t;
        localtime_r(&now_t, &t);
        strftime(ts, sizeof(ts), "[%Y-%m-%d %H:%M:%S] ", &t);
    } else {
        memcpy(ts, "[--:--:--:--:--] \0\0\0\0\0", 23);
    }

    TelnetStream.print(ts);

    if (newline)
        TelnetStream.println(msg);
    else
        TelnetStream.print(msg);
#endif
}