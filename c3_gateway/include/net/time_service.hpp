#pragma once

#include <Arduino.h>
#include <time.h>
#include "mcshal_protocol.hpp"

namespace mcshal {

// NTP-backed wall clock. Degrades gracefully: without internet the CPX keeps
// its standalone behaviour (day window always active, default theme).
// TZ default: mainland Spain (CET/CEST). Override with -DMCSHAL_TZ_RULE.
#ifndef MCSHAL_TZ_RULE
#define MCSHAL_TZ_RULE "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

class TimeService {
public:
    void begin() {
        configTzTime(MCSHAL_TZ_RULE, "pool.ntp.org", "time.nist.gov");
    }

    bool synced() const {
        time_t now = time(nullptr);
        return now > 1700000000; // after Nov 2023 => plausible NTP sync
    }

    bool minuteOfDay(uint16_t &out) const {
        if (!synced()) return false;
        struct tm t;
        if (!getLocalTime(&t, 0)) return false;
        out = static_cast<uint16_t>(t.tm_hour * 60 + t.tm_min);
        return true;
    }

    bool dateMd(uint8_t &month, uint8_t &day) const {
        if (!synced()) return false;
        struct tm t;
        if (!getLocalTime(&t, 0)) return false;
        month = static_cast<uint8_t>(t.tm_mon + 1);
        day = static_cast<uint8_t>(t.tm_mday);
        return true;
    }

    // "MM-DD" inclusive window, supports wrap-around (e.g. 12-20 -> 01-06).
    static bool dateInWindow(uint8_t m, uint8_t d, const char *from, const char *to) {
        int cur = m * 100 + d;
        int a = (from[0] - '0') * 1000 + (from[1] - '0') * 100 + (from[3] - '0') * 10 + (from[4] - '0');
        int b = (to[0] - '0') * 1000 + (to[1] - '0') * 100 + (to[3] - '0') * 10 + (to[4] - '0');
        if (a <= b) return cur >= a && cur <= b;
        return cur >= a || cur <= b;
    }
};

} // namespace mcshal
