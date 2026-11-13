// Unicorn Pet game core. Freestanding C++: no libc, no heap, no STL, so the same
// code runs on the ESP32-S3 firmware, in the browser (wasm32) and in host tests.
#pragma once

namespace up {

#include "voice.gen.h"            // enum Voice: her spoken lines (tools/voice.py)

typedef unsigned char u8;
typedef signed char i8;
typedef unsigned short u16;
typedef short i16;
typedef unsigned int u32;

#include "routine.gen.h"          // her daily-routine questions (tools/routines.py via tools/voice.py)

enum { SCREEN_W = 368, SCREEN_H = 448 };

inline int iabs(int v) { return v < 0 ? -v : v; }

// Platform -> core
enum Input { IN_NEXT = 1, IN_SELECT = 2, IN_TOUCH = 3, IN_SHAKE = 4,
             IN_PARENT = 5 };   // the grown-up gesture on BOOT (see set_boot): door: recall; else: settings
// Core -> platform, returned by Game::tick
enum Flags { F_REDRAW = 1, F_SCREEN_ON = 2, F_SAVE = 4, F_SOUND = 8,   // F_SOUND: call take_sfx()
             F_CLOCK = 16,     // a parent set the clock: write Game::wall_s (local) to the RTC
             F_WIFI = 32,      // a WiFi setup request: read Game::wifi_req (and wifi_arg)
             F_UPDATE = 64,    // a firmware update request: read Game::update_req
             F_LISTEN = 128 }; // listen to the child (or stop): read Game::listen_req (see listen_result)

// Sound cues. The melodies live in sound.cpp so the board and the emulator play the same notes.
enum Sfx { SFX_NONE, SFX_TAP, SFX_HATCH, SFX_EAT, SFX_PLAY, SFX_WASH, SFX_SLEEP, SFX_WAKE, SFX_REFUSE,
           SFX_INFO, SFX_PET, SFX_POP, SFX_CHEER, SFX_BYE, SFX_KNOCK, SFX_HELLO,
           SFX_NOTE1, SFX_NOTE2, SFX_NOTE3, SFX_CHOOSE, SFX_LAND, SFX_DRESS, SFX_ECHO, SFX_CHARGE, SFX_COUNT };
struct Note { u16 hz, ms; };             // hz = 0 is a rest
const Note* melody(int sfx, int* count);
void set_echo(const u8* flowers, int n);   // SFX_ECHO plays these flowers' notes back as one tune
// Her voice: packs from tools/voice.py ("UPO1": a table of Ogg Opus clips, 16 kHz mono, 16 kbps).
// fw/data/voice.bin (public lines, clip = line - 1 per language), fw/personal/voice_name.bin (NameLine) and
// routines_tr.bin (routine.gen.h), both Turkish only. pack_clip() checks the whole clip first (a damaged or
// cut-short pack plays nothing); ogg_packet() then hands out its Opus packets for the platform's decoder.
enum { VOICE_RATE = 16000 };
struct Clip { const u8* data; u32 len, page, body, samples; u16 pre_skip; u8 seg; };   // samples: at 16 kHz
bool pack_clip(const u8* bin, u32 len, int lang, int index, Clip* c);
int ogg_packet(Clip* c, const u8** packet);    // length of the next Opus packet, 0 at the end
int name_line(int sfx, int line);              // the name pack's version of a line (NameLine), -1: none

enum Stat { HUNGER, HAPPY, ENERGY, CLEAN, NSTATS };
enum { MAX_STAT = 10000 };

// New scenes are appended so existing scene numbers (emulator) stay put.
// LEAVE/AWAY/RETURN: after PLAY_LIMIT_MS of screen time she has "things to do" and goes out,
// leaving a do-not-disturb sign on the door, so the screen stays dark for a while.
enum Scene { SC_EGG, SC_HATCH, SC_HOME, SC_EAT, SC_PLAY, SC_WASH, SC_SLEEP, SC_INFO, SC_REFUSE, SC_GAME,
             SC_LEAVE, SC_AWAY, SC_RETURN, SC_FEED, SC_SETTINGS,     // FEED: pick a snack
             SC_EGG_PICK, SC_TOYS, SC_MUSIC, SC_CLOUD, SC_DRESS,     // choose an egg; Oyna's toy box
             SC_ALBUM,                                               // memories and her garden
             SC_LOUNGE,                                              // plugged in: on the sun lounger
             SC_ASK };                                               // a routine question: a picture, then listening
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
enum { LIFE_ADOPTED = 1, LIFE_UTC_AWAY = 2, MILESTONES = 4 };      // from the wall clock; unknown clock = day
enum Mood { M_IDLE, M_HAPPY, M_HUNGRY, M_SLEEPY, M_SAD };
enum Menu { MENU_FEED, MENU_PLAY, MENU_SLEEP, MENU_WASH, MENU_COUNT };
enum Treat { TREAT_APPLE, TREAT_STRAWBERRY, TREAT_CARROT, TREAT_COUNT };
enum Idle { IDLE_NONE, IDLE_BLINK, IDLE_WALK, IDLE_HOP,
            // tricks, in the order she learns them (TRICK_DAYS)
            IDLE_HUM, IDLE_BOW, IDLE_TWIRL, IDLE_DANCE, IDLE_RAINBOW, IDLE_BUBBLES, IDLE_JUGGLE, IDLE_BALANCE,
            IDLE_MAGIC, IDLE_SHOW, IDLE_END };
enum { TRICKS = IDLE_END - IDLE_HUM };
// care days to learn each trick, how long it lasts, and whether calm mode allows it (no hops/spins)
extern const u16 TRICK_DAYS[TRICKS], TRICK_MS[TRICKS];
extern const u8 TRICK_CALM[TRICKS];
enum Particle { P_NONE, P_HEART, P_SPARKLE, P_BUBBLE, P_CRUMB, P_STAR, P_CONFETTI, P_RING, P_NOTE };
// Parent settings (stored in the save; 0 always means "default")
enum Volume { VOL_NORMAL, VOL_LOW, VOL_OFF };
enum Opt { OPT_CALM = 1, OPT_NO_SHAKE = 2,        // calm: no bobbing/hops, fewer particles
           OPT_NO_BEDTIME = 4,                    // the parent switched the bedtime off
           OPT_NO_ROUTINES = 8, OPT_HOLIDAY = 16,  // the daily questions off; holiday mode (no school)
           OPT_TV_TEST = 32 };                    // TV test mode: 5 s samples for the listener, nothing shown
// Bedtime: an hour of drowsy swaying, then she falls asleep (standing, if still up); asleep in the
// night window she cannot be woken until the wake-up time. Needs a trusted calendar.
enum Bedtime { BED_NONE, BED_DROWSY, BED_SLEEP };
enum { BED_DEFAULT10 = 117, WAKE_DEFAULT10 = 42, WAKE_WE_DEFAULT10 = 48,   // 19:30, 07:00, 08:00 at weekends
       DROWSY_MIN = 60 };                                                    // (tens of minutes)
enum ClockSet { T_BED, T_WAKE, T_WAKE_WE };   // the parent's times (Game::clock_min), minute precision
// the everyday rows first: the list shows in two halves (see set_half), ÇIKIŞ on both
enum SettingRow { SET_VOLUME, SET_LIMIT, SET_BREAK, SET_LANG, SET_CLOCK, SET_SLEEP, SET_BRIGHT, SET_CALM, SET_SHAKE,
                  SET_ROUTINE, SET_EXIT, SET_ROWS };
// Screen languages, in tools/i18n.py's order (Latin and Cyrillic: what the pixel font can draw).
enum Lang { LANG_TR, LANG_EN, LANG_ES, LANG_FR, LANG_DE, LANG_PT, LANG_IT, LANG_RU, LANG_SV, LANG_ID, LANG_COUNT };
// third settings page: bedtime on/off, bedtime, wake-up time on school days and at weekends (left half -1,
// right half +1: 10 minutes)
enum BedRow { BEDR_ON, BEDR_BED, BEDR_WAKE, BEDR_WAKE_WE, BEDR_BACK, BEDR_ROWS };
// the daily routine page: questions on/off, holiday mode, TV test mode, the week's stars
enum RoutineRow { RT_ON, RT_HOLIDAY, RT_TV, RT_HABITS, RT_BACK, RT_ROWS };
// Settings pages, and the WiFi setup flow (the toy shows a QR code for a temporary network of its
// own; the parent picks the home network and types its password on the phone).
enum SetPage { PG_MAIN, PG_CLOCK, PG_BED, PG_HUB, PG_WIFI, PG_WSTART, PG_WSESSION, PG_WHELP, PG_WLIST, PG_WFORGET,
               PG_UPDATE, PG_ROUTINE, PG_HABITS };
enum HubRow { HUB_CLOCK, HUB_WIFI, HUB_UPDATE, HUB_BACK, HUB_ROWS };
// Firmware updates over WiFi (signed releases; the platform checks, downloads and verifies, the core
// draws the page). IDLE/LATEST/AVAILABLE are at rest; CHECKING..RESTARTING under way; then the errors.
enum UpdateStage { UST_IDLE, UST_CHECKING, UST_LATEST, UST_AVAILABLE, UST_DOWNLOADING, UST_VERIFYING, UST_RESTARTING,
                   UST_NO_WIFI, UST_FAILED, UST_BAD_SIG };
enum UpdateReq { UREQ_NONE, UREQ_CHECK, UREQ_INSTALL };
enum { UPDATE_MIN_BATTERY = 50 };          // installing needs USB power or at least this much battery
enum WifiRow { WF_ADD, WF_LIST, WF_BACK, WF_ROWS };
enum WifiStage { WST_OFF, WST_STARTING, WST_WAIT, WST_PHONE, WST_JOIN, WST_ADDR, WST_CLOCK,   // under way
                 WST_OK, WST_OK_NOCLOCK, WST_BAD_PASS, WST_NOT_FOUND, WST_FULL, WST_TIMEOUT };   // results
enum WifiReq { WREQ_NONE, WREQ_START, WREQ_STOP, WREQ_RETRY, WREQ_FORGET };
enum { WIFI_LIST_MAX = 8, WIFI_SAVED_MAX = 5, WIFI_NAME = 33 };
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
    // BOOT: a short press is IN_NEXT, sent once the press is over (TAP_GAP_MS later, so it can still
    // turn out to be the grown-up gesture). The gesture: PARENT_TAPS quick presses, the last one held
    // PARENT_HOLD_MS. Hard for a child to hit by chance, and it never moves the menu cursor.
    TAP_GAP_MS = 400, PARENT_TAPS = 3, PARENT_HOLD_MS = 3000, KEY_BOUNCE_MS = 30,
    NAP_MS = 60000, NAP_BONUS = 2500,    // a one-minute nap is enough to play again
    FEED_PICK_MS = 8000,                 // snack choice waits this long, then takes the highlighted one
    PRESS_MS = 260, SQUASH_MS = 140,     // button press pop / landing squash
    FRIEND_MS = 1400,                    // a friend comes to pick her up
    MODE_IDLE_MS = 12000,                // toy modes go home after this long without a deliberate input
    EGG_HINT_MS = 4000,                  // egg picker: a hand shows what to do
    WASH_MS = 6500,                      // the bath: long enough for a five-year-old to pop all three puffs
    FOAM_FROM_MS = 400, FOAM_TO_MS = 5400, CROWN_TO_MS = 6000,   // bath: three foam puffs to pop (5 s)
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
    DARK_FLOOR = 1500,                   // with the screen dark, needs never drop below this
    LOUNGE_IDLE_MS = 10000,              // plugged in at home: back to the lounger after this long
    PLUG_QUIET_MS = 5000,                // re-plugged this soon (a loose cable): no fanfare
    TRICK_GAP_MS = 12000,                // at most one trick this often
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
    u32 away_until;           // v3: UTC second she comes back (0 = no clock, count down)
    u8 volume, limit_min, break_min, bright, opts;           // v4: parent settings, 0 = default
    u8 birth_flags, accessory, last_treat;                    // v5: egg chosen (bit 0), dress-up,
                                                              //     last snack + 1 (0 = none yet)
    u16 memory_day;           // v6: local day of the last album memory (0 = none yet)
    u16 care_days;            //     days she was cared for: album, garden, growing up
    u16 hatch_day;            //     local day she hatched (or was adopted by a trusted clock), 0 = unknown
    u16 seen_day;             //     local day of the last care, for a warm reunion after a long gap
    u8 party_year;            //     years since 2000 of the last birthday party
    u8 milestones;            //     care-day milestones already celebrated (bit per MILESTONE_DAYS)
    u8 life_flags;            //     LIFE_ADOPTED: hatch_day is the first trusted day, not the real one
                              //     LIFE_UTC_AWAY: away_until is UTC (older saves stored local time)
    u8 bed10;                 // v7 (was v6's pad): bedtime in tens of minutes after midnight (117 = 19:30)
    u8 wake10;                //     wake-up time, the same way (42 = 07:00)
    u8 tricks_seen;           //     tricks she has shown off already (a newly learned one comes first)
    u8 lang;                  //     screen language (Lang, 0 = Turkish: older saves stay Turkish)
    u8 wake_we10;             // v8 (was v7's spare): wake-up time at weekends and on holidays
    u16 r_day;                //     local day the routine fields below are about (0 = none yet)
    u16 r_asked, r_done;      //     questions asked today (bit per Question), and finished (no re-ask)
    u16 q_off;                //     questions the parent switched off (bit per Question)
    u16 week[7];              //     yes answers (stars) per day: week[day % 7], cleared as days go by
    u16 cfg_dirty;            //     settings changed on the toy since the last sync (bit per SetKey)
    u32 cfg_rev;              //     the parent page's config revision the settings are based on
    u16 hol[4][2];            //     holiday ranges from the parent page: local days, first..last
    u16 r_ans[Q_COUNT];       //     today's answers: Answer << 11 | minute of the day (0 = none)
    u16 tv_day;               //     local day TV test mode was switched on (it ends after TV_DAYS)
    u16 min_ones;             //     the minutes past bed10, wake10, wake_we10 (4 bits each, 0..9): a page's 19:35
    u32 check;                // FNV-1a over everything above
};
enum { SAVE_VERSION = 8, SAVE_SIZE = sizeof(Save), SAVE_V1_SIZE = 24, SAVE_V2_SIZE = 28, SAVE_V3_SIZE = 32,
       SAVE_V4_SIZE = 40, SAVE_V5_SIZE = 40, SAVE_V6_SIZE = 52, SAVE_V7_SIZE = 56, BIRTH_CHOSEN = 1, HOL_MAX = 4 };
