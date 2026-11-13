// The game core lives in ../../core so the browser emulator and host tests build the
// exact same files; this unity file pulls it into the PlatformIO build.
// Speed, not size, for the core: Arduino builds with -Os, and the renderer's pixel loops are what every
// frame (and so every touch) waits on.
#pragma GCC optimize("O2")
#include "../../core/game.cpp"
#include "../../core/render.cpp"
#include "../../core/sound.cpp"
#include "../../core/qr.cpp"
