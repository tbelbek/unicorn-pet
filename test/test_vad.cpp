// The firmware's answer detection (fw/src/vad.h): when the child's answer starts and stops.
//   make -C test vad
#include <cassert>
#include <cstdio>
#include "../fw/src/vad.h"

// frames of `rms` for `ms`; returns the step of the last frame and counts the frames until speech began
static int feed(Vad& v, int rms, int ms, int* start_at = 0, int* frames = 0) {
    int st = VAD_WAIT;
    for (int t = 0; t < ms && st != VAD_END; t += VAD_FRAME_MS) {
        st = vad_step(v, rms);
        if (frames) ++*frames;
        if (st == VAD_START && start_at) *start_at = *frames;
    }
    return st;
}

int main() {
    int quiet[VAD_ROOM_FRAMES] = {150, 160, 140, 155, 150};
    {   // the child answers at once, 800 ms at RMS 2000, then a pause: speech, from its second frame
        Vad v = vad_begin(quiet);
        int start = 0, frames = 0;
        assert(feed(v, 2000, 800, &start, &frames) == VAD_ON);
        assert(v.speech && start == VAD_START_FRAMES);
        assert(feed(v, 150, 2000) == VAD_END);
    }
    {   // the room frames: the speaker's fading tail and a settling zero do not set the threshold
        int odd[VAD_ROOM_FRAMES] = {9000, 0, 150, 160, 7000};
        Vad v = vad_begin(odd);
        assert(v.thr == 480);
    }
    {   // a word into the room frames cannot make her deaf to the rest of the answer
        int loud[VAD_ROOM_FRAMES] = {2500, 2600, 2400, 2500, 2550};
        Vad v = vad_begin(loud);
        assert(v.thr == VAD_MAX_THR);
        assert(feed(v, 2000, 100) == VAD_ON);
    }
    {   // a dead quiet room: never below VAD_MIN_RMS
        int zero[VAD_ROOM_FRAMES] = {0, 0, 0, 0, 0};
        assert(vad_begin(zero).thr == VAD_MIN_RMS);
    }
    {   // nobody speaks: the end after VAD_WAIT_MS, no speech; a single loud frame (a click) is not speech
        Vad v = vad_begin(quiet);
        int frames = 0;
        assert(feed(v, 150, 1000) == VAD_WAIT);
        assert(vad_step(v, 5000) == VAD_WAIT && vad_step(v, 150) == VAD_WAIT);
        assert(feed(v, 150, 10000, 0, &frames) == VAD_END && !v.speech);
        assert(v.waited == VAD_WAIT_MS);
    }
    {   // speech with a short pause goes on; VAD_STOP_MS of quiet ends it
        Vad v = vad_begin(quiet);
        feed(v, 3000, 300);
        assert(feed(v, 150, VAD_STOP_MS - VAD_FRAME_MS) == VAD_ON);
        assert(feed(v, 3000, 200) == VAD_ON);
        assert(feed(v, 150, VAD_STOP_MS) == VAD_END);
    }
    std::printf("answer detection: ok\n");
    return 0;
}
