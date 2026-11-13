#pragma once
#include <stdint.h>
#include "../../core/unicorn.h"
// Firmware updates over WiFi: signed GitHub releases (tools/release.py), checked and downloaded by a
// background task like the clock sync, verified before the new image may boot. The game core draws the
// page (settings > SAAT > GÜNCELLEME); all I2C and NVS stay on the main task.
void ota_begin(up::Game& game);           // in setup(): the running version; is this a new image on trial?
bool ota_poll(up::Game& game, uint32_t now_ms, uint32_t flags, bool power_ok);   // power_ok: USB in or battery >= 50 %, read just now;   // every loop; true: save and restart now
void ota_frame_pushed(uint32_t now_ms);   // a frame was drawn: a new image on trial is kept after 10 s of them
bool ota_busy();                          // a check or download holds the radio: no sync, no setup, no light sleep
int ota_latest();                         // in a WiFi task, connected: the newest signed version, 0 if unknown
void ota_seen(int version);               // main task: hand that version on to the settings (0: nothing)
