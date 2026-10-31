// Unicorn Pet game core. Freestanding C++: no libc, no heap, no STL, so the same
// code runs on the ESP32-S3 firmware, in the browser (wasm32) and in host tests.
#pragma once

namespace up {

typedef unsigned char u8;
typedef signed char i8;
typedef unsigned short u16;
typedef short i16;
typedef unsigned int u32;

enum { SCREEN_W = 368, SCREEN_H = 448 };

inline int iabs(int v) { return v < 0 ? -v : v; }

// Platform -> core
enum Input { IN_NEXT = 1, IN_SELECT = 2, IN_TOUCH = 3, IN_SHAKE = 4,
             IN_PARENT = 5 };   // platform: BOOT held PARENT_HOLD_MS (door: recall; else: settings)
// Core -> platform, returned by Game::tick
enum Flags { F_REDRAW = 1, F_SCREEN_ON = 2, F_SAVE = 4, F_SOUND = 8,   // F_SOUND: call take_sfx()
             F_CLOCK = 16 };   // a parent set the clock: write Game::wall_s (local) to the RTC

// Sound cues. The melodies live in sound.cpp so the board and the emulator play the same notes.
enum Sfx { SFX_NONE, SFX_TAP, SFX_HATCH, SFX_EAT, SFX_PLAY, SFX_WASH, SFX_SLEEP, SFX_WAKE, SFX_REFUSE,
           SFX_INFO, SFX_PET, SFX_POP, SFX_CHEER, SFX_BYE, SFX_KNOCK, SFX_HELLO,
           SFX_NOTE1, SFX_NOTE2, SFX_NOTE3, SFX_CHOOSE, SFX_LAND, SFX_DRESS, SFX_ECHO, SFX_CHARGE, SFX_COUNT };
struct Note { u16 hz, ms; };             // hz = 0 is a rest
const Note* melody(int sfx, int* count);
void set_echo(const u8* flowers, int n);   // SFX_ECHO plays these flowers' notes back as one tune

enum Stat { HUNGER, HAPPY, ENERGY, CLEAN, NSTATS };
enum { MAX_STAT = 10000 };

// New scenes are appended so existing scene numbers (emulator) stay put.
// LEAVE/AWAY/RETURN: after PLAY_LIMIT_MS of screen time she has "things to do" and goes out,
// leaving a do-not-disturb sign on the door, so the screen stays dark for a while.
enum Scene { SC_EGG, SC_HATCH, SC_HOME, SC_EAT, SC_PLAY, SC_WASH, SC_SLEEP, SC_INFO, SC_REFUSE, SC_GAME,
             SC_LEAVE, SC_AWAY, SC_RETURN, SC_FEED, SC_SETTINGS,     // FEED: pick a snack
             SC_EGG_PICK, SC_TOYS, SC_MUSIC, SC_CLOUD, SC_DRESS,     // choose an egg; Oyna's toy box
             SC_ALBUM,                                               // memories and her garden
             SC_LOUNGE };                                            // plugged in: on the sun lounger
enum Toy { TOY_STARS, TOY_MUSIC, TOY_CLOUD, TOY_DRESS, TOY_HOME, TOY_COUNT };
enum Accessory { ACC_NONE, ACC_BOW, ACC_FLOWER, ACC_TIARA, ACC_COUNT };
// What she dreams of. The first dream of a night comes from what she did that day.
enum Dream { D_SNACK, D_BUBBLES, D_STARS, D_MUSIC, D_CLOUD, D_CROWN, D_HEARTS, D_BUTTERFLY,   // her day
             D_FLOWERS, D_RAINBOW, D_FRIEND, D_BALL, D_MOON, D_EGG, D_COUNT };            // anytime
enum { DREAM_FROM_DAY = D_FLOWERS };       // D_SNACK..D_BUTTERFLY match the Did bits below
enum Did { DID_FEED = 1, DID_WASH = 2, DID_STARS = 4, DID_MUSIC = 8, DID_CLOUD = 16, DID_DRESS = 32,
           DID_PET = 64, DID_BUTTERFLY = 128 };
enum Phase { PH_DAY, PH_EVENING, PH_NIGHT };
// She never dies; she grows up through the days you share. Stages only add looks, never take away.
enum Stage { STAGE_FOAL, STAGE_YOUNG, STAGE_GROWN, STAGE_STORYKEEPER };
enum Party { PARTY_NONE, PARTY_REUNION, PARTY_BIRTHDAY, PARTY_MILESTONE };
enum { LIFE_ADOPTED = 1, MILESTONES = 4 };      // from the wall clock; unknown clock = day
enum Mood { M_IDLE, M_HAPPY, M_HUNGRY, M_SLEEPY, M_SAD };
enum Menu { MENU_FEED, MENU_PLAY, MENU_SLEEP, MENU_WASH, MENU_COUNT };
enum Treat { TREAT_APPLE, TREAT_STRAWBERRY, TREAT_CARROT, TREAT_COUNT };
enum Idle { IDLE_NONE, IDLE_BLINK, IDLE_WALK, IDLE_HOP, IDLE_HUM, IDLE_TWIRL, IDLE_DANCE };   // last 3: tricks
enum Particle { P_NONE, P_HEART, P_SPARKLE, P_BUBBLE, P_CRUMB, P_STAR, P_CONFETTI, P_RING, P_NOTE };
// Parent settings (stored in the save; 0 always means "default")
enum Volume { VOL_NORMAL, VOL_LOW, VOL_OFF };
enum Opt { OPT_CALM = 1, OPT_NO_SHAKE = 2 };      // calm: no bobbing/hops, fewer particles
enum SettingRow { SET_VOLUME, SET_LIMIT, SET_BREAK, SET_BRIGHT, SET_CALM, SET_SHAKE, SET_CLOCK, SET_EXIT, SET_ROWS };
// second settings page: the date and time, and her birthday (left half of a row: -1, right: +1)
enum ClockRow { CLK_YEAR, CLK_MONTH, CLK_DAY, CLK_HOUR, CLK_MIN, CLK_BMONTH, CLK_BDAY, CLK_SAVE, CLK_BACK, CLK_ROWS };

// Tunables (per game minute unless noted). Time only passes while the device is on:
// powered off = the unicorn sleeps and nothing decays.
enum {
    DECAY_HUNGER = 20, DECAY_HAPPY = 16, DECAY_ENERGY = 12, DECAY_CLEAN = 10,   // ~8-17 h to empty
    SLEEP_ENERGY_GAIN = 85,              // ~2 h from empty to full
    FEED_GAIN = 3500, PLAY_HAPPY = 3000, PLAY_COST = 1000, PLAY_HUNGER = 500, PLAY_DIRT = 500,
    TREAT_HAPPY = 300, PLAY_MIN_ENERGY = 1500,
    PET_HAPPY = 200, PET_COOLDOWN_MS = 2000, PET_REACT_MS = 700,   // hearts always, points rationed
    GAME_MS = 15000, GAME_TARGETS = 3, CELEBRATE_MS = 3000,
    NEED_LOW = 2500,                     // below this a need shows on the face
    ACTION_MS = 4000, REFUSE_MS = 2500, HATCH_MS = 3000, INFO_MS = 6000,
    EGG_HATCH_MS = 60000, EGG_TAPS = 5, WOBBLE_MS = 400,
    SCREEN_OFF_MS = 20000, AUTOSAVE_MS = 5 * 60000,
    PLAY_LIMIT_MS = 10 * 60000,          // screen-on play time before she goes out
    AWAY_MS = 30 * 60000,                // how long she stays out (powered time)
    REST_RESET_MS = 15 * 60000,          // this long with the screen dark counts as a break
    AWAY_SCREEN_OFF_MS = 5000,           // the door screen goes dark quickly
    LEAVE_MS = 4600, RETURN_MS = 3000,
    STEP_MS = 100,                       // simulation step; render interpolates between steps
    WALK_RANGE = 40, WALK_SPEED = 2,     // px from centre / px per step (20 px/s)
    PARENT_HOLD_MS = 5000,               // BOOT held this long: door -> call her back, else settings
    NAP_MS = 60000, NAP_BONUS = 2500,    // a one-minute nap is enough to play again
    FEED_PICK_MS = 8000,                 // snack choice waits this long, then takes the highlighted one
    PRESS_MS = 260, SQUASH_MS = 140,     // button press pop / landing squash
    FRIEND_MS = 1400,                    // a friend comes to pick her up
    MODE_IDLE_MS = 12000,                // toy modes go home after this long without a deliberate input
    EGG_HINT_MS = 4000,                  // egg picker: a hand shows what to do
    FOAM_FROM_MS = 400, FOAM_TO_MS = 2800, CROWN_TO_MS = 3300,   // bath: three foam puffs to pop
    VISIT_FIRST_MS = 8000, VISIT_EVERY_MS = 12000, VISIT_MS = 4000,   // butterfly neighbour
    MUSIC_MS = 20000, NOTES = 6, ECHO_WAIT_MS = 1500, ECHO_GAP_MS = 300,   // music garden
    CLOUD_MS = 15000, FEATHERS = 6, FEATHER_EVERY_MS = 2000, FEATHER_FALL = 6,   // tilt-a-cloud
    CLOUD_Y = 310, CLOUD_SPEED = 8, CLOUD_CATCH = 48, TILT_STALE_MS = 500,
    GREET_MS = 2000,                     // after a real break she greets you (stretch or yawn)
    EVENING_MIN = 17 * 60, NIGHT_MIN = 20 * 60, MORNING_MIN = 7 * 60,   // minute of the day
    TUCK_MS = 4000,                      // bedtime: a blanket to tuck her in (or it happens by itself)
    DREAM_FIRST_MS = 5000, DREAM_EVERY_MS = 8000, DREAM_MS = 3000,   // a short dream every 8 s she is seen
    DREAMS = 3, TUCK_PULL_MS = 500,
    ALBUM_PICS = 12, CARE_DAYS_MAX = 9999, // one picture per day she was cared for; the garden grows too
    ALBUM_IDLE_MS = 8000, MEMORY_SHOW_MS = 1600,
    YOUNG_DAYS = 7, GROWN_DAYS = 30, STORYKEEPER_DAYS = 100,   // care days; also milestones, with 365
    PARTY_MS = 4000, MILESTONE_MS = 3000,
    REUNION_DAYS = 7, REUNION_NEED = 8000,   // back after a week or more: a welcome, needs topped up
    DARK_FLOOR = 1500,
    LOUNGE_IDLE_MS = 10000,              // plugged in at home: back to the lounger after this long
    PLUG_QUIET_MS = 5000,                // re-plugged this soon (a loose cable): no fanfare
    HUM_DAYS = 3, TWIRL_DAYS = 7, DANCE_DAYS = 14,   // tricks she learns over the days together
    HUM_MS = 1200, TWIRL_MS = 1200, DANCE_MS = 1600, TRICK_GAP_MS = 12000,                       // with the screen dark, needs never drop below this
    NPART = 16,
};

// Persisted blob. Saved after every action and every AUTOSAVE_MS; the PWR key on this
// board is a hardware power cut, so there is no "save on shutdown". Cosmetic state
// (position, particles, mini-game) is deliberately not saved.
struct Save {
    u8 version, character, hatched, muted;
    u32 age_s;                // powered lifetime since hatching
    u16 stat[NSTATS];
    u32 egg_ms;
    u16 away_s, play_s;       // v2: time left out of the house, screen-time used
    u32 away_until;           // v3: wall-clock second she comes back (0 = no clock, count down)
    u8 volume, limit_min, break_min, bright, opts;           // v4: parent settings, 0 = default
    u8 birth_flags, accessory, last_treat;                    // v5: egg chosen (bit 0), dress-up,
                                                              //     last snack + 1 (0 = none yet)
    u16 memory_day;           // v6: local day of the last album memory (0 = none yet)
    u16 care_days;            //     days she was cared for: album, garden, growing up
    u16 hatch_day;            //     local day she hatched (or was adopted by a trusted clock), 0 = unknown
    u16 seen_day;             //     local day of the last care, for a warm reunion after a long gap
    u8 party_year;            //     years since 2000 of the last birthday party
    u8 milestones;            //     care-day milestones already celebrated (bit per MILESTONE_DAYS)
    u8 life_flags, pad;       //     LIFE_ADOPTED: hatch_day is the first trusted day, not the real one
    u32 check;                // FNV-1a over everything above
};
enum { SAVE_VERSION = 6, SAVE_SIZE = sizeof(Save), SAVE_V1_SIZE = 24, SAVE_V2_SIZE = 28, SAVE_V3_SIZE = 32,
       SAVE_V4_SIZE = 40, SAVE_V5_SIZE = 40, BIRTH_CHOSEN = 1 };

// Local calendar from the wall clock (local seconds since 2000-01-01 00:00, 0 = unknown).
struct Cal { u16 day; u16 minute; u16 year; u8 month, mday, wday, known; };   // wday: 0 = Monday
Cal calendar(u32 local_s);

struct Part { i16 x, y; i8 vx, vy; u8 kind, life, col, pad; };
struct Target { i16 x, y; i8 vy; u8 alive; };

struct Game {
    Save s;
    u8 scene, menu, screen_on, frame, taps, dirty, want_save, sfx;
    u32 scene_ms, idle_ms, anim_ms, minute_ms, sec_ms, save_ms, rng;

