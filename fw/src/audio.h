#pragma once
// Plays the core's sound cues through the ES8311 codec on a background task.
bool audio_begin();          // after Wire.begin(); false if the codec does not answer
void audio_play(int sfx);    // non-blocking; a new cue cuts the current one short
void audio_level(int level);  // 0 normal, 1 low, 2 off (parent setting)
bool audio_busy();           // true while a melody is still sounding (keeps the CPU awake)
