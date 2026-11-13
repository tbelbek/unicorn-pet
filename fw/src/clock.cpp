#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_sntp.h>
#include "clock.h"
#include "config.h"
#include "rtc.h"
#include "tz.h"
#include "ota.h"
#include "net.h"
#include "wifi_setup.h"

static Preferences* nv;
static bool trusted;

// ---- local <-> UTC through the TZ rule ----------------------------------------------------

static void use_zone(const char* rule) {
    setenv("TZ", rule, 1);
    tzset();
}

static uint32_t to_local(uint32_t utc) {   // seconds since 2000, both ways
    time_t t = (time_t)utc + tz::UNIX_2000;
    struct tm lt;
    localtime_r(&t, &lt);
    return (uint32_t)tz::days_from_2000(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400u +
           lt.tm_hour * 3600u + lt.tm_min * 60u + lt.tm_sec;
}

static uint32_t to_utc(uint32_t local) {
    time_t t = (time_t)local + tz::UNIX_2000;
    struct tm lt;
    gmtime_r(&t, &lt);                     // just splits the local reading into fields
    lt.tm_isdst = -1;                      // let the rule decide summer time
    return (uint32_t)(mktime(&lt) - (time_t)tz::UNIX_2000);
}

// ---- the build time, as a starting point --------------------------------------------------

// The build machine's local time, from the compiler. Off by however long it was between building
// and flashing (minutes, usually): good enough until WiFi or a parent sets the real time.
static uint32_t build_local() {
    static const char* MON = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char* d = __DATE__;              // "Oct 31 2026"
    int m = 0;
    while (m < 12 && strncmp(MON + m * 3, d, 3)) m++;
    const char* t = __TIME__;              // "14:05:09"
    return (uint32_t)tz::days_from_2000(atoi(d + 7), m + 1, atoi(d + 4)) * 86400u + atoi(t) * 3600u +
           atoi(t + 3) * 60u + atoi(t + 6);
}

// config.h's zone, if usable: a name from tz.h's list, or a POSIX rule. An unknown IANA name
// ("Asia/Tokyo") is treated as empty, so the WiFi lookup fills it in.
static const char* fixed_rule() {
    const char* z = TIMEZONE;
    if (!*z) return 0;
    const char* r = tz::posix_for(z);
    if (r) return r;
    bool posix = strchr(z, ',') || z[0] == '<' || !strchr(z, '/');   // "EST5EDT,M3.2.0/2,M11.1.0/2" is a rule
    return posix ? z : 0;
}

// The zone: config.h wins, then the one looked up over WiFi, then the fallback.
static void pick_zone() {
    if (const char* r = fixed_rule()) { use_zone(r); return; }
    String cached = nv->getString("zrule", "");
    if (cached.length()) { use_zone(cached.c_str()); return; }
    use_zone(tz::posix_for(TIMEZONE_FALLBACK));
}

void clock_begin(Preferences& prefs) {
    nv = &prefs;
    pick_zone();
    // A new firmware keeps a running clock (flashing must not undo WiFi or a parent's setting);
    // it only restarts the clock from its build time when the clock is clearly behind it or had
    // stopped. Such a clock runs (breaks time from it) but its date is only a guess: __DATE__ is
    // when this file was compiled, which may be days before the flash, and that would hatch her on
    // the wrong day and throw a "missed you" party at the first WiFi sync. Only WiFi or a parent
    // (settings > SAAT) makes the calendar trusted.
    uint32_t build = build_local();
    bool fresh = prefs.getUInt("build", 0) != build, lost = rtc_lost();
    uint32_t now = rtc_now();
    bool seed = lost || (fresh && (!now || to_local(now) < build));
    bool seeded = seed && rtc_set(to_utc(build));
    if (fresh && (!seed || seeded)) {      // a failed write is retried on the next boot of this build
        prefs.putUInt("build", build);
        if (seeded) prefs.putBool("cal", false);   // a kept clock keeps its trust; a seeded one has none
    } else if (lost) {
        prefs.putBool("cal", false);
    }
    trusted = prefs.getBool("cal", false);
}

uint32_t clock_local(uint32_t utc) { return utc ? to_local(utc) : 0; }

bool clock_trusted() { return trusted; }

void clock_set_local(uint32_t local) {
    if (!rtc_set(to_utc(local))) return;   // only a clock that took the time is trusted
    nv->putBool("cal", true);
    trusted = true;
}

// ---- NTP, shared with the WiFi setup (wifi_setup.cpp) ---------------------------------------

void ntp_start() {
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);   // not configTime(): it would reset TZ
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);      // wait for a fresh answer, every time
    esp_sntp_init();
}

