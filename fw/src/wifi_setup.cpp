// WiFi from the toy, run from the main loop without blocking it (the unicorn keeps playing).
// Radio: scan once (while nobody is joined yet), then AP+STA with the toy's own network up; the
// home network is tried with the channel and BSSID from that scan, so the radio does not hop.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_random.h>
#include "wifi_setup.h"
#include "clock.h"
#include "portal.gen.h"
#if __has_include("secrets.h")
#include "secrets.h"
#endif

using namespace up;

static const uint32_t SESSION_MS = 5 * 60000, JOIN_MS = 25000, NTP_MS = 15000, OK_LINGER_MS = 60000,
                      RESULT_MS = 15000;   // a result is shown at least this long, even past the session

// ---- saved networks: one NVS blob, written in one go ----------------------------------------

struct Saved { uint8_t n; WifiNet net[WIFI_SAVED_MAX]; };
static Saved saved;
static Preferences store;

#ifdef WIFI_NETWORKS
static const struct { const char* ssid; const char* pass; } BUILT_IN[] = WIFI_NETWORKS;
static const int BUILT_IN_N = sizeof BUILT_IN / sizeof BUILT_IN[0];
#else
static const struct { const char* ssid; const char* pass; } BUILT_IN[1] = {{"", ""}};
static const int BUILT_IN_N = 0;
#endif

static void persist() { store.putBytes("list", &saved, sizeof saved); }

static void show_list(Game& g) {           // saved ones first (they can be forgotten), then built-in
    g.wifi_list_clear();
    for (int i = 0; i < saved.n; i++) g.wifi_list_add(saved.net[i].ssid, false);
    for (int i = 0; i < BUILT_IN_N; i++) g.wifi_list_add(BUILT_IN[i].ssid, true);
}

int wifi_known(WifiNet* out, int max) {
    int n = 0;
    for (int i = 0; i < saved.n && n < max; i++) out[n++] = saved.net[i];
    for (int i = 0; i < BUILT_IN_N && n < max; i++) {
        bool dup = false;
        for (int j = 0; j < n; j++) dup |= !strcmp(out[j].ssid, BUILT_IN[i].ssid);
        if (dup) continue;
        snprintf(out[n].ssid, sizeof out[n].ssid, "%s", BUILT_IN[i].ssid);
        snprintf(out[n].pass, sizeof out[n].pass, "%s", BUILT_IN[i].pass);
        n++;
    }
    return n;
}

void wifi_setup_begin(Game& g) {
    // Passwords live only in our own list (and secrets.h): the WiFi driver must not keep a copy of
    // its own in NVS, or "forget" would leave one behind. Applies to the clock sync's joins too.
    WiFi.persistent(false);
    store.begin("wifi", false);
    if (store.getBytesLength("list") != sizeof saved || store.getBytes("list", &saved, sizeof saved) != sizeof saved ||
        saved.n > WIFI_SAVED_MAX)
        memset(&saved, 0, sizeof saved);
    show_list(g);
}

// ---- the session -----------------------------------------------------------------------------

enum SetupPhase { IDLE, SCANNING, OPEN, JOINING, ADDRESS, CLOCK, DONE };
static SetupPhase phase = IDLE;
// Bound to the toy's own network only (192.168.4.1): phones on the home LAN cannot even connect.
// The server and DNS run in their own task: WebServer reads a request body synchronously, so a
// phone that trickles a huge POST would otherwise stall the game loop. Handlers never touch the
// game or start the radio: /connect only queues the request; the main loop acts on it.
static WebServer server(IPAddress(192, 168, 4, 1), 80);
struct JoinReq { WifiNet net; };
static QueueHandle_t joins;
static volatile bool web_on;
static TaskHandle_t web_task;
static DNSServer dns;
static bool routes;
static uint32_t started, step_at;
static char ap_name[24], ap_pass[16];
struct Seen { char ssid[33]; int8_t rssi; uint8_t locked, channel; uint8_t bssid[6]; };
static Seen seen[20];
static int seen_n;
static WifiNet want;                       // the network the parent picked
static int want_seen = -1;
static volatile int disc_reason;           // from the WiFi event task: why a join failed
static volatile uint32_t disc_at;          // and when (a drop from our own disconnect is ignored)
static const char* volatile result = "wait";   // what /status says (phone side), set by the main loop
static volatile bool accepting;            // the main loop takes a /connect now (OPEN or DONE)

