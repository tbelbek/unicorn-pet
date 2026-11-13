// Signed firmware updates (see ota.h and the README's "Firmware updates").
//
// Trust: a release is firmware.bin plus manifest.json {version, size, sha256, sig}; sig is an ECDSA P-256
// signature over "unicorn-pet|<version>|<size>|<sha256>" by the key whose public half is release_key.h.
// The image is streamed into the inactive OTA slot while it is hashed, and only an image whose size and
// SHA-256 match a correctly signed manifest with a higher version than ours is ever made bootable.
// So TLS adds nothing to integrity, and its certificate is deliberately not validated (setInsecure):
// no CA bundle to keep current, no clock needed for certificate dates. Someone on the path can still
// block updates, or offer an older signed release that is newer than this one; never an image we did
// not sign, and never a downgrade.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "ota.h"
#include "clock.h"
#include "manifest.h"
#include "release_key.h"
#include "version.h"
#include "wifi_setup.h"

using namespace up;

static const char URL_MANIFEST[] = "https://github.com/tbelbek/unicorn-pet/releases/latest/download/manifest.json";
static const char URL_FIRMWARE[] = "https://github.com/tbelbek/unicorn-pet/releases/latest/download/firmware.bin";

// Arduino marks a freshly installed image as good the moment it boots. Wait instead until it has run
// its loop and drawn for ~10 s (ota_poll): an image that crashes before that rolls back on the next reset.
extern "C" bool verifyRollbackLater() { return true; }

// ---- the task -> main loop report: stage, percent and version in one word, so never torn ----------

static std::atomic<uint32_t> report{0};
static std::atomic<bool> running{false};
static uint32_t pack(int stage, int pct, int ver) { return (uint32_t)stage | (uint32_t)pct << 8 | (uint32_t)ver << 16; }
static void say(int stage, int pct, int ver) { report = pack(stage, pct, ver); }

// ---- checking a manifest -------------------------------------------------------------------------

static bool sig_ok(const Manifest& m) {
    char msg[128];
    int len = ota_message(m, msg, sizeof msg);
    uint8_t hash[32], sig[80];
    size_t sig_len = 0;
    if (len <= 0 || len >= (int)sizeof msg || mbedtls_sha256((const uint8_t*)msg, len, hash, 0) != 0 ||
        mbedtls_base64_decode(sig, sizeof sig, &sig_len, (const uint8_t*)m.sig, strlen(m.sig)) != 0)
        return false;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    // PEM: the length counts the terminating NUL. An empty key (no release key yet) never parses.
    bool ok = mbedtls_pk_parse_public_key(&pk, (const uint8_t*)RELEASE_PUBKEY, sizeof RELEASE_PUBKEY) == 0 &&
              mbedtls_pk_get_type(&pk) == MBEDTLS_PK_ECKEY && mbedtls_pk_get_bitlen(&pk) == 256 &&
              mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof hash, sig, sig_len) == 0;
    mbedtls_pk_free(&pk);
    return ok;
}

// GET a release asset: github.com redirects (twice) to its download host. Certificates: see the top.
static int get(HTTPClient& http, NetworkClientSecure& tls, const char* url) {
    // mbedTLS takes internal RAM only here (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC): ~45 KB in all, the biggest
    // piece a 16 KB record buffer. Asking for one 60 KB block failed on the board with WiFi up (the largest
    // block was 59 KB, free 125 KB): the total and one buffer-sized block is what a session needs.
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 56000 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 24000) {
        Serial.println("update: not enough internal RAM for TLS");
        return -1;
    }
    tls.setInsecure();
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setConnectTimeout(10000);
    http.setTimeout(15000);
    if (!http.begin(tls, url)) return -1;
    return http.GET();
}

// The manifest, fetched and checked: 0, UST_LATEST (none published), UST_FAILED (no answer) or UST_BAD_SIG.
static int fetch_manifest(Manifest* m) {
    NetworkClientSecure* tls = new NetworkClientSecure;   // on the heap: the TLS context is a few KB
    char body[1024];
    int len = -1, n = 0, code;
    {
        HTTPClient http;
        code = get(http, *tls, URL_MANIFEST);
        Serial.printf("update: manifest HTTP %d\n", code);
        if (code == 200) len = http.getSize();   // GitHub always sends a length
        if (len > 0 && len < (int)sizeof body) {
            NetworkClient* s = http.getStreamPtr();
            for (uint32_t t = millis(); n < len && millis() - t < 10000;) {
                int r = s->available() ? s->read((uint8_t*)body + n, len - n) : 0;
                if (r > 0) { n += r; t = millis(); } else if (!s->connected()) break; else delay(5);
            }
        }
        http.end();
    }
    delete tls;
    if (code == 404) return UST_LATEST;    // no release published yet: nothing newer, not a failure
    if (len <= 0 || n != len) return UST_FAILED;
    body[n] = 0;
    if (!parse_manifest(body, m) || !sig_ok(*m)) return UST_BAD_SIG;
    return 0;
}

int ota_latest() {
    Manifest m;
    return fetch_manifest(&m) ? 0 : (int)m.version;
}

// ---- installing ----------------------------------------------------------------------------------

