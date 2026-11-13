#pragma once
// When the child's answer starts and stops, from the RMS of each 20 ms microphone frame (audio.cpp records
// it). Header-only, so test/test_vad.cpp runs it on the host.
enum { VAD_FRAME_MS = 20, VAD_MIN_RMS = 400,   // about -38 dBFS: quieter than this is never speech
       VAD_MAX_THR = 1500,                    // about -27 dBFS: a word said into the room frames cannot deafen it
       VAD_START_FRAMES = 2, VAD_STOP_MS = 800, VAD_WAIT_MS = 5000, VAD_ROOM_FRAMES = 5 };
enum VadStep { VAD_WAIT, VAD_START, VAD_ON, VAD_END };
struct Vad { int thr, above, quiet_ms, waited; bool speech; };

// The room, from the VAD_ROOM_FRAMES taken before the child is told she may speak (never from her answer):
// their middle value, so neither the speaker's fading tail nor a frame of the ADC settling decides it.
// Speech is 3x louder than the room, at least VAD_MIN_RMS and at most VAD_MAX_THR (shortcut: a fixed cap,
// retune it from recordings on the board if a loud room starts every recording at once).
inline Vad vad_begin(const int* room) {
    int r[VAD_ROOM_FRAMES];
    for (int i = 0; i < VAD_ROOM_FRAMES; i++) {
        int j = i;
        for (; j > 0 && r[j - 1] > room[i]; j--) r[j] = r[j - 1];
        r[j] = room[i];
    }
    int floor = r[VAD_ROOM_FRAMES / 2];
    int thr = floor * 3 < VAD_MIN_RMS ? VAD_MIN_RMS : floor * 3 > VAD_MAX_THR ? VAD_MAX_THR : floor * 3;
    Vad v = {thr, 0, 0, 0, false};
    return v;
}

// One frame: still waiting, speech starts now (VAD_START_FRAMES loud frames in a row), goes on, or the end
// (VAD_STOP_MS quiet after speech, or VAD_WAIT_MS without any).
inline int vad_step(Vad& v, int rms) {
    if (!v.speech) {
        v.above = rms > v.thr ? v.above + 1 : 0;
        if (v.above >= VAD_START_FRAMES) { v.speech = true; return VAD_START; }
        return (v.waited += VAD_FRAME_MS) >= VAD_WAIT_MS ? VAD_END : VAD_WAIT;
    }
    v.quiet_ms = rms > v.thr ? 0 : v.quiet_ms + VAD_FRAME_MS;
    return v.quiet_ms >= VAD_STOP_MS ? VAD_END : VAD_ON;
}