static bool from_ap() {                    // only phones on the toy's own network may use the page
    return server.client().localIP() == WiFi.softAPIP();
}

static void json_str(String& out, const char* s) {   // a JSON string; control and non-UTF-8 bytes made safe
    out += '"';
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned char c = *p;
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = len > 0;
        for (int k = 1; ok && k < len; k++) ok = (p[k] & 0xC0) == 0x80;
        if (!ok) { out += '?'; continue; }
        if (len > 1) { for (int k = 0; k < len; k++) out += (char)p[k]; p += len - 1; continue; }
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20) { char e[8]; snprintf(e, sizeof e, "\\u%04x", c); out += e; }
        else out += (char)c;
    }
    out += '"';
}

static void send_json(const String& body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

static void to_page() {                    // captive-portal checks and stray URLs land on the page
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

static void handle_root() {
    if (!from_ap()) { server.send(403, "text/plain", ""); return; }
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", PORTAL_HTML);
}

static void handle_scan() {
    if (!from_ap()) { server.send(403, "text/plain", ""); return; }
    String out = "[";
    for (int i = 0; i < seen_n; i++) {
        if (i) out += ',';
        out += "{\"i\":" + String(i) + ",\"s\":";   // the page sends back i: the exact SSID bytes stay here
        json_str(out, seen[i].ssid);
        out += ",\"r\":" + String(seen[i].rssi) + ",\"l\":" + String(seen[i].locked) + "}";
    }
    send_json(out + "]");
}

static bool expired() { return millis() - started > SESSION_MS; }   // no new tries after this

static void handle_connect() {             // web task: validate and queue, nothing more
    if (!from_ap()) { server.send(403, "text/plain", ""); return; }
    String ssid = server.arg("ssid"), pass = server.arg("pass");
    if (server.hasArg("id")) {             // a network from the scan list, by its index
        int id = server.arg("id").toInt();
        if (id < 0 || id >= seen_n) { send_json("{\"error\":\"stale\"}"); return; }
        ssid = seen[id].ssid;
    }
    bool hex64 = pass.length() == 64;
    for (unsigned i = 0; hex64 && i < 64; i++) hex64 = isxdigit((unsigned char)pass[i]);
    if (!ssid.length() || ssid.length() > 32) { send_json("{\"error\":\"ssid\"}"); return; }
    if (pass.length() && !((pass.length() >= 8 && pass.length() <= 63) || hex64)) {
        send_json("{\"error\":\"len\"}");   // the page words it in the phone's language
        return;
    }
    if (!accepting || expired()) { send_json("{\"error\":\"busy\"}"); return; }
    JoinReq r;
    snprintf(r.net.ssid, sizeof r.net.ssid, "%s", ssid.c_str());
    snprintf(r.net.pass, sizeof r.net.pass, "%s", pass.c_str());
    accepting = false;                     // one at a time; the main loop says when the next may come
    result = "join";                       // what the phone sees until the main loop knows better
    xQueueOverwrite(joins, &r);
    send_json("{\"ok\":true}");
}

static void start_join(Game& g, const WifiNet& net) {   // main loop: the parent's choice
    bool known = false;
    for (int i = 0; i < saved.n; i++) known |= !strcmp(net.ssid, saved.net[i].ssid);
    if (!known && saved.n >= WIFI_SAVED_MAX) {   // refuse plainly: the parent forgets one first
        result = "full";
        phase = DONE;
        step_at = millis();
        accepting = !expired();
        g.wifi_status(WST_FULL, net.ssid);
        return;
    }
    want = net;
    want_seen = -1;
    for (int i = 0; i < seen_n; i++) if (!strcmp(net.ssid, seen[i].ssid)) { want_seen = i; break; }
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false);   // a second network: drop the first (AP stays)
    disc_reason = 0;
    WiFi.setAutoReconnect(false);          // a failed join must not keep hopping channels
    step_at = millis();
    if (want_seen >= 0) WiFi.begin(want.ssid, want.pass, seen[want_seen].channel, seen[want_seen].bssid);
    else WiFi.begin(want.ssid, want.pass);
    phase = JOINING;
    result = "join";
    g.wifi_status(WST_JOIN, want.ssid);
}

static void web_loop(void*) {              // the server's own task: a slow phone only slows this
    while (web_on) {
        dns.processNextRequest();
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    server.stop();
    dns.stop();
    web_task = nullptr;
    vTaskDelete(nullptr);
}

static void handle_status() {
    if (!from_ap()) { server.send(403, "text/plain", ""); return; }
    send_json(String("{\"stage\":\"") + result + "\"}");
}

static void start_ap(Game& g) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(ap_name, sizeof ap_name, "Unicorn-%02X%02X", mac[4], mac[5]);
    static const char ALPHA[] = "abcdefghjkmnpqrstuvwxyz23456789";   // nothing that needs escaping or looks alike
    for (int i = 0; i < 12; i++) ap_pass[i] = ALPHA[esp_random() % (sizeof ALPHA - 1)];
    ap_pass[12] = 0;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_name, ap_pass, 1, 0, 2);   // channel 1, visible, at most two phones
    dns.start(53, "*", WiFi.softAPIP());   // every name points here: phones open the page themselves
    if (!joins) joins = xQueueCreate(1, sizeof(JoinReq));
    if (!routes) {
        server.on("/", HTTP_GET, handle_root);
        server.on("/scan", HTTP_GET, handle_scan);
        server.on("/connect", HTTP_POST, handle_connect);
        server.on("/status", HTTP_GET, handle_status);
        server.onNotFound(to_page);        // /generate_204, /hotspot-detect.html, ... -> the page
        routes = true;
    }
    server.begin();
    result = "wait";
    phase = OPEN;
    accepting = true;
    xQueueReset(joins);
    web_on = true;
    if (xTaskCreatePinnedToCore(web_loop, "portal", 8192, nullptr, 1, &web_task, 0) != pdPASS) web_task = nullptr;
    g.wifi_session(ap_name, ap_pass);
}

