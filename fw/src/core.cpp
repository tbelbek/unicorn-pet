// The game core lives in ../../core so the browser emulator and host tests build the
// exact same files; this unity file pulls it into the PlatformIO build.
#include "../../core/game.cpp"
#include "../../core/render.cpp"
#include "../../core/sound.cpp"
#include "../../core/qr.cpp"
