// The home listener and the parent page's settings (server/API.md). See net.h.
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/dns.h>
#include <lwip/priv/tcpip_priv.h>
#include "net.h"
#include "http_reply.h"
#include "store.h"
#include "wifi_setup.h"

using namespace up;

static const uint32_t PING_MS = 2000, ANSWER_MS = 6000, HTTP_MS = 6000, POLL_MS = 60000, RETRY_MS = 60000;
static const int BODY_MAX = 4096;          // the listener's JSON replies are small; anything bigger is refused
static const char* const ANSWER_NAME[] = {"", "yes", "no", "unclear", "silent", "said", "tap_yes", "tap_no"};
static const char* const LANG_CODE[LANG_COUNT] = {"tr", "en", "es", "fr", "de", "pt", "it", "ru", "sv", "id"};

// ---- what the main task shares with the net task ---------------------------------------------------
struct Snap {                              // the core's state for /v1/status, refreshed every second
    Settings set;
    SyncMark mark;                         // the toy's own changes this status carries (Game::sync_mark)
    u32 rev, local_s;
    int32_t utc_offset;
    u16 r_day, r_ans[Q_COUNT];
    int rday_now;                          // her routine day now (Game::rday_now), -1 = no trusted clock
    int battery, fw, update, stars;
    bool plugged, valid;
};
struct Result { uint8_t kind, answer; uint16_t present; uint32_t rev; Settings page; SyncMark mark; };
enum { R_ANSWER, R_CONFIG, R_SYNCED };
struct Job { volatile bool full; int question, kind; uint32_t len, local_s; uint8_t* wav; };

static SemaphoreHandle_t mx, talk;         // mx: the shared data below; talk: one HTTP conversation at a time
static QueueHandle_t results;
static TaskHandle_t task;
static Snap snap;
static WifiNet nets[WIFI_KNOWN_MAX];
static int nets_n;
static Job answer_job, tv_job;
static volatile int want = LINK_OFF;
static volatile bool held, joining;     // joining: a join under way (no light sleep then either)
static ListenerConf conf;
static bool conf_ok;
static IPAddress ip;                       // the listener, resolved once per connection
static uint8_t* jbuf;                      // a journal entry on its way out

// ---- HTTP -------------------------------------------------------------------------------------------
static int32_t left(uint32_t deadline) { int32_t ms = (int32_t)(deadline - millis()); return ms > 0 ? ms : 0; }

// Plain DNS with a bounded wait: lwIP's own lookup, answered by a callback on the tcpip thread. The answer
// lands in static storage tagged with the lookup's number, so a callback that comes after we gave up (or for
// an older lookup) touches nothing that is gone and is ignored.
#ifndef CONFIG_LWIP_TCPIP_CORE_LOCKING
#error "dns_lookup() calls lwIP from the net task: it needs the tcpip core lock"
#endif
static volatile uint32_t dns_seq, dns_got, dns_ip;
static void dns_found(const char*, const ip_addr_t* a, void* arg) {
    uint32_t seq = (uint32_t)(uintptr_t)arg;
    if (seq != dns_seq) return;            // a lookup we no longer wait for
    dns_ip = a && IP_IS_V4(a) ? ip4_addr_get_u32(ip_2_ip4(a)) : 0;
    dns_got = seq;
}
static uint32_t dns_lookup(const char* host, int32_t ms) {   // under `talk`: one lookup at a time
    uint32_t seq = dns_seq + 1;
    dns_seq = seq;
    ip_addr_t a;
    bool mine = !sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER);
    if (mine) LOCK_TCPIP_CORE();
    err_t e = dns_gethostbyname(host, &a, dns_found, (void*)(uintptr_t)seq);
    if (mine) UNLOCK_TCPIP_CORE();
    if (e == ERR_OK) return IP_IS_V4(&a) ? ip4_addr_get_u32(ip_2_ip4(&a)) : 0;   // cached or a literal
    if (e != ERR_INPROGRESS) return 0;
    for (uint32_t t0 = millis(); dns_got != seq && (int32_t)(millis() - t0) < ms;) delay(10);
    return dns_got == seq ? dns_ip : 0;
}