static_assert(sizeof(Save) == 128, "the save layout is stored: change it only with a new version");

// ---- her daily routine ---------------------------------------------------------------------------
// The core asks (F_LISTEN, listen_req) and the platform listens, then reports with listen_result(). Answers
// as stored in Save::r_ans (3 bits); ANS_NO_LISTENER means "nobody to ask": the big tick and cross buttons.
enum ListenReq { LREQ_NONE, LREQ_LISTEN, LREQ_CANCEL };
enum ListenKind { LK_YESNO, LK_OPEN,       // after her question: until 0.8 s of silence, at most 6 s
                  LK_LEVEL,                // TV test: about 1 s of ambient level, measured on the toy only
                  LK_TV };                 // TV test: a 5 s sample for the listener (/v1/tv-sample)
enum Answer { ANS_NONE, ANS_YES, ANS_NO, ANS_UNCLEAR, ANS_SILENT, ANS_SAID, ANS_TAP_YES, ANS_TAP_NO,
              ANS_NO_LISTENER };
enum RStage { RS_ASK, RS_LISTEN, RS_BUTTONS, RS_REPLY };
enum {
    Q_GAP_MIN = 8, Q_REASK_MIN = 10,       // questions at least 8 minutes apart; one more try 10 min later
    Q_AT_WINDOW_MIN = 30,                  // a question at a set time (dinner, water) may come this much later
    R_LISTEN_MS = 30000, R_BUTTONS_MS = 20000, R_REPLY_MS = 5000,   // so a question never wedges
    R_DARK_LINGER_MS = 5000,               // woke the screen for it: dark again this soon after
    R_CLIP_MAX_MS = 15000,                 // a routine clip is shorter than this (still playing, maybe)
    // TV test mode: once a minute ~1 s of ambient level; two loud checks in a row (sustained, not a slam)
    // make a 5 s sample. At most TV_PER_HOUR an hour, none for TV_SAME_MIN while the same sound goes on.
    TV_CHECK_MS = 60000, TV_LEVEL_DB = -42, TV_SUSTAIN_PCT = 60, TV_PER_HOUR = 6, TV_SAME_MIN = 10,
    TV_CHANGE_DB = 8, TV_DAYS = 7,
};
// Radio policy the platform follows (Game::link): off by default; up for a question, a TV sample or a
// pending upload; kept up while on USB power.
enum Link { LINK_OFF, LINK_NOW, LINK_STAY };