static void stop_all(Game& g, int stage) {
    if (phase == CLOCK) ntp_stop();
    web_on = false;                        // the web task stops the server and DNS and ends itself
    accepting = false;
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    phase = IDLE;
    g.wifi_status(stage, nullptr);
}

static void finish(Game& g, int stage, const char* phone) {
    result = phone;
    phase = DONE;
    accepting = !expired();                // another try (or network), while the session lasts
    step_at = millis();
    if (stage != WST_OK && stage != WST_OK_NOCLOCK) WiFi.disconnect(false);   // keep the AP for a retry
    g.wifi_status(stage, want.ssid);
}

static void save_want(Game& g) {           // after it has worked: newest first, replacing its old entry
    int at = saved.n;
    for (int i = 0; i < saved.n; i++) if (!strcmp(saved.net[i].ssid, want.ssid)) at = i;
    if (at == saved.n && saved.n < WIFI_SAVED_MAX) saved.n++;
    for (int i = at; i > 0; i--) saved.net[i] = saved.net[i - 1];
    saved.net[0] = want;
    persist();
    show_list(g);
}

bool wifi_setup_busy() { return phase != IDLE; }

void wifi_setup_poll(Game& g, uint32_t now, uint32_t flags) {
    static bool events;
    if (!events) {
        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
            disc_reason = info.wifi_sta_disconnected.reason;
            disc_at = millis();
        }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        events = true;
    }
    if (flags & F_WIFI) {
        int req = g.wifi_req, arg = g.wifi_arg;
        g.wifi_req = WREQ_NONE;
        if (req == WREQ_START && phase == IDLE && !clock_busy() && !web_task) {
            WiFi.mode(WIFI_STA);
            WiFi.scanNetworks(true);       // async: the list is ready before any phone joins
            phase = SCANNING;
            started = now;
        } else if (req == WREQ_START && phase == IDLE) {
            g.wifi_req = WREQ_START;       // a clock sync (or the last session's server) still busy: soon
        } else if (req == WREQ_STOP && phase != IDLE) {
            stop_all(g, WST_OFF);
        } else if (req == WREQ_RETRY && phase == DONE && !expired()) {   // late: the timeout follows
            phase = OPEN;
            result = "wait";
            accepting = true;
            g.wifi_status(WiFi.softAPgetStationNum() ? WST_PHONE : WST_WAIT, nullptr);
        } else if (req == WREQ_FORGET && arg < saved.n) {
            for (int i = arg; i + 1 < saved.n; i++) saved.net[i] = saved.net[i + 1];
            saved.n--;
            persist();
            show_list(g);
        }
    }
    if (phase == IDLE) return;
    if (phase == SCANNING) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        seen_n = 0;
        for (int i = 0; i < n && seen_n < (int)(sizeof seen / sizeof seen[0]); i++) {   // strongest first, no repeats
            String s = WiFi.SSID(i);
            if (!s.length() || s.length() > 32) continue;
            int dup = -1;
            for (int j = 0; j < seen_n; j++) if (s == seen[j].ssid) dup = j;
            if (dup >= 0 && seen[dup].rssi >= WiFi.RSSI(i)) continue;
            Seen& e = dup >= 0 ? seen[dup] : seen[seen_n++];
            snprintf(e.ssid, sizeof e.ssid, "%s", s.c_str());
            e.rssi = (int8_t)WiFi.RSSI(i);
            e.locked = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            e.channel = (uint8_t)WiFi.channel(i);
            memcpy(e.bssid, WiFi.BSSID(i), 6);
        }
        for (int i = 1; i < seen_n; i++)   // strongest first
            for (int j = i; j > 0 && seen[j].rssi > seen[j - 1].rssi; j--) { Seen t = seen[j]; seen[j] = seen[j - 1]; seen[j - 1] = t; }
        WiFi.scanDelete();
        start_ap(g);
        return;
    }
    JoinReq jr;
    if ((phase == OPEN || phase == DONE) && xQueueReceive(joins, &jr, 0) == pdTRUE && !expired()) start_join(g, jr.net);
    uint32_t ms = millis();
    if (phase == OPEN && g.wifi_stage == WST_WAIT && WiFi.softAPgetStationNum()) g.wifi_status(WST_PHONE, nullptr);
    if (phase == JOINING) {
        if (WiFi.status() == WL_CONNECTED) { phase = ADDRESS; result = "addr"; g.wifi_status(WST_ADDR, nullptr); }
        else if ((disc_reason && (int32_t)(disc_at - step_at) > 300) || ms - step_at > JOIN_MS) {   // ours: earlier
            int r = disc_reason;           // 201 no AP found; 2/15/202/204 and friends: the password or security
            if (r == 201 || (!r && want_seen < 0)) finish(g, WST_NOT_FOUND, "notfound");
            else finish(g, WST_BAD_PASS, "badpass");
        }
    }
    if (phase == ADDRESS && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        save_want(g);                      // it works: keep it, even if the clock check fails
        ntp_start();
        phase = CLOCK;
        step_at = ms;
        result = "clock";
        g.wifi_status(WST_CLOCK, nullptr);
    }
    if (phase == CLOCK) {
        uint32_t utc;
        if (ntp_done(&utc)) { ntp_stop(); bool set = clock_set_utc(utc); finish(g, set ? WST_OK : WST_OK_NOCLOCK, set ? "ok" : "noclock"); }
        else if (ms - step_at > NTP_MS) { ntp_stop(); finish(g, WST_OK_NOCLOCK, "noclock"); }
    }
    bool joined = g.wifi_stage == WST_OK || g.wifi_stage == WST_OK_NOCLOCK;
    if (phase == DONE && joined && ms - step_at > OK_LINGER_MS) stop_all(g, WST_OFF);   // TAMAM never tapped
    bool fresh = phase == DONE && ms - step_at < RESULT_MS;                             // the phone polls it
    if ((phase == OPEN || phase == DONE) && !joined && !fresh && now - started > SESSION_MS) {   // never mid-join
        stop_all(g, WST_TIMEOUT);          // the radio is off; the toy offers TEKRAR
    }
}
