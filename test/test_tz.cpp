// Host check for fw/src/tz.h: the zone table, the location reply parser, and local <-> UTC with
// real POSIX rules (summer time on the right Sundays), the same way the firmware converts.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "../fw/src/tz.h"

static long local_offset(const char* rule, int y, int m, int d, int h) {   // what localtime makes of a UTC moment
    setenv("TZ", rule, 1);
    tzset();
    time_t t = (time_t)tz::UNIX_2000 + (time_t)tz::days_from_2000(y, m, d) * 86400 + h * 3600;
    struct tm lt;
    localtime_r(&t, &lt);
    long local = (long)tz::days_from_2000(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 +
                 lt.tm_hour * 3600 + lt.tm_min * 60;
    return local - (long)(t - tz::UNIX_2000);
}

int main() {
    assert(!std::strcmp(tz::posix_for("Europe/Stockholm"), "CET-1CEST,M3.5.0,M10.5.0/3"));
    assert(tz::posix_for("Mars/Olympus") == 0 && tz::posix_for("") == 0);

    char buf[32];
    tz::fixed_posix(3 * 3600, buf, sizeof buf);
    assert(!std::strcmp(buf, "<+0300>-3:00"));
    tz::fixed_posix(-(5 * 3600 + 1800), buf, sizeof buf);
    assert(!std::strcmp(buf, "<-0530>+5:30"));
    assert(local_offset(buf, 2026, 1, 1, 12) == -(5 * 3600 + 1800));

    char zone[48];
    long off = 0;
    assert(tz::parse_location("{\"status\":\"success\",\"timezone\":\"Europe/Stockholm\",\"offset\":7200}",
                              zone, sizeof zone, &off));
    assert(!std::strcmp(zone, "Europe/Stockholm") && off == 7200);
    assert(!tz::parse_location("{\"status\":\"fail\",\"message\":\"private range\"}", zone, sizeof zone, &off));
    assert(!tz::parse_location("{\"status\":\"success\",\"timezone\":\"\",\"offset\":0}", zone, sizeof zone, &off));
    assert(!tz::parse_location("{\"status\":\"success\",\"timezone\":\"X\",\"offset\":999999}", zone, sizeof zone, &off));

    const char* se = tz::posix_for("Europe/Stockholm");   // Sweden, 2026: summer time 29 Mar - 25 Oct
    assert(local_offset(se, 2026, 1, 15, 12) == 3600);
    assert(local_offset(se, 2026, 3, 29, 0) == 3600);     // 01:00 local, still winter
    assert(local_offset(se, 2026, 3, 29, 1) == 7200);     // 01:00 UTC: clocks jump to 03:00
    assert(local_offset(se, 2026, 7, 1, 12) == 7200);
    assert(local_offset(se, 2026, 10, 25, 0) == 7200);
    assert(local_offset(se, 2026, 10, 25, 1) == 3600);    // back to winter time
    assert(local_offset(tz::posix_for("Europe/Istanbul"), 2026, 7, 1, 12) == 3 * 3600);
    assert(local_offset(tz::posix_for("America/New_York"), 2026, 7, 1, 12) == -4 * 3600);

    assert(tz::days_from_2000(2000, 1, 1) == 0 && tz::days_from_2000(2026, 10, 31) == 9800);
    std::puts("tz ok");
}