// Settings the parent page can change too (server/API.md): one plain array the platform serialises.
// v[]: routines 0/1, questions (bit per Question: 1 = asked), wakes and bedtime in minutes of the day,
// holiday 0/1, holiday range count (ranges in hol[]), tv test 0/1, lang (Lang), volume (Volume), brightness
// 1..255, play and break minutes, calm 0/1, shake 0/1.
enum SetKey { K_ROUTINES, K_QUESTIONS, K_WAKE_WEEKDAY, K_WAKE_WEEKEND, K_BEDTIME, K_HOLIDAY, K_HOLIDAY_RANGES,
              K_TV_TEST, K_LANG, K_VOLUME, K_BRIGHTNESS, K_PLAY_MIN, K_BREAK_MIN, K_CALM, K_SHAKE, K_COUNT };
// tv_day: the local day TV test mode was switched on (the page's tv_test_since, 0 = today), taken with K_TV_TEST.
struct Settings { u16 v[K_COUNT]; u16 hol[HOL_MAX][2]; u16 tv_day; };
// What a sync carried: the keys changed on the toy (dirty) and each key's edit count then, so its acknowledgement
// clears only those edits (one made while the request was out stays dirty for the next sync).
struct SyncMark { u16 dirty; u8 gen[K_COUNT]; };
// The page's config over mine: the keys it sends (present, bit per SetKey) win, except those changed on the
// toy since the last sync (dirty), which keep the toy's value. Values out of range keep mine too.
Settings merge_settings(const Settings& mine, u16 dirty, const Settings& page, u16 present);