static bool resolve(uint32_t deadline) {
    if (ip != IPAddress(0, 0, 0, 0)) return true;
    int n = strlen(conf.host);
    if (left(deadline) <= 0) return false;
    if (n > 6 && !strcasecmp(conf.host + n - 6, ".local")) {   // mDNS: "my-mac.local"
        char name[64];
        snprintf(name, sizeof name, "%.*s", n - 6, conf.host);
        MDNS.begin("unicorn-pet");
        int32_t ms = left(deadline);
        ip = MDNS.queryHost(name, ms < 2000 ? ms : 2000);
    } else if (!ip.fromString(conf.host)) {
        ip = IPAddress(dns_lookup(conf.host, left(deadline)));
    }
    return ip != IPAddress(0, 0, 0, 0);
}

static bool send_all(int fd, const uint8_t* p, uint32_t n, uint32_t deadline) {   // never past the deadline
    while (n) {
        int32_t ms = left(deadline);
        if (ms <= 0) return false;
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd, &w);
        timeval tv = {0, (ms > 100 ? 100 : ms) * 1000};
        if (select(fd + 1, nullptr, &w, nullptr, &tv) < 0) return false;
        if (!FD_ISSET(fd, &w)) continue;
        int r = send(fd, p, n, MSG_DONTWAIT);
        if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (r > 0) { p += r; n -= r; }
    }
    return true;
}

static char rbuf[BODY_MAX + 1024 + 1];     // a reply: headers and at most BODY_MAX of JSON (under `talk`)

