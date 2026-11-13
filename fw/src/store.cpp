// The private files on the microSD card or the internal LittleFS (see store.h).
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "store.h"

// Waveshare's own pins for this board's card slot (examples/arduino*/libraries/Mylibrary/pin_config.h and
// 14_LVGL_SD_Test: SD_MMC.setPins(2, 1, 3), 1-bit mode).
static const int SD_CLK = 2, SD_CMD = 1, SD_D0 = 3;
static bool sd_ok, lfs_ok;
static Pack packs[PACKS];
static volatile bool packs_ready;
static ListenerConf conf;
static bool conf_ok;
static const char* const NAMES[PACKS] = {"voice_name.bin", "routines_tr.bin"};

// a private file, from the card if it has one, else from LittleFS (opened for reading)
static File open_private(const char* name) {
    char path[48];
    if (sd_ok) {
        snprintf(path, sizeof path, "/unicorn/%s", name);
        if (SD_MMC.exists(path)) return SD_MMC.open(path, "r");
    }
    snprintf(path, sizeof path, "/%s", name);
    if (lfs_ok && LittleFS.exists(path)) return LittleFS.open(path, "r");
    return File();
}

const char* store_where() { return sd_ok ? "SD" : lfs_ok ? "LittleFS" : "-"; }

static void read_conf() {
    File f = open_private("config.json");
    if (!f) return;
    if (f.size() > 2048) { f.close(); return; }   // not ours
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (e) { Serial.println("config.json: not JSON"); return; }
    const char* listener = doc["listener"] | "";
    const char* token = doc["token"] | "";
    const char* colon = strrchr(listener, ':');
    long port = colon ? strtol(colon + 1, nullptr, 10) : 8765;
    int host_len = colon ? (int)(colon - listener) : (int)strlen(listener);
    if (host_len <= 0 || host_len >= (int)sizeof conf.host || port <= 0 || port > 65535 ||
        strlen(token) >= sizeof conf.token) { Serial.println("config.json: no usable listener"); return; }
    memcpy(conf.host, listener, host_len);
    conf.host[host_len] = 0;
    conf.port = (uint16_t)port;
    snprintf(conf.token, sizeof conf.token, "%s", token);
    conf_ok = true;
    Serial.printf("listener: %s:%u (token %s)\n", conf.host, conf.port, *conf.token ? "set" : "missing");   // never the token
}

void store_begin() {
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
    sd_ok = SD_MMC.begin("/sdcard", true) && SD_MMC.cardType() != CARD_NONE;   // 1-bit mode
    if (!sd_ok) SD_MMC.end();
    lfs_ok = LittleFS.begin(false, "/littlefs", 6, "spiffs");   // never formats: an empty partition stays empty
    Serial.printf("private files: card %s, LittleFS %s\n", sd_ok ? "yes" : "no", lfs_ok ? "yes" : "no");
    read_conf();
}

bool store_conf(ListenerConf* out) {
    if (!conf_ok) return false;
    *out = conf;
    return true;
}

static void load_task(void*) {
    for (int i = 0; i < PACKS; i++) {
        File f = open_private(NAMES[i]);
        if (!f) continue;
        uint32_t n = f.size();
        uint8_t* buf = n >= 8 && n <= 6 * 1024 * 1024 ? (uint8_t*)ps_malloc(n) : nullptr;
        uint32_t got = buf ? f.read(buf, n) : 0;
        f.close();
        if (!buf || got != n || memcmp(buf, "UPO1", 4)) { free(buf); Serial.printf("%s: unreadable\n", NAMES[i]); continue; }
        packs[i].data = buf;               // published whole, before packs_ready
        packs[i].len = n;
        Serial.printf("%s: %u KB\n", NAMES[i], (unsigned)(n / 1024));
    }
    packs_ready = true;
    vTaskDelete(nullptr);
}

void store_load_packs() {
    if (xTaskCreatePinnedToCore(load_task, "packs", 6144, nullptr, 1, nullptr, 0) != pdPASS) packs_ready = true;
}

bool store_pack(int which, Pack* out) {
    if (!packs_ready || which < 0 || which >= PACKS || !packs[which].data) return false;
    *out = packs[which];
    return true;
}

// ---- journal ----------------------------------------------------------------------------------

static fs::FS* jfs(const char** dir) {
    *dir = sd_ok ? "/unicorn/journal" : "/journal";
    if (sd_ok) return &SD_MMC;
    return lfs_ok ? &LittleFS : nullptr;
}

// walk the journal: total bytes, and the oldest entry (by the time in its name)
static uint32_t scan(char* oldest, int n, int* question, uint32_t* local_s) {
    const char* dir;
    fs::FS* fs = jfs(&dir);
    uint32_t total = 0, best = 0xFFFFFFFFu;
    if (oldest && n) oldest[0] = 0;
    if (!fs) return 0;
    File d = fs->open(dir);
    if (!d || !d.isDirectory()) return 0;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        total += f.size();
        unsigned long t = 0;
        int q = -1;
        if (sscanf(f.name(), "%lu_%d.wav", &t, &q) == 2 && t < best && oldest) {
            best = t;
            snprintf(oldest, n, "%s/%s", dir, f.name());
            *question = q;
            *local_s = t;
        }
        f.close();
    }
    return total;
}

bool journal_oldest(char* path, int n, int* question, uint32_t* local_s) {
    scan(path, n, question, local_s);
    return path[0] != 0;
}

void journal_remove(const char* path) {
    const char* dir;
    fs::FS* fs = jfs(&dir);
    if (fs) fs->remove(path);
}

uint32_t journal_read(const char* path, uint8_t* buf, uint32_t cap) {
    const char* dir;
    fs::FS* fs = jfs(&dir);
    File f = fs ? fs->open(path, "r") : File();
    if (!f || f.size() > cap) return 0;
    uint32_t n = f.read(buf, f.size());
    f.close();
    return n;
}

void journal_expire(uint32_t now) {
    char path[64];
    int q;
    uint32_t t;
    for (int k = 0; k < 64 && journal_oldest(path, sizeof path, &q, &t) && now > t + JOURNAL_DAYS * 86400u; k++)
        journal_remove(path);
}

bool journal_add(const uint8_t* wav, uint32_t len, int question, uint32_t local_s) {
    const char* dir;
    fs::FS* fs = jfs(&dir);
    if (!fs || len > JOURNAL_MAX_BYTES) return false;
    fs->mkdir(sd_ok ? "/unicorn" : "/");
    fs->mkdir(dir);
    char old[64];
    int q;
    uint32_t t;
    for (int k = 0; k < 64 && scan(old, sizeof old, &q, &t) + len > JOURNAL_MAX_BYTES && old[0]; k++)
        journal_remove(old);               // room for the newest: the oldest go first
    char path[64];
    snprintf(path, sizeof path, "%s/%lu_%d.wav", dir, (unsigned long)local_s, question);
    File f = fs->open(path, "w");
    if (!f) return false;
    bool ok = f.write(wav, len) == len;
    f.close();
    if (!ok) fs->remove(path);
    return ok;
}