// Local calendar from the wall clock (local seconds since 2000-01-01 00:00, 0 = unknown).
struct Cal { u16 day; u16 minute; u16 year; u8 month, mday, wday, known; };   // wday: 0 = Monday
Cal calendar(u32 local_s);

enum { QR_MAX_SIDE_ = 37 };                       // = QR_MAX_SIDE below (version 5)
struct Part { i16 x, y; i8 vx, vy; u8 kind, life, col, pad; };
struct Target { i16 x, y; i8 vy; u8 alive; };

struct Game {
    Save s;
    u8 scene, menu, screen_on, frame, taps, dirty, want_save, sfx, voice;
    u32 scene_ms, idle_ms, anim_ms, minute_ms, sec_ms, save_ms, rng;

    // cosmetic, transient
    u32 clock_ms, acc_ms, crng;           // acc_ms: time not yet simulated (< STEP_MS)
    i16 pet_x, walk_to;
    i8 vel_x;                             // px per step right now, for render interpolation
    i8 facing;                        // 1 = looking right, -1 = mirrored
    u8 idle_act, treat, score, sel, wash_messes, celebrated;
    u32 idle_next, idle_end, pet_react_end, pet_gain_at, wobble_end;   // deadlines, see due()
    u8 petting, pet_cooling, wobbling;
    u8 hint_pet, napped, press_item, set_row, battery, batt_known;   // battery: % from the PMU
    u8 plugged, plug_raw, plug_known;     // USB power (debounced: two equal samples), raw sample
    u32 unplug_at;                        // when the cable last came out (0 = never)
    u32 press_end, sleep_ms;
    // bath foam, butterfly, music garden, tilt-a-cloud, dress-up (all transient)
    u8 foam_mask, foam_sel, visitor, note_count, echo_i, landed, preview;
    u16 foam_at[3], flower_at[3];          // scene_ms of the last pop / note per target
    u8 notes[NOTES];
    u32 visit_due, visit_start, last_note_ms, echo_next, tilt_at, dress_at, land_cue_at, voice_at, voice_line_at[V_COUNT];
    i16 cloud_x, tilt, tilt_f;
    struct Feather { i16 x, y; u8 state; } fea[FEATHERS];   // state: 0 waiting, 1 falling, 2 on cloud, 3 floor
    u32 play_ms, dark_ms, away_ms;        // screen time used, dark streak, time left out
    u32 wall_s;                           // local wall clock from the board's RTC, 0 = unknown
    u32 utc_s;                            // the same moment in UTC: what break deadlines count in
    u8 cal_ok, greeting, tucked;          // cal_ok: the RTC's date can be believed
    u8 did, dreams[DREAMS];               // what she did since waking, tonight's dreams
    u16 tuck_ms;                          // scene_ms when the blanket went over her
    u8 memory_new, album_unseen;          // a picture is waiting to float into the album
    u16 book;                             // the album book being looked at
    u8 party, party_arg;                  // a celebration on the home screen right now (Party)
    u8 idle_count, trick_next;            // every second idle moment may be a trick, in turn
    u32 trick_at;                         // when the last trick started (0 = none yet)
    u32 pet_saved_at;                     // petting gains are saved at most once a minute
    // WiFi setup: the platform runs the radio and reports back; the core draws every screen
    u8 wifi_stage, wifi_req, wifi_arg, wifi_count, wifi_sel, qr_side;
    u16 wifi_fixed;                       // bit i: list entry i is built in (secrets.h), read-only
    char wifi_ap[WIFI_NAME], wifi_pass[24], wifi_net[WIFI_NAME];   // the toy's network, the home one
    char wifi_names[WIFI_LIST_MAX][WIFI_NAME];
    // firmware update: the platform sets fw_version (the running one) and reports through update_status
    u8 update_stage, update_req, update_pct;
    u16 fw_version, update_ver;           // update_ver: the newest signed version seen (0 = none yet)
    u8 qr[QR_MAX_SIDE_ * QR_MAX_SIDE_];
    u8 boot_down, boot_taps, boot_fired;  // the BOOT key's state machine (set_boot)
    u32 boot_t;                           // time of its last edge
    u8 set_page, want_clock;              // settings page (0 main, 1 clock, 2 bedtime); F_CLOCK pending
    u8 standing, night_sleep, bed_seen;   // asleep on her feet; asleep in the night window; checked once
    u8 back_scene, back_standing, back_night;   // where the settings were opened from (and how she slept)
    u32 back_ms;                          // her sleep's scene time, so it carries on afterwards
    u16 ed_year;                          // the clock page's fields while a parent edits them
    u8 ed_month, ed_day, ed_hour, ed_min, ed_bmonth, ed_bday;
    u32 party_end;
    u32 memory_at;
    u32 greet_end;
    // daily routine (RAM; what must survive a power cut is in Save)
    u8 has_routines, has_names;           // the platform found routines_tr.bin / voice_name.bin
    u8 r_q, r_stage, r_back, r_was_dark;  // the question on screen, its stage, where she came from
    u8 listen_req, listen_kind, listen_q, r_sel;   // F_LISTEN: what the platform should do; button cursor
    u16 rclip;                            // a routine clip to say (index + 1), 0 = none
    u32 r_said;                           // when the last routine clip was handed out (it may still play)
    u32 r_at;                             // when the stage started (stamp)
    u32 r_first[Q_COUNT];                 // minute (day * 1440 + minute) of the first ask, for the re-ask
    u32 r_last;                           // minute of the last question, for the spacing
    u16 bag[3 * Q_COUNT + 4];             // shuffle bags of variants: used bits per list
    u8 bag_last[3 * Q_COUNT + 4];         // and the variant said last + 1 (not first in a new round)
    // TV test mode (RAM)
    u32 tv_check_at, tv_samples[TV_PER_HOUR];   // last level check; when the last samples were sent
    i8 tv_db, tv_sample_db;               // last level; the level when the last sample was taken
    u8 tv_loud, tv_quiet_since;           // loud checks in a row; a quiet check since the last sample
    u8 cfg_gen[K_COUNT];                  // edits per settings key (RAM, wraps): see SyncMark
    Part part[NPART];
    Target tgt[GAME_TARGETS];