    // cosmetic, transient
    u32 clock_ms, acc_ms, crng;           // acc_ms: time not yet simulated (< STEP_MS)
    i16 pet_x, walk_to;
    i8 vel_x;                             // px per step right now, for render interpolation
    i8 facing;                        // 1 = looking right, -1 = mirrored
    u8 idle_act, treat, score, sel, wash_messes, celebrated;
    u32 idle_next, idle_end, pet_react_end, pet_gain_at, wobble_end;   // deadlines, see due()
    u8 petting, pet_cooling, wobbling;
    u8 hint_pet, napped, press_item, set_row, battery;   // battery: % from the PMU, 0 = unknown
    u8 plugged, plug_raw, plug_known;     // USB power (debounced: two equal samples), raw sample
    u32 unplug_at;                        // when the cable last came out (0 = never)
    u32 press_end, sleep_ms;
    // bath foam, butterfly, music garden, tilt-a-cloud, dress-up (all transient)
    u8 foam_mask, foam_sel, visitor, note_count, echo_i, landed, preview;
    u16 foam_at[3], flower_at[3];          // scene_ms of the last pop / note per target
    u8 notes[NOTES];
    u32 visit_due, visit_start, last_note_ms, echo_next, tilt_at, dress_at, land_cue_at;
    i16 cloud_x, tilt, tilt_f;
    struct Feather { i16 x, y; u8 state; } fea[FEATHERS];   // state: 0 waiting, 1 falling, 2 on cloud, 3 floor
    u32 play_ms, dark_ms, away_ms;        // screen time used, dark streak, time left out
    u32 wall_s;                           // local wall clock from the board's RTC, 0 = unknown
    u8 cal_ok, greeting, tucked;          // cal_ok: the RTC's date can be believed
    u8 did, dreams[DREAMS];               // what she did since waking, tonight's dreams
    u16 tuck_ms;                          // scene_ms when the blanket went over her
    u8 memory_new, album_unseen;          // a picture is waiting to float into the album
    u16 book;                             // the album book being looked at
    u8 party, party_arg;                  // a celebration on the home screen right now (Party)
    u8 idle_count, trick_next;            // every second idle moment may be a trick, in turn
    u32 trick_at;                         // when the last trick started (0 = none yet)
    u8 set_page, want_clock;              // settings page (0 main, 1 clock); F_CLOCK pending
    u16 ed_year;                          // the clock page's fields while a parent edits them
    u8 ed_month, ed_day, ed_hour, ed_min, ed_bmonth, ed_bday;
    u32 party_end;
    u32 memory_at;
    u32 greet_end;
    Part part[NPART];
    Target tgt[GAME_TARGETS];