// One request, with one deadline of `ms` over all of it: resolving, connecting, sending and the whole reply (a
// listener that trickles bytes cannot hold the net task). Returns the status (negative: no connection, too
// slow, too big or not HTTP), the JSON reply (at most BODY_MAX) in *reply.
static int http(const char* method, const char* path, const uint8_t* body, uint32_t len, const char* type,
                uint32_t ms, const char* const* headers, String* reply) {
    uint32_t deadline = millis() + ms;
    if (!conf_ok || !resolve(deadline)) return -1;
    NetworkClient c;
    int32_t ms_left = left(deadline);
    if (ms_left <= 0) return -1;
    if (!c.connect(ip, conf.port, ms_left)) { ip = IPAddress(0, 0, 0, 0); return -1; }   // it may have moved: look again
    String head = String(method) + " " + path + " HTTP/1.1\r\nHost: " + conf.host + ":" + conf.port +
                  "\r\nConnection: close\r\nX-Token: " + conf.token + "\r\nContent-Length: " + len + "\r\n";
    if (type) head += String("Content-Type: ") + type + "\r\n";
    for (int i = 0; headers && headers[i]; i += 2) head += String(headers[i]) + ": " + headers[i + 1] + "\r\n";
    head += "\r\n";
    if (!send_all(c.fd(), (const uint8_t*)head.c_str(), head.length(), deadline) ||
        (len && !send_all(c.fd(), body, len, deadline))) { c.stop(); return -3; }
    int got = 0, st = 0, at = 0, blen = 0, whole = 0;
    while (!whole) {
        if (left(deadline) <= 0) { whole = -3; break; }
        int a = c.available();
        if (a > 0) {
            if (got == (int)sizeof rbuf - 1) { whole = -2; break; }   // more than a reply of ours
            int r = c.read((uint8_t*)rbuf + got, a < (int)sizeof rbuf - 1 - got ? a : (int)sizeof rbuf - 1 - got);
            if (r > 0) got += r;
            whole = http_reply(rbuf, got, false, &st, &at, &blen);
        } else if (int r = recv(c.fd(), rbuf + got, 1, MSG_PEEK | MSG_DONTWAIT);
                   r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {   // the server closed (connected() misses it)
            whole = http_reply(rbuf, got, true, &st, &at, &blen);
        } else if (r < 0) {
            delay(5);
        }
    }
    c.stop();                              // also the abort: the deadline passed
    if (whole != 1) return whole == -2 ? -2 : -3;
    if (reply) {
        if (blen > BODY_MAX) return -2;
        rbuf[at + blen] = 0;
        *reply = rbuf + at;
    }
    return st;
}

static void iso(uint32_t local_s, int32_t offset, char* out, int n) {   // X-Recorded-At, the toy's local time
    Cal c = calendar(local_s);
    int off = offset / 60, a = off < 0 ? -off : off;
    snprintf(out, n, "%04u-%02u-%02uT%02u:%02u:%02u%c%02d:%02d", c.year, c.month, c.mday, c.minute / 60, c.minute % 60,
             (unsigned)(local_s % 60), off < 0 ? '-' : '+', a / 60, a % 60);
}

static void push(const Result& r) { xQueueSend(results, &r, 0); }

// ---- settings <-> the page's JSON ------------------------------------------------------------------
static u32 day_of(const char* s) {         // "YYYY-MM-DD" -> local days since 2000-01-01, 0 if not a date
    int y, m, d;
    if (!s || sscanf(s, "%4d-%2d-%2d", &y, &m, &d) != 3 || y < 2001 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    static const int CUM[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    u32 n = 0;
    for (int k = 2000; k < y; k++) n += k % 4 ? 365 : 366;   // 2000..2099: every 4th year is a leap year
    return n + CUM[m - 1] + (m > 2 && y % 4 == 0) + d - 1;
}
static void date_of(u32 day, char* out, int n) { Cal c = calendar(day * 86400u + 1); snprintf(out, n, "%04u-%02u-%02u", c.year, c.month, c.mday); }
static int minutes_of(const char* s) {
    int h, m;
    return s && sscanf(s, "%2d:%2d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60 ? h * 60 + m : -1;
}

// The page's settings over mine (merge_settings() in the core decides what wins); present: the keys it sent.
static bool parse_config(const String& body, Settings* page, u16* present, u32* rev) {
    JsonDocument doc;
    if (deserializeJson(doc, body) || !doc["rev"].is<uint32_t>()) return false;
    *rev = doc["rev"];
    JsonObjectConst s = doc["settings"];
    *present = 0;
    auto flag = [&](int k, const char* key) {
        if (s[key].is<bool>()) { page->v[k] = s[key].as<bool>(); *present |= 1 << k; }
    };
    auto number = [&](int k, const char* key) {
        if (s[key].is<int>()) { int v = s[key]; if (v >= 0 && v <= 65535) { page->v[k] = (u16)v; *present |= 1 << k; } }
    };
    auto clock = [&](int k, const char* key) {
        int m = minutes_of(s[key] | "");
        if (m >= 0) { page->v[k] = (u16)m; *present |= 1 << k; }
    };
    flag(K_ROUTINES, "routines"); flag(K_HOLIDAY, "holiday"); flag(K_TV_TEST, "tv_test");
    flag(K_CALM, "calm"); flag(K_SHAKE, "shake");
    clock(K_WAKE_WEEKDAY, "wake_weekday"); clock(K_WAKE_WEEKEND, "wake_weekend"); clock(K_BEDTIME, "bedtime");
    number(K_VOLUME, "volume"); number(K_BRIGHTNESS, "brightness"); number(K_PLAY_MIN, "play_min"); number(K_BREAK_MIN, "break_min");
    const char* lang = s["lang"] | "";
    for (int l = 0; l < LANG_COUNT; l++) if (!strcmp(lang, LANG_CODE[l])) { page->v[K_LANG] = (u16)l; *present |= 1 << K_LANG; }
    JsonObjectConst q = s["questions"];
    if (!q.isNull()) {                     // partial is fine: missing questions keep their state
        for (int k = 0; k < Q_COUNT; k++)
            if (q[Q_KEY[k]].is<bool>()) page->v[K_QUESTIONS] = (u16)(q[Q_KEY[k]] ? page->v[K_QUESTIONS] | 1 << k : page->v[K_QUESTIONS] & ~(1 << k));
        *present |= 1 << K_QUESTIONS;
    }
    if (s["tv_test_since"].is<const char*>()) page->tv_day = (u16)day_of(s["tv_test_since"]);   // "" -> 0: today
    JsonArrayConst hr = s["holiday_ranges"];
    if (!hr.isNull() && hr.size() <= HOL_MAX) {
        int n = 0;
        for (JsonArrayConst r : hr) {
            page->hol[n][0] = (u16)day_of(r[0] | ""); page->hol[n][1] = (u16)day_of(r[1] | "");
            n++;
        }
        page->v[K_HOLIDAY_RANGES] = (u16)n;
        *present |= 1 << K_HOLIDAY_RANGES;   // (a bad range is refused by merge_settings)
    }
    return true;
}

static String status_json(const Snap& sn, const Settings& st, u32 rev) {
    JsonDocument doc;
    doc["rev"] = rev;
    JsonObject s = doc["settings"].to<JsonObject>();
    char t[16];
    s["routines"] = st.v[K_ROUTINES] != 0;
    JsonObject q = s["questions"].to<JsonObject>();
    for (int k = 0; k < Q_COUNT; k++) q[Q_KEY[k]] = (st.v[K_QUESTIONS] >> k & 1) != 0;
    auto hhmm = [&](const char* key, int m) { snprintf(t, sizeof t, "%02d:%02d", m / 60, m % 60); s[key] = t; };
    hhmm("wake_weekday", st.v[K_WAKE_WEEKDAY]); hhmm("wake_weekend", st.v[K_WAKE_WEEKEND]); hhmm("bedtime", st.v[K_BEDTIME]);
    s["holiday"] = st.v[K_HOLIDAY] != 0;
    JsonArray hr = s["holiday_ranges"].to<JsonArray>();
    for (int i = 0; i < st.v[K_HOLIDAY_RANGES] && i < HOL_MAX; i++) {
        JsonArray r = hr.add<JsonArray>();
        date_of(st.hol[i][0], t, sizeof t); r.add(String(t));
        date_of(st.hol[i][1], t, sizeof t); r.add(String(t));
    }
    s["tv_test"] = st.v[K_TV_TEST] != 0;
    u32 since = st.v[K_TV_TEST] ? (st.tv_day ? st.tv_day : sn.local_s / 86400) : 0;   // switched on just now: today
    if (since) { date_of(since, t, sizeof t); s["tv_test_since"] = t; } else s["tv_test_since"] = "";
    s["lang"] = LANG_CODE[st.v[K_LANG] < LANG_COUNT ? st.v[K_LANG] : 0];
    s["volume"] = st.v[K_VOLUME]; s["brightness"] = st.v[K_BRIGHTNESS];
    s["play_min"] = st.v[K_PLAY_MIN]; s["break_min"] = st.v[K_BREAK_MIN];
    s["calm"] = st.v[K_CALM] != 0; s["shake"] = st.v[K_SHAKE] != 0;
    doc["battery"] = sn.battery; doc["plugged"] = sn.plugged; doc["fw"] = sn.fw; doc["update"] = sn.update;
    JsonObject day = doc["day"].to<JsonObject>();
    JsonArray ans = day["answers"].to<JsonArray>();
    bool today = sn.rday_now >= 0 && sn.r_day == sn.rday_now;
    for (int k = 0; today && k < Q_COUNT; k++) {
        int a = sn.r_ans[k] >> 11, m = sn.r_ans[k] & 2047;
        if (!a || a > ANS_TAP_NO) continue;
        JsonObject e = ans.add<JsonObject>();
        e["q"] = Q_KEY[k]; e["a"] = ANSWER_NAME[a];
        snprintf(t, sizeof t, "%02d:%02d", m / 60, m % 60); e["t"] = t;
    }
    day["stars"] = today ? sn.stars : 0;
    String out;
    serializeJson(doc, out);
    return out;
}

// GET /v1/config (a newer page config), then POST /v1/status with the result; the core applies the same
// merge on the main task (Result). Called with `talk` held, while the link is up.
static void sync_now() {
    xSemaphoreTake(mx, portMAX_DELAY);
    Snap sn = snap;
    xSemaphoreGive(mx);
    if (!sn.valid) return;
    char rev[12];
    snprintf(rev, sizeof rev, "%lu", (unsigned long)sn.rev);
    const char* hdr[] = {"X-Config-Rev", rev, nullptr};
    String body;
    Settings page = sn.set, merged = sn.set;
    u16 present = 0;
    u32 new_rev = sn.rev;
    int code = http("GET", "/v1/config", nullptr, 0, nullptr, HTTP_MS, hdr, &body);
    if (code < 0) return;                  // nobody there
    if (code == 200 && parse_config(body, &page, &present, &new_rev) && new_rev != sn.rev) {
        merged = merge_settings(sn.set, sn.mark.dirty, page, present);
        Result r = {R_CONFIG, 0, present, new_rev, page, {}};
        push(r);
    } else new_rev = sn.rev;
    String st = status_json(sn, merged, new_rev);
    if (http("POST", "/v1/status", (const uint8_t*)st.c_str(), st.length(), "application/json", HTTP_MS, nullptr, nullptr) == 200) {
        Result r = {R_SYNCED, 0, 0, new_rev, page, sn.mark};   // clears only the edits this status carried
        push(r);
    }
}

static void upload_journal() {             // open answers recorded while nobody was listening
    xSemaphoreTake(mx, portMAX_DELAY);
    uint32_t now = snap.local_s;
    int32_t off = snap.utc_offset;
    xSemaphoreGive(mx);
    if (now) journal_expire(now);
    char path[64], at[32];
    int q;
    uint32_t t;
    for (int k = 0; k < 4 && jbuf && journal_oldest(path, sizeof path, &q, &t); k++) {
        uint32_t n = journal_read(path, jbuf, 44 + 16000 * 2 * 8);
        if (!n || q < 0 || q >= Q_COUNT) { journal_remove(path); continue; }   // unreadable: not kept forever
        iso(t, off, at, sizeof at);
        const char* hdr[] = {"X-Question", Q_KEY[q], "X-Recorded-At", at, nullptr};
        if (http("POST", "/v1/journal", jbuf, n, "audio/wav", HTTP_MS, hdr, nullptr) != 200) break;
        journal_remove(path);
    }
}

// An answer with nobody to understand it: the buttons for a yes/no; an open answer is kept for later.
static void no_listener(Job& j) {
    int a = ANS_NO_LISTENER;
    if (j.kind == LK_OPEN) { journal_add(j.wav, j.len, j.question, j.local_s); a = ANS_SAID; }
    Result r = {R_ANSWER, (uint8_t)a, 0, 0, {}, {}};
    push(r);
    j.full = false;
}

static bool do_answer(Job& j) {
    char at[32];
    xSemaphoreTake(mx, portMAX_DELAY);
    int32_t off = snap.utc_offset;
    xSemaphoreGive(mx);
    iso(j.local_s, off, at, sizeof at);
    String reply;
    if (http("GET", "/v1/ping", nullptr, 0, nullptr, PING_MS, nullptr, &reply) != 200) { no_listener(j); return false; }
    const char* hdr[] = {"X-Question", Q_KEY[j.question], "X-Kind", j.kind == LK_OPEN ? "open" : "yesno", "X-Recorded-At", at, nullptr};
    if (http("POST", "/v1/answer", j.wav, j.len, "audio/wav", ANSWER_MS, hdr, &reply) != 200) { no_listener(j); return false; }
    JsonDocument doc;
    const char* a = deserializeJson(doc, reply) ? "" : (doc["answer"] | "");
    int code = !strcmp(a, "yes") ? ANS_YES : !strcmp(a, "no") ? ANS_NO : !strcmp(a, "said") ? ANS_SAID
             : !strcmp(a, "silent") ? ANS_SILENT : ANS_UNCLEAR;
    Result r = {R_ANSWER, (uint8_t)code, 0, 0, {}, {}};
    push(r);
    j.full = false;
    return true;
}

static void drop_link() {                  // under `talk`: the clock task may be mid-request (an mDNS query) on it
    xSemaphoreTake(talk, portMAX_DELAY);
    MDNS.end();
    wifi_leave();
    held = false;
    ip = IPAddress(0, 0, 0, 0);
    xSemaphoreGive(talk);
}

static void net_task(void*) {
    uint32_t last_sync = 0, retry_at = 0;
    bool synced_once = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        int w = want;
        bool jobs = answer_job.full || tv_job.full;
        if (!conf_ok && answer_job.full) no_listener(answer_job);   // no config.json: nobody to ask
        if (!conf_ok) { tv_job.full = false; continue; }
        if (held && WiFi.status() != WL_CONNECTED) drop_link();   // the link dropped (a router restart): join again below
        if (!jobs && w == LINK_OFF) {
            if (held) drop_link();
            continue;
        }
        if (!held) {
            if (!jobs && (int32_t)(millis() - retry_at) < 0) continue;   // plugged in, but no network: later
            WifiNet copy[WIFI_KNOWN_MAX];
            xSemaphoreTake(mx, portMAX_DELAY);
            int n = nets_n;
            memcpy(copy, nets, sizeof copy);
            xSemaphoreGive(mx);
            joining = true;
            held = n && wifi_join(copy, n);
            joining = false;
            if (!held) {
                retry_at = millis() + RETRY_MS;
                if (answer_job.full) no_listener(answer_job);
                tv_job.full = false;
                continue;
            }
            synced_once = false;
        }
        xSemaphoreTake(talk, portMAX_DELAY);
        bool talked = false;
        if (answer_job.full) talked |= do_answer(answer_job);
        if (tv_job.full) {
            char at[32];
            iso(tv_job.local_s, snap.utc_offset, at, sizeof at);
            const char* hdr[] = {"X-Recorded-At", at, nullptr};
            talked |= http("POST", "/v1/tv-sample", tv_job.wav, tv_job.len, "audio/wav", HTTP_MS, hdr, nullptr) == 200;
            tv_job.full = false;
        }
        // connected anyway: the page's settings and the journal (on USB power right away, then every minute)
        if (talked || !synced_once || (w == LINK_STAY && millis() - last_sync >= POLL_MS)) {
            sync_now();
            upload_journal();
            last_sync = millis();
            synced_once = true;
        }
        xSemaphoreGive(talk);
    }
}

void net_sync_here() {                     // on the clock sync's task, which holds the link
    if (!conf_ok || !talk || xSemaphoreTake(talk, pdMS_TO_TICKS(100)) != pdTRUE) return;   // the net task is at it
    sync_now();
    upload_journal();
    ip = held ? ip : IPAddress(0, 0, 0, 0);
    xSemaphoreGive(talk);
}

void net_begin() {
    mx = xSemaphoreCreateMutex();
    talk = xSemaphoreCreateMutex();
    results = xQueueCreate(6, sizeof(Result));
    conf_ok = store_conf(&conf);
    uint32_t cap = 44 + 16000 * 2 * 8;     // 8 s of 16 kHz mono: every recording fits
    answer_job.wav = (uint8_t*)ps_malloc(cap);
    tv_job.wav = (uint8_t*)ps_malloc(cap);
    jbuf = (uint8_t*)ps_malloc(cap);
    xTaskCreatePinnedToCore(net_task, "net", 12288, nullptr, 1, &task, 0);
}

static void hand_over(Job& j, int question, int kind, const uint8_t* wav, uint32_t len) {
    if (!j.wav || len > 44 + 16000 * 2 * 8 || j.full) return;
    memcpy(j.wav, wav, len);
    j.len = len;
    j.question = question;
    j.kind = kind;
    xSemaphoreTake(mx, portMAX_DELAY);
    j.local_s = snap.local_s;
    xSemaphoreGive(mx);
    j.full = true;
    if (task) xTaskNotifyGive(task);
}

void net_answer(int question, int kind, const uint8_t* wav, uint32_t len) {
    if (answer_job.full) return;
    hand_over(answer_job, question, kind, wav, len);
    if (!answer_job.full) { Result r = {R_ANSWER, ANS_NO_LISTENER, 0, 0, {}, {}}; push(r); }   // no room: the buttons
}
void net_tv_sample(const uint8_t* wav, uint32_t len) { hand_over(tv_job, 0, LK_TV, wav, len); }
bool net_sample_waiting() { return tv_job.full; }
bool net_busy() { return held || joining || answer_job.full || tv_job.full; }

void net_poll(Game& g, uint32_t now) {
    static uint32_t last_snap, last_nets;
    if (!mx) return;
    if (!last_snap || now - last_snap >= 1000) {   // the core's state, for the next /v1/status
        last_snap = now ? now : 1;
        xSemaphoreTake(mx, portMAX_DELAY);
        snap.set = g.settings();
        snap.rev = g.s.cfg_rev;
        snap.mark = g.sync_mark();
        snap.r_day = g.s.r_day;
        snap.rday_now = g.rday_now();
        memcpy(snap.r_ans, g.s.r_ans, sizeof snap.r_ans);
        snap.stars = g.stars_today();
        snap.local_s = g.cal_ok ? g.wall_s : 0;
        snap.utc_offset = g.wall_s && g.utc_s ? (int32_t)(g.wall_s - g.utc_s) : 0;
        snap.battery = g.batt_known ? g.battery : -1;
        snap.plugged = g.plugged;
        snap.fw = g.fw_version;
        snap.update = g.update_waiting() ? g.update_ver : 0;
        snap.valid = true;
        if (!last_nets || now - last_nets >= 10000) { last_nets = now ? now : 1; nets_n = wifi_known(nets, WIFI_KNOWN_MAX); }
        xSemaphoreGive(mx);
    }
    // the radio policy; the WiFi setup (or one about to start) takes the radio from everyone
    int w = g.wifi_req == WREQ_START ? LINK_OFF : g.link(net_sample_waiting());
    if (w != want) { want = w; if (task) xTaskNotifyGive(task); }
    Result r;
    while (xQueueReceive(results, &r, 0) == pdTRUE) {
        if (r.kind == R_ANSWER) g.listen_result(r.answer);
        else if (r.kind == R_CONFIG) g.apply_settings(r.rev, r.page, r.present);
        else g.settings_synced(r.mark);
    }
}
