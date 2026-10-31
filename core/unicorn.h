// Unicorn Pet game core. Freestanding C++: no libc, no heap, no STL, so the same
// code runs on the ESP32-S3 firmware, in the browser (wasm32) and in host tests.
#pragma once

namespace up {

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

enum { SCREEN_W = 368, SCREEN_H = 448 };

// Platform -> core
enum Input { IN_NEXT = 1, IN_SELECT = 2, IN_TOUCH = 3, IN_SHAKE = 4 };
// Core -> platform, returned by Game::tick
enum Flags { F_REDRAW = 1, F_SCREEN_ON = 2, F_SAVE = 4, F_SOUND = 8 };   // F_SOUND: call take_sfx()

// Sound cues. The melodies live in sound.cpp so the board and the emulator play the same notes.
enum Sfx { SFX_NONE, SFX_TAP, SFX_HATCH, SFX_EAT, SFX_PLAY, SFX_WASH, SFX_SLEEP, SFX_WAKE, SFX_REFUSE,
           SFX_INFO, SFX_COUNT };
struct Note { u16 hz, ms; };             // hz = 0 is a rest
const Note* melody(int sfx, int* count);

enum Stat { HUNGER, HAPPY, ENERGY, CLEAN, NSTATS };
enum { MAX_STAT = 10000 };

enum Scene { SC_EGG, SC_HATCH, SC_HOME, SC_EAT, SC_PLAY, SC_WASH, SC_SLEEP, SC_INFO, SC_REFUSE };
enum Mood { M_IDLE, M_HAPPY, M_HUNGRY, M_SLEEPY, M_SAD };
enum Menu { MENU_FEED, MENU_PLAY, MENU_SLEEP, MENU_WASH, MENU_COUNT };

// Tunables (per game minute unless noted). Time only passes while the device is on:
// powered off = the unicorn sleeps and nothing decays.
enum {
    DECAY_HUNGER = 20, DECAY_HAPPY = 16, DECAY_ENERGY = 12, DECAY_CLEAN = 10,   // ~8-17 h to empty
    SLEEP_ENERGY_GAIN = 85,              // ~2 h from empty to full
    FEED_GAIN = 3500, PLAY_HAPPY = 3000, PLAY_COST = 1000, PLAY_HUNGER = 500, PLAY_DIRT = 500,
    TREAT_HAPPY = 300, PLAY_MIN_ENERGY = 1500,
    NEED_LOW = 2500,                           // below this a need shows on the face
    ACTION_MS = 4000, REFUSE_MS = 2500, HATCH_MS = 3000, INFO_MS = 6000,
    EGG_HATCH_MS = 60000, EGG_TAPS = 5,
    SCREEN_OFF_MS = 20000, AUTOSAVE_MS = 5 * 60000,
};

// Persisted blob. Saved after every action and every AUTOSAVE_MS; the PWR key on this
// board is a hardware power cut, so there is no "save on shutdown".
struct Save {
    u8 version, character, hatched, muted;
    u32 age_s;                // powered lifetime since hatching
    u16 stat[NSTATS];
    u32 egg_ms;
    u32 check;                // FNV-1a over everything above
};
enum { SAVE_VERSION = 1, SAVE_SIZE = sizeof(Save) };

struct Game {
    Save s;
    u8 scene, menu, screen_on, frame, taps, dirty, want_save, sfx;
    u32 scene_ms, idle_ms, anim_ms, minute_ms, sec_ms, save_ms, rng;

    void init(u32 seed);                         // fresh egg
    bool load(const u8* data, u32 len);          // false -> corrupt/other version, stays fresh
    u32 save(u8* out) const;                     // returns SAVE_SIZE
    u32 tick(u32 dt_ms);                         // advance time, returns Flags
    void input(int type, int x, int y);
    int take_sfx();                              // pending cue (once), SFX_NONE if muted

    Mood mood() const;
    int stat(int i) const { return s.stat[i]; }
    void set_stat(int i, int v);                 // debug / tests

private:
    void go(u8 sc);
    void act(int item);
    void minute();
    void hatch();
    void cue(int id) { sfx = (u8)id; }
    u32 random();
};

void render(const Game& g, u16* fb);             // fb = SCREEN_W * SCREEN_H RGB565

// Hit boxes shared by input handling and rendering.
enum { MENU_Y0 = 368, MENU_Y1 = 434, MENU_X0 = 10, MENU_STEP = 92 };
enum { PET_X0 = 64, PET_Y0 = 100, PET_X1 = 304, PET_Y1 = 350 };
enum { SOUND_X0 = 236, SOUND_Y1 = 48 };          // info screen: sound on/off toggle, top right

}  // namespace up