    void init(u32 seed);                         // fresh egg
    bool load(const u8* data, u32 len);          // false -> corrupt/other version, stays fresh
    u32 save(u8* out) const;                     // returns SAVE_SIZE
    u32 tick(u32 dt_ms);                         // advance time, returns Flags
    void input(int type, int x, int y);
    int take_sfx(int* line = 0, int* clip = 0);  // pending cue (once), SFX_NONE if muted or at bedtime;
                                                 // *line: what she says (V_NONE = nothing), played instead
                                                 // of the cue's tune, in the language s.lang; *clip: a
                                                 // routines_tr.bin clip to say instead (-1 = none)
    // Local wall-clock seconds since 2000-01-01 from an RTC that keeps running while the board is
    // off (0 = none). Breaks use it; needs still freeze while powered off. calendar = false: the
    // clock ticks but its date is a guess (flat battery), so day/night falls back to daytime.
    // utc_s (seconds since 2000, 0 = same as local_s) times the breaks: a summer-time jump must not
    // end a 30-minute break after ten.
    void set_wall(u32 local_s, bool calendar = true, u32 utc_s = 0);
    void set_battery(int percent) {        // < 0: unknown (no battery, or the PMU could not tell)
        batt_known = percent >= 0;
        battery = (u8)(percent < 0 ? 0 : percent > 100 ? 100 : percent);
    }
    void set_tilt(int v);                        // -100..100 from the IMU (or a slider); cloud game only
    void set_plugged(bool usb);                  // USB power present, sampled about once a second
    void set_boot(bool down);                    // the BOOT key, raw: call every loop (taps, gesture)
    // WiFi setup, platform -> core: the toy's own network is up; progress and results; saved networks
    void wifi_session(const char* ap, const char* pass);
    void wifi_status(int stage, const char* network);
    void wifi_list_clear() { wifi_count = 0; wifi_fixed = 0; dirty = 1; }
    void wifi_list_add(const char* name, bool built_in);
    bool wifi_busy() const { return wifi_stage != WST_OFF; }   // a session is open (radio in use)
    // firmware update, platform -> core: stage (UpdateStage), percent while downloading, and the newest
    // version it knows (0 = keep the one known)
    void update_status(int stage, int percent, int version);
    bool update_waiting() const { return update_ver > fw_version; }   // a newer version is out there
    bool update_allowed() const { return plugged || (batt_known && battery >= UPDATE_MIN_BATTERY); }
    bool update_busy() const { return update_stage == UST_CHECKING || (update_stage >= UST_DOWNLOADING &&
                                                                       update_stage <= UST_RESTARTING); }
    int update_view() const;                     // what the update page shows (a known newer version: AVAILABLE)

