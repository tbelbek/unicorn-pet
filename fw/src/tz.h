#pragma once
// Time-zone helpers with no Arduino dependency, so test/test_tz.cpp checks them on the host.
// The RTC keeps UTC; local time comes from a POSIX TZ rule, so summer time switches by itself.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace tz {

// IANA name -> POSIX rule for the zones a family is likely to be in. Anything else falls back to
// the fixed offset the location lookup reported (refreshed at every sync).
inline const char* posix_for(const char* iana) {
    static const char* const CET = "CET-1CEST,M3.5.0,M10.5.0/3", *EET = "EET-2EEST,M3.5.0/3,M10.5.0/4";
    static const struct { const char* name; const char* rule; } ZONES[] = {
        {"Europe/Stockholm", CET}, {"Europe/Oslo", CET}, {"Europe/Copenhagen", CET}, {"Europe/Berlin", CET},
        {"Europe/Paris", CET}, {"Europe/Amsterdam", CET}, {"Europe/Brussels", CET}, {"Europe/Vienna", CET},
        {"Europe/Zurich", CET}, {"Europe/Madrid", CET}, {"Europe/Rome", CET}, {"Europe/Warsaw", CET},
        {"Europe/Prague", CET}, {"Europe/Budapest", CET}, {"Europe/Belgrade", CET},
        {"Europe/Helsinki", EET}, {"Europe/Tallinn", EET}, {"Europe/Riga", EET}, {"Europe/Vilnius", EET},
        {"Europe/Athens", EET}, {"Europe/Bucharest", EET}, {"Europe/Sofia", EET}, {"Europe/Kyiv", EET},
        {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"}, {"Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"},
        {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"}, {"Atlantic/Reykjavik", "GMT0"},
        {"Europe/Istanbul", "<+03>-3"}, {"Europe/Moscow", "MSK-3"},
        {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
        {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"}, {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
        {"UTC", "UTC0"},
    };
    if (!iana || !*iana) return 0;
    for (const auto& z : ZONES)
        if (!strcmp(z.name, iana)) return z.rule;
    return 0;
}

// A fixed offset as a POSIX rule. POSIX counts west as positive: UTC+2 is "<+0200>-2".
inline void fixed_posix(long offset_s, char* out, int n) {
    long a = offset_s < 0 ? -offset_s : offset_s;
    snprintf(out, n, "<%c%02ld%02ld>%c%ld:%02ld", offset_s < 0 ? '-' : '+', a / 3600, a % 3600 / 60,
             offset_s < 0 ? '+' : '-', a / 3600, a % 3600 / 60);
}

// The two fields we need from ip-api.com's reply, without a JSON library:
// {"status":"success","timezone":"Europe/Stockholm","offset":3600}
inline bool parse_location(const char* json, char* zone, int n, long* offset_s) {
    if (!json || !strstr(json, "\"status\":\"success\"")) return false;
    const char* z = strstr(json, "\"timezone\":\"");
    const char* o = strstr(json, "\"offset\":");
    if (!z || !o) return false;
    z += 12;
    int i = 0;
    while (z[i] && z[i] != '"' && i < n - 1) { zone[i] = z[i]; i++; }
    zone[i] = 0;
    if (z[i] != '"' || !i) return false;
    char* end = 0;
    long v = strtol(o + 9, &end, 10);
    if (end == o + 9 || v < -14 * 3600 || v > 14 * 3600) return false;
    *offset_s = v;
    return true;
}

// Days since 2000-01-01 for a civil date, and back: the RTC and the game both count from 2000.
inline int32_t days_from_2000(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 730425;    // 730425 = days from 0000-03-01 to 2000-01-01
}

enum : uint32_t { UNIX_2000 = 946684800u };   // 2000-01-01 00:00:00 UTC as a Unix time

}  // namespace tz