static int install(int* ver) {
    Manifest m;
    if (int e = fetch_manifest(&m)) return e;
    *ver = (int)m.version;
    if (m.version <= FW_VERSION) return UST_LATEST;        // nothing newer: no downgrade, no replay
    const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
    if (!slot || m.size > slot->size || m.size < 4096) return UST_FAILED;
    static uint8_t buf[4096];                               // static: one update at a time, off the stack
    NetworkClientSecure* tls = new NetworkClientSecure;
    int result = UST_FAILED;
    {
        HTTPClient http;
        if (get(http, *tls, URL_FIRMWARE) == 200 && http.getSize() == (int)m.size && Update.begin(m.size)) {
            mbedtls_sha256_context sha;
            mbedtls_sha256_init(&sha);
            mbedtls_sha256_starts(&sha, 0);
            NetworkClient* s = http.getStreamPtr();
            uint32_t got = 0;
            for (uint32_t t = millis(); got < m.size && millis() - t < 20000;) {   // 20 s without a byte: give up
                size_t want = m.size - got < sizeof buf ? m.size - got : sizeof buf;
                int r = s->available() ? s->read(buf, want) : 0;
                if (r <= 0) { if (!s->connected()) break; delay(2); continue; }
                mbedtls_sha256_update(&sha, buf, r);
                if (Update.write(buf, r) != (size_t)r) break;   // a flash error, or not an ESP32 image
                got += r;
                t = millis();
                say(UST_DOWNLOADING, (int)((uint64_t)got * 100 / m.size), 0);
            }
            say(UST_VERIFYING, 100, 0);
            uint8_t digest[32];
            char hex[65];
            mbedtls_sha256_finish(&sha, digest);
            mbedtls_sha256_free(&sha);
            for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
            if (got != m.size) Update.abort();
            else if (strcmp(hex, m.sha256)) { Update.abort(); result = UST_BAD_SIG; }   // not the signed image
            // strict end(): every byte is there, and the image's own checksum is checked before it is
            // made the boot image
            else if (Update.end()) result = UST_RESTARTING;
            else { Serial.printf("update: %s\n", Update.errorString()); Update.abort(); }
        }
        http.end();
    }
    delete tls;
    return result;
}

struct Job { bool install; int n; WifiNet nets[WIFI_KNOWN_MAX]; };   // a copy for the task

static void task(void* arg) {
    Job* job = (Job*)arg;
    int stage, ver = 0;
    bool joined = wifi_join(job->nets, job->n);
    if (!joined) stage = UST_NO_WIFI;
    else if (job->install) stage = install(&ver);
    else {
        Manifest m;
        stage = fetch_manifest(&m);
        if (!stage) { ver = (int)m.version; stage = m.version > FW_VERSION ? UST_AVAILABLE : UST_LATEST; }
    }
    delete job;
    if (joined) wifi_leave();              // (the link is shared: it stays up if another task holds it)
    Serial.printf("update: stage %d, version %d\n", stage, ver);
    say(stage, stage == UST_RESTARTING ? 100 : 0, ver);
    running = false;
    vTaskDelete(nullptr);
}

// ---- main task ------------------------------------------------------------------------------------

static uint32_t applied;                   // the report the game has
static bool trial;                         // a new image that has not proved itself yet
static uint32_t first_push, pushes;
static int seen;                           // from the clock sync

void ota_begin(Game& g) {
    g.fw_version = FW_VERSION;
    esp_ota_img_states_t st;
    trial = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
            st == ESP_OTA_IMG_PENDING_VERIFY;   // a USB flash is not on trial (its state is undefined)
    Serial.printf("firmware v%d%s\n", FW_VERSION, trial ? " on trial: kept after 10 s of frames" : "");
}

void ota_frame_pushed(uint32_t now_ms) { if (!pushes++) first_push = now_ms; }

bool ota_busy() { return running; }

void ota_seen(int version) { if (version > 0) seen = version; }

bool ota_poll(Game& g, uint32_t now_ms, uint32_t flags, bool power_ok) {
    if (trial && pushes >= 20 && now_ms - first_push >= 10000) {   // it ran its loop and drew: keep it
        trial = false;
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("firmware confirmed");
    }
    if (seen) { g.update_status(g.update_stage, g.update_pct, seen); seen = 0; }
    uint32_t r = report;
    if (r != applied) { applied = r; g.update_status(r & 0xFF, (r >> 8) & 0xFF, (int)(r >> 16)); }
    if (flags & F_UPDATE) {
        int req = g.update_req;
        g.update_req = UREQ_NONE;
        if (running || wifi_setup_busy() || clock_busy()) {
            g.update_req = (u8)req;                // one radio owner: soon
        } else if (req == UREQ_INSTALL && (!g.update_allowed() || !power_ok)) {   // the battery rule, on fresh PMU readings
            g.update_status(UST_AVAILABLE, 0, 0);
        } else if (req == UREQ_CHECK || req == UREQ_INSTALL) {
            Job* job = new Job();
            job->install = req == UREQ_INSTALL;
            job->n = wifi_known(job->nets, WIFI_KNOWN_MAX);
            int start = job->install ? UST_DOWNLOADING : UST_CHECKING;
            if (!job->n) { delete job; g.update_status(UST_NO_WIFI, 0, 0); return false; }
            report = applied = pack(start, 0, 0);  // a stale result from the last run is not shown
            running = true;
            if (xTaskCreatePinnedToCore(task, "ota", 16384, job, 1, nullptr, 0) != pdPASS) {
                running = false;
                delete job;
                g.update_status(UST_FAILED, 0, 0);
            }
        }
    }
    return !running && g.update_stage == UST_RESTARTING && (applied & 0xFF) == UST_RESTARTING;
}
