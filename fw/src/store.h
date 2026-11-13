#pragma once
#include <stdint.h>
// The toy's private files: a microSD card (folder /unicorn/) wins, else the internal LittleFS partition
// (the "spiffs" one of default_16MB.csv), filled from fw/personal/ by `pio run -d fw -t uploadfs`.
//   voice_name.bin, routines_tr.bin   her lines with the child's name, the routine questions (tools/voice.py)
//   config.json                       {"listener": "host:port", "token": "..."} (server/API.md)
//   journal/                          open answers recorded while the listener was away (uploaded later)
// None of them has to exist: then she says her lines without a name and asks nothing.
enum { PACK_NAMES, PACK_ROUTINES, PACKS };
struct Pack { const uint8_t* data; uint32_t len; };
struct ListenerConf { char host[64]; uint16_t port; char token[80]; };

void store_begin();                       // setup(): mount the card and LittleFS, read config.json
void store_load_packs();                  // after the first frame: the packs into PSRAM, in a task
bool store_pack(int which, Pack* out);    // false: absent, damaged, or still loading
bool store_conf(ListenerConf* out);       // false: no config.json (no listener)
const char* store_where();                // "SD", "LittleFS" or "-" (serial log)

// Journal (the net task only): at most JOURNAL_MAX_BYTES, nothing older than 7 days.
enum { JOURNAL_MAX_BYTES = 512 * 1024, JOURNAL_DAYS = 7 };
bool journal_add(const uint8_t* wav, uint32_t len, int question, uint32_t local_s);
// the oldest entry: its path, question index and local time; false when the journal is empty
bool journal_oldest(char* path, int n, int* question, uint32_t* local_s);
uint32_t journal_read(const char* path, uint8_t* buf, uint32_t cap);
void journal_remove(const char* path);
void journal_expire(uint32_t now_local_s);