    // effective parent settings
    u32 play_limit_ms() const { return (s.limit_min ? s.limit_min : PLAY_LIMIT_MS / 60000) * 60000u; }
    u32 break_ms() const { return (s.break_min ? s.break_min : AWAY_MS / 60000) * 60000u; }
    int volume() const { return s.muted ? (int)VOL_OFF : s.volume; }   // load() folds muted into volume
    int brightness() const { return s.bright ? s.bright : 180; }   // the parent's AMOLED level 1..255
    // what the panel shows: at night and at bedtime a fifth of that, a glow, not a lamp in the
    // child's room (the settings stay readable at the parent's level)
    int screen_brightness() const {
        bool night = phase() == PH_NIGHT || bedtime() != BED_NONE;
        return night && scene != SC_SETTINGS ? (brightness() / 5 < 12 ? 12 : brightness() / 5) : brightness();
    }
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
    int tricks() const;                          // tricks learned so far: 0..TRICKS
    bool quiet() const { return volume() == VOL_OFF || bedtime() != BED_NONE; }   // no sound: off, or bedtime
    int bedtime() const;                         // Bedtime: none, drowsy hour, or time to sleep
    bool bed_locked() const { return bedtime() != BED_NONE; }   // asleep now = asleep till morning

    Mood mood() const;
    int messes() const;                          // rainbow messes on the floor, 0..3
    int particles() const;                       // live particles
    int stat(int i) const { return s.stat[i]; }
    bool due(u32 t) const { return (int)(clock_ms - t) >= 0; }   // wrap-safe (49.7-day clock)
    // "when did it happen": stamp() is never 0 (0 means never); since() is the time elapsed, wrap-safe
    u32 stamp() const { return clock_ms + 1; }
    u32 since(u32 st) const { return clock_ms + 1 - st; }
    void set_stat(int i, int v);                 // debug / tests
    int set_rows() const;                        // rows of the settings list on screen (0: a button page)

