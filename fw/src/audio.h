#pragma once
#include <stdint.h>
// Sound and the microphone, through the ES8311 codec on a background task (the audio task).
bool audio_begin();          // after Wire.begin(); false if the codec does not answer
// A cue: her routine clip (routines_tr.bin) if clip >= 0, else her spoken line (up::V_*) in lang (with the
// child's name from voice_name.bin when lang is Turkish and the pack is there), else the sfx tune.
// A new cue cuts the old one short.
void audio_play(int sfx, int line = 0, int lang = 0, int clip = -1);
void audio_level(int level);  // 0 normal, 1 low, 2 off (parent setting)
bool audio_busy();           // a tune, a line or a recording is under way (keeps the CPU awake)
void audio_set_rail(void (*rail)(bool on));   // the codec's analog supply, switched with audio_sleep/wake
void audio_sleep();          // main task: the codec suspended and I2S stopped while nothing plays (woken by play/listen)

// Listening (up::LK_*): after whatever is playing, the task records from the codec's ADC, 16 kHz mono.
//   LK_YESNO / LK_OPEN: from the first speech until 0.8 s of silence, at most 6 s (nothing in 5 s: silent)
//   LK_LEVEL: about 1 s, only the level is kept     LK_TV: 5 s
void audio_mic(bool on);     // main task only (I2C): the codec's microphone path powered up or down
void audio_listen(int kind);
void audio_listen_cancel();  // the request waiting and the capture under way
void audio_routine_stop();    // her routine clip: the queued one is dropped, a playing one stops
bool audio_listening();       // a request waits or a capture runs (the microphone stays powered)
bool audio_listen_started();  // once per recording: the child can speak now (core: listen_started)
struct Heard { int kind; bool speech; int db, loud_pct; const uint8_t* wav; uint32_t len; };
bool audio_heard(Heard* out); // a finished recording; wav (a whole WAV file) stays valid until the next audio_listen
extern int audio_mic_slot;   // bench: 0 left, 1 right
void audio_codec_check();  // bench: "CODEC awake=.. diff: REG:boot>now ..."
void audio_mic_gain(int g);  // bench: es8311 mic gain step 0..7 (0, 6 .. 42 dB)