    void init(u32 seed);                         // fresh egg
    bool load(const u8* data, u32 len);          // false -> corrupt/other version, stays fresh
    u32 save(u8* out) const;                     // returns SAVE_SIZE
    u32 tick(u32 dt_ms);                         // advance time, returns Flags
    void input(int type, int x, int y);
    int take_sfx();                              // pending cue (once), SFX_NONE if muted
    // Local wall-clock seconds since 2000-01-01 from an RTC that keeps running while the board is
    // off (0 = none). Breaks use it; needs still freeze while powered off. calendar = false: the
    // clock ticks but its date is a guess (flat battery), so day/night falls back to daytime.
    void set_wall(u32 local_s, bool calendar = true);
    void set_battery(int percent) { battery = (u8)(percent < 0 ? 0 : percent > 100 ? 100 : percent); }
    void set_tilt(int v);                        // -100..100 from the IMU (or a slider); cloud game only
    void set_plugged(bool usb);                  // USB power present, sampled about once a second

    // effective parent settings
    u32 play_limit_ms() const { return (s.limit_min ? s.limit_min : PLAY_LIMIT_MS / 60000) * 60000u; }
    u32 break_ms() const { return (s.break_min ? s.break_min : AWAY_MS / 60000) * 60000u; }
    int volume() const { return s.muted ? (int)VOL_OFF : s.volume; }
    int brightness() const { return s.bright ? s.bright : 180; }   // AMOLED level 1..255
    bool calm() const { return s.opts & OPT_CALM; }
    int recommended() const;                     // menu item her needs point at, -1 = none
    bool foam_window() const;                    // bath: the foam puffs are out
    Cal today() const { return calendar(cal_ok ? wall_s : 0); }
    int phase() const;                           // Phase: day, evening or night
    bool tucked_in() const { return tucked || scene_ms >= TUCK_MS; }
    int dream() const;                           // 0, or 1 + the Dream she is having right now
    int dream_ms() const;                        // ms into the current dream
    int books() const { return s.care_days ? (s.care_days + ALBUM_PICS - 1) / ALBUM_PICS : 1; }
    int album() const;                           // pictures in the open book (12 per book)
    int garden(int pot) const;                   // the open book's garden: 0 seed .. 4 bloom, per pot
    int stage() const;                           // Stage, from the care days
    int years() const;                           // birthdays she has had (0 = unknown or none yet)
    int tricks() const;                          // tricks learned so far: 0..3 (hum, twirl, dance)

