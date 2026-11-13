#pragma once
#include <stdint.h>
#include "../../core/unicorn.h"
// The home listener (server/API.md): her questions' answers, the journal, TV samples, and the parent page's
// settings. One background task owns this traffic and follows the core's radio policy (Game::link): the radio
// is off by default, comes up when she starts asking (so the link is ready for the answer) or a TV sample
// waits, and stays up on USB power (settings polled every 60 s, the journal sent at once). It shares the
// station link with the clock sync and the update (wifi_join/wifi_leave count the users) and gives way to a
// parent's WiFi setup. The game core is only touched on the main task, in net_poll().
void net_begin();                          // setup(), after store_begin() and wifi_setup_begin()
void net_poll(up::Game& g, uint32_t now_ms);   // main task, every loop: policy, snapshot, results into the core
bool net_busy();                           // the link is held or work waits: no light sleep (modem sleep only)
bool net_sample_waiting();
// main task: hand over a finished recording (copied)
void net_answer(int question, int kind, const uint8_t* wav, uint32_t len);
void net_tv_sample(const uint8_t* wav, uint32_t len);
// another task that holds the link anyway (the clock sync): settings and journal now, on that task
void net_sync_here();