bool ntp_done(uint32_t* utc) {             // a fresh, sane answer has arrived
    const time_t Y2025 = 1735689600;       // anything earlier is the ESP32's own unset clock
    time_t now = time(nullptr);
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED || now < Y2025) return false;
    *utc = (uint32_t)(now - (time_t)tz::UNIX_2000);
    return true;
}

void ntp_stop() { esp_sntp_stop(); }

bool clock_set_utc(uint32_t utc) {         // main task only (I2C)
    if (!rtc_set(utc)) return false;
    nv->putBool("cal", true);
    trusted = true;
    Serial.println("clock set over WiFi");
    return true;
}

// ---- WiFi sync: a one-shot task per attempt, results handed over through a queue -----------

struct Sync { uint32_t utc, at_ms; bool zone_ok; long offset; char zone[40]; int newest; };   // utc as of at_ms
struct Job { bool want_zone; int n; WifiNet nets[WIFI_KNOWN_MAX]; };            // a copy for the task
static QueueHandle_t results;
static volatile bool running;
static uint32_t next_ms = 3000;            // first try shortly after power-up

static void sync_task(void* arg) {
    Job* job = (Job*)arg;
    Sync r = {};
    bool joined = false;
    {                                      // scoped: HTTPClient must be destroyed before vTaskDelete
    if ((joined = wifi_join(job->nets, job->n))) {
        ntp_start();
        for (int i = 0; i < 60 && !ntp_done(&r.utc); i++) delay(250);
        if (r.utc) r.at_ms = millis();
        ntp_stop();
        if (job->want_zone) {              // the time zone of this internet connection
            HTTPClient http;
            http.setTimeout(5000);
            if (http.begin("http://ip-api.com/json/?fields=status,timezone,offset") && http.GET() == 200) {
                String body = http.getString();
                r.zone_ok = tz::parse_location(body.c_str(), r.zone, sizeof r.zone, &r.offset);
            }
            http.end();
        }
        r.newest = ota_latest();           // while connected: is a newer signed firmware out? (0: unknown)
        net_sync_here();                   // connected anyway: the parent page's settings, the toy's status
    }
    }
    delete job;
    if (joined) wifi_leave();              // the radio is off again until the next sync (unless shared)
    xQueueOverwrite(results, &r);
    running = false;
    vTaskDelete(nullptr);
}

void clock_poll(uint32_t now_ms) {
    if (!results) results = xQueueCreate(1, sizeof(Sync));
    Sync r;
    if (!running && xQueueReceive(results, &r, 0) == pdTRUE) {
        if (r.zone_ok) {                   // remember the zone; look it up again in a week (travel)
            char rule[48];
            const char* known = tz::posix_for(r.zone);
            // ponytail: an unknown zone gets today's fixed offset, so it is an hour off across a
            // summer-time change until the next weekly lookup; add its rule to tz.h to fix it.
            if (known) snprintf(rule, sizeof rule, "%s", known); else tz::fixed_posix(r.offset, rule, sizeof rule);
            nv->putString("zrule", rule);
            nv->putString("zone", r.zone);
            nv->putUInt("zday", (r.utc ? r.utc : rtc_now()) / 86400);
            pick_zone();
            Serial.printf("time zone: %s (%s)\n", r.zone, rule);
        }
        if (r.utc) clock_set_utc(r.utc + (millis() - r.at_ms + 500) / 1000);   // + the seconds since NTP answered
        ota_seen(r.newest);                // the settings mark a waiting update
        next_ms = now_ms + (r.utc ? 12u * 3600000u : 30u * 60000u);   // twice a day, or retry soon
        if (!next_ms) next_ms = 1;
    }
    // one radio owner: no sync while a parent's WiFi setup holds the radio
    if (running || wifi_setup_busy() || ota_busy() || (int32_t)(now_ms - next_ms) < 0) return;
    next_ms = now_ms + 30u * 60000u;       // if this attempt never reports back
    Job* job = new Job();
    job->n = wifi_known(job->nets, WIFI_KNOWN_MAX);
    if (!job->n) { delete job; return; }   // no networks yet: the RTC and the parent set the time
    uint32_t today = rtc_now() / 86400, last = nv->getUInt("zday", 0);
    job->want_zone = !fixed_rule() && (!nv->getString("zrule", "").length() || today >= last + 7);
    running = true;
    if (xTaskCreatePinnedToCore(sync_task, "clock", 16384, job, 1, nullptr, 0) != pdPASS) { running = false; delete job; }
}

bool clock_busy() { return running; }