    // daily routine, platform -> core: listening began; the answer (Answer); a TV-test level check
    void listen_started();
    void listen_result(int answer);
    void level_result(int db, int loud_pct);     // dBFS (-90..0) and the share of loud frames, 0..100
    bool routines_on() const { return has_routines && !(s.opts & OPT_NO_ROUTINES); }
    bool holiday() const;                        // holiday mode, or today is in a page's holiday range
    bool holiday_on(int day) const;              // the same for a local day (-1: unknown)
    bool free_on(int day) const;                 // a weekend day or a holiday
    int wake_on(int day) const;                  // that day's wake-up, minute of the day
    int rday_now(int* min = 0) const;            // her routine day (wake-up to wake-up), -1 = no trusted clock
    bool question_may_continue() const;
    bool free_day() const;                       // a weekend day or a holiday: no school, later wake-up
    int wake_min() const;                        // today's wake-up time (school days / free days), minute of the day
    int clock_min(int t) const;                  // a ClockSet (bedtime, wake-ups) as a minute of the day
    void set_clock_min(int t, int minute);
    int stars_today() const;
    int link(bool sample_waiting = false) const; // Link: what the radio should do now
    bool tv_on() const { return s.opts & OPT_TV_TEST; }
    // settings sync with the parent page (server/API.md): read, apply a newer page config, synced
    Settings settings() const;
    void apply_settings(u32 rev, const Settings& page, u16 present);
    SyncMark sync_mark() const;
    void settings_synced(const SyncMark& sent);

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
    int set_touch(int y);                        // the settings row under a tap, -1: none (or a page turn)
    void clock_input(int type, int x, int y);
    void open_clock();
    void save_clock();
    void sync_away();
    void greet();
    void remember();
    void plan_dreams();
    void check_life();
    void check_bedtime();
    void doze(bool on_her_feet);
    bool rested() const;
    void bed_input(int type, int x, int y);
    void leave_settings();
    void pet_gain();
    void wifi_input(int type, int x, int y);
    void wifi_request(int req, int arg = 0);
    void update_input(int type, int x, int y);
    void burst(int kind, int x, int y, int n);
    void cue(int id, int line = -1);            // a sound, and what she says with it (-1: the usual line)
    void say_clip(int first, int list);          // a routine clip: one of the 10 from `first`, bag `list`
    void check_routine();
    void routine_day(int day);
    void routine_check();
    int question_due(u32 abs_min, int day, int min);
    void ask(int q);
    void routine_done();
    void routine_input(int type, int x, int y);
    void routine_page_input(int type, int x, int y);
    void check_tv();
    bool tv_may_listen() const;
    void routine_cancel();
    void touched(int key) { s.cfg_dirty |= (u16)(1 << key); cfg_gen[key]++; }
    u32 random();
    u32 crandom();
};