    Mood mood() const;
    int messes() const;                          // rainbow messes on the floor, 0..3
    int particles() const;                       // live particles
    int stat(int i) const { return s.stat[i]; }
    bool due(u32 t) const { return (int)(clock_ms - t) >= 0; }   // wrap-safe (49.7-day clock)
    void set_stat(int i, int v);                 // debug / tests

private:
    void go(u8 sc);
    void act(int item);
    void minute();
    void hatch();
    void wake();
    void come_back();
    void step();
    void advance(u32 ms);
    void scene_timers();
    bool visible(int i) const;
    int highest_visible() const;
    void pet();
    void pop(int i);
    void spawn_target(int i, int y);
    bool counts_as_play() const;
    void start_leave();
    void eat(int t);
    void open_toy(int toy);
    void start_stars();
    void choose_egg();
    void pop_foam(int i);
    void mode_input(int type, int x, int y);
    void play_note(int flower);
    void press(int item);
    void settings_input(int type, int x, int y);
    void clock_input(int type, int x, int y);
    void open_clock();
    void save_clock();
    void sync_away();
    void greet();
    void remember();
    void plan_dreams();
    void check_life();
    void burst(int kind, int x, int y, int n);
    void cue(int id) { sfx = (u8)id; }
    u32 random();
    u32 crandom();
};

void render(const Game& g, u16* fb);             // fb = SCREEN_W * SCREEN_H RGB565

// Hit boxes shared by input handling and rendering.
enum { MENU_Y0 = 368, MENU_Y1 = 434, MENU_X0 = 10, MENU_STEP = 92, MENU_BTN_W = 72 };
enum { PET_X0 = 64, PET_Y0 = 100, PET_X1 = 304, PET_Y1 = 350 };   // shifted by Game::pet_x
enum { HEADER_Y1 = 92 };
enum { SET_Y0 = 64, SET_ROW_H = 38 };             // settings rows (both pages)                          // status bar + name: opens the info screen
enum { SOUND_X0 = 236, SOUND_Y1 = 48 };          // info screen: sound on/off toggle, top right
enum { BOOK_X1 = 132, BOOK_Y1 = 56 };            // info screen: the album, top left
enum { TARGET_HIT = 36 };                         // mini-game star hit box half-size (72 px)

}  // namespace up