void render(const Game& g, u16* fb);             // fb = SCREEN_W * SCREEN_H RGB565
int text_overflows();                            // text drawn off the screen since the last call (tests)

// QR code (version <= QR_MAX_VERSION, medium error correction) as side*side bytes of 0/1.
// Returns the side, or 0 if the text does not fit.
enum { QR_MAX_VERSION = 5, QR_MAX_SIDE = 4 * QR_MAX_VERSION + 17 };
static_assert(QR_MAX_SIDE == QR_MAX_SIDE_, "QR buffer size");
int qr_encode(const char* text, u8* modules, int max_side);

// Hit boxes shared by input handling and rendering.
// menu: buttons centred at MENU_STEP / 2 + i * MENU_STEP; a touch anywhere from MENU_Y0 down takes the nearest
enum { MENU_Y0 = 368, MENU_Y1 = 434, MENU_STEP = 92 };
enum { PET_X0 = 64, PET_Y0 = 100, PET_X1 = 304, PET_Y1 = 350 };   // shifted by Game::pet_x
enum { HEADER_Y1 = 92 };                          // status bar + name: opens the info screen
// Settings rows (all list pages): 48 px = 3.75 mm, at most SET_SLOTS on screen. A longer list shows in two
// halves: rows [0, SET_SPLIT) and a "more" slot, then an "up" slot and the rest; its last row (exit or back)
// sits in the bottom slot of both halves.
enum { SET_Y0 = 56, SET_ROW_H = 48, SET_SLOTS = 7, SET_SPLIT = 5, SLOT_MORE = -2, SLOT_UP = -3 };
inline int set_half(int n, int row) { return n > SET_SLOTS && row >= SET_SPLIT && row < n - 1; }
inline int set_row_slot(int n, int row) {        // where a row is drawn (on its own half)
    return n <= SET_SLOTS || row < SET_SPLIT ? row : row == n - 1 ? SET_SLOTS - 1 : row - SET_SPLIT + 1;
}
inline int set_slot_row(int n, int half, int slot) {   // the row in a slot: a row, SLOT_MORE, SLOT_UP or -1
    if (n <= SET_SLOTS) return slot < n ? slot : -1;
    if (slot == SET_SLOTS - 1) return n - 1;
    if (!half) return slot < SET_SPLIT ? slot : SLOT_MORE;
    if (!slot) return SLOT_UP;
    return SET_SPLIT + slot - 1 < n - 1 ? SET_SPLIT + slot - 1 : -1;
}
static_assert(CLK_ROWS - 1 - SET_SPLIT <= SET_SLOTS - 2 && SET_ROWS - 1 - SET_SPLIT <= SET_SLOTS - 2 &&
              WIFI_LIST_MAX - SET_SPLIT <= SET_SLOTS - 2, "a settings list does not fit in two halves");
enum { WBTN_Y0 = 384, WBTN_Y1 = 440 };
enum { ASK_BTN_Y0 = 296 };                        // a routine question: the big tick (left) and cross (right) below            // WiFi pages: two big buttons along the bottom
enum { HOUSE_X0 = 272, HOUSE_Y1 = 88 };          // toy modes and the album: the whole top-right corner goes home
enum { BOOK_X1 = 132, BOOK_Y1 = 56 };            // info screen: the album, top left
enum { TARGET_HIT = 52 };                         // mini-game star hit box half-size (104 px = 8 mm)

}  // namespace up
