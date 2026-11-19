// Monkey test: a five-year-old plays the real core for days, across many seeds, through the same
// public API the firmware and the emulator use (input, tick, set_wall, set_plugged, set_battery,
// set_boot, set_tilt, take_sfx, save/load, render). Every loop is checked against rules that must
// hold however she plays: no wedged scene, the screen goes dark, the pet is never lost, saves
// round-trip, no settings without the grown-up gesture, no sound at bedtime, the voice rules.
//   make -C test monkey            all seeds (ASan + UBSan)
//   out/monkey <seed> [trace]      one seed again, optionally with every action printed
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/unicorn.h"

using namespace up;
typedef unsigned long long u64;

enum { SEEDS = 300, DAYS = 3, SLACK = 400 };

static const char* SCENES[] = {"EGG", "HATCH", "HOME", "EAT", "PLAY", "WASH", "SLEEP", "INFO", "REFUSE", "GAME",
                               "LEAVE", "AWAY", "RETURN", "FEED", "SETTINGS", "EGG_PICK", "TOYS", "MUSIC", "CLOUD",
                               "DRESS", "ALBUM", "LOUNGE", "ASK", "MARBLE", "BALLOON"};
static_assert(sizeof SCENES / sizeof *SCENES == SC_BALLOON + 1, "a scene name for every scene");

static u16* fb;                            // exactly one frame on the heap: ASan sees any write past it
static Game G, FRESH;
static bool trace;
static u64 visits[SC_BALLOON + 1];       // every scene must be reached, or the monkey has gone blind

struct Rng {
    u32 x;
    u32 next() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
    int in(int lo, int hi) { return lo + (int)(next() % (u32)(hi - lo + 1)); }
    bool pm(int permille) { return (int)(next() % 1000) < permille; }
};

struct Sim {
    u32 seed;
    Rng r, look;                           // play; which frames get rendered (apart, so the play is the same)
    u64 now, real, loops, renders;                  // powered ms; ms including power-off (for the voice); loop count
    const char* action;
    bool failed;
    // the board around the core
    bool rtc, trusted, usb, boot;
    u32 wall, tz, wall_ms;                 // local seconds since 2000, its offset from UTC
    int batt, fw;
    u64 wall_at, batt_at;
    u64 down_at[3], up_at[3];              // the last three BOOT presses (index 2 = newest)
    u8 saved[SAVE_SIZE];
    bool have_save;
    Save last;                             // what the last F_SAVE wrote
    u64 wifi_at, upd_at;                   // platform: when the next WiFi / update report is due
    int wifi_next, upd_next, upd_pct;
    u64 hear_at;                           // platform: when the listener's answer comes (0: none pending)
    int hear_kind;
    u32 page_rev;                          // the parent page's config revision
    // the routine: questions per day, the last one's wall minute
    u32 q_day, q_last_min, questions, answers, samples, levels, break_max;
    u8 q_count[Q_COUNT];
    // trackers for the rules
    int scene;
    u32 voice_dt, line_dt[V_COUNT];
    u64 scene_at, back_at, radio_at, cable_at, input_at, lit_at, leave_at, saved_at, rewound, sleep_free, voice_at, line_at[V_COUNT];
    bool parent, lit, clock_back;
    u32 age_max, hatched, care_max, who;   // who: her character + 1 once the egg is chosen
};
static Sim S;
static struct { u32 questions, answers, levels, samples; } T;   // totals over all seeds

static const char* kinds[32];
static int kind_n[32], nkinds;

static void fail(const char* what) {
    if (S.failed) return;
    S.failed = true;
    int k = 0;
    while (k < nkinds && kinds[k] != what) k++;
    if (k == nkinds && nkinds < 32) kinds[nkinds++] = what;
    if (k < 32 && kind_n[k]++ >= 3) return;   // the first three of each kind are enough to read
    std::printf("FAIL seed %u loop %llu (%s, t=%llus, scene %s, page %d, screen %d): %s\n", S.seed, S.loops,
                S.action, S.now / 1000, G.scene <= SC_BALLOON ? SCENES[G.scene] : "?", G.set_page, G.screen_on, what);
}
#define RULE(cond, what) do { if (!(cond)) fail(what); } while (0)

static bool busy(const Game& g) {          // the documented keep-awake cases (Game::tick)
    int sc = g.scene;
    return sc == SC_EAT || sc == SC_PLAY || sc == SC_WASH || sc == SC_HATCH || sc == SC_GAME || sc == SC_LEAVE ||
           sc == SC_RETURN || sc == SC_ASK ||
           (sc == SC_SETTINGS && g.set_page == PG_WSESSION && g.wifi_stage > WST_OFF && g.wifi_stage < WST_OK) ||
           (sc == SC_SETTINGS && g.set_page == PG_UPDATE && g.update_busy());
}

static bool bedtime_now(const Game& g) {   // the bedtime window, worked out again from the clock
    int m = (int)(g.wall_s % 86400 / 60), bed = g.clock_min(T_BED), up = g.wake_min();   // later on free days
    if (!g.cal_ok || !g.wall_s || (g.s.opts & OPT_NO_BEDTIME) || bed == up) return false;
    return bed < up ? m >= bed && m < up : m >= bed || m < up;
}

// ---- the rules -------------------------------------------------------------------------------

static void check(u32 dt) {
    const Game& g = G;
    RULE(g.scene <= SC_BALLOON, "scene out of range");
    for (int i = 0; i < NSTATS; i++) RULE(g.s.stat[i] <= MAX_STAT, "stat above MAX_STAT");
    RULE(g.scene != SC_ASK || g.r_q < Q_COUNT, "a question that does not exist");
    RULE(g.menu < MENU_COUNT && g.s.character < 4 && g.s.accessory < ACC_COUNT && g.s.lang < LANG_COUNT &&
         g.treat < TREAT_COUNT && g.s.volume <= VOL_OFF && g.set_page <= PG_HABITS && g.book < g.books(),
         "a field out of range");
    for (int i = 0; i < NPART; i++) RULE(g.part[i].kind <= P_NOTE, "bad particle");
    RULE(!g.s.hatched || g.s.birth_flags & BIRTH_CHOSEN, "hatched without a chosen egg");
    if (g.s.birth_flags & BIRTH_CHOSEN) {
        if (!S.who) S.who = g.s.character + 1;
        RULE(g.s.character + 1 == S.who, "she turned into another unicorn");
    }
    // the pet is never lost, and she only grows
    RULE(g.s.hatched >= S.hatched, "hatched went back to 0");
    RULE(g.s.care_days >= S.care_max, "care_days went down");
    RULE(g.s.age_s >= S.age_max, "age went down");
    S.hatched = g.s.hatched; S.care_max = g.s.care_days; S.age_max = g.s.age_s;
    RULE(g.scene != SC_LOUNGE || g.plugged, "on the lounger without the cable");

    if (g.scene != S.scene) {
        if (trace) std::printf("  %llu scene %s -> %s  away_ms %u until %u utc %u wall %u cal %d\n", S.now,
                               S.scene >= 0 ? SCENES[S.scene] : "-", SCENES[g.scene], g.away_ms, g.s.away_until, g.utc_s, g.wall_s, g.cal_ok);
        if (g.scene == SC_LEAVE || (g.scene == SC_AWAY && S.scene != SC_LEAVE && S.scene != SC_ASK)) {   // (back out after
            S.leave_at = S.now; S.rewound = 0; S.break_max = g.break_ms();                             //  a question: the same break)
        }
        if (g.scene <= SC_BALLOON) visits[g.scene]++;
        // per routine day as the toy sees it: a clock set days back and then forward again starts that day afresh
        if (g.s.r_day != S.q_day) { S.q_day = g.s.r_day; std::memset(S.q_count, 0, sizeof S.q_count); S.q_last_min = 0; }
        if (g.scene == SC_ASK) {               // her question: only when she may ask, never too often
            S.questions++;
            RULE(g.routines_on() && g.cal_ok && g.wall_s && !bedtime_now(g) && g.volume() != VOL_OFF,
                 "a question she should not ask now");
            u64 natural = S.scene == SC_EAT || S.scene == SC_FEED ? ACTION_MS : S.scene == SC_WASH ? WASH_MS
                        : S.scene == SC_GAME ? GAME_MS : S.scene == SC_PLAY ? CELEBRATE_MS : S.scene == SC_HATCH ? HATCH_MS : 0;
            bool ended = S.now - S.scene_at + SLACK >= natural;   // it may follow an activity that just ended
            RULE(ended && S.scene != SC_LEAVE && S.scene != SC_EGG && S.scene != SC_EGG_PICK,   // (out with friends she may: she pops in)
                 "a question in the middle of something");
            u32 min = g.wall_s / 60;
            RULE(++S.q_count[g.r_q] <= 2, "a question asked more than twice a day");
            RULE(!S.q_last_min || min < S.q_last_min || S.clock_back || min - S.q_last_min >= Q_GAP_MIN,
                 "two questions within 8 minutes");   // (a clock set back, then a power cut: the toy cannot know)
            S.clock_back = false;
            S.q_last_min = min;
        }
        S.scene = g.scene; S.scene_at = S.now; S.lit_at = S.now; S.sleep_free = 0;
    }
    if (busy(g) || g.scene == SC_HATCH || (g.screen_on && !S.lit)) S.lit_at = S.now;   // (or it lit itself)
    S.lit = g.screen_on;
    if (g.scene == SC_SLEEP && !g.bed_locked()) S.sleep_free += dt;

    // nothing wedges: every scene but home, the egg picker, the lounger and the door ends by itself
    if ((g.wifi_stage > WST_OFF && g.wifi_stage < WST_OK) || g.update_busy()) S.radio_at = S.now;   // the settings wait for the radio
    u64 from = S.input_at > S.scene_at ? S.input_at : S.scene_at;
    if (g.scene == SC_SETTINGS && S.radio_at > from) from = S.radio_at;
    u64 in = S.now - S.scene_at, quiet = S.now - from;
    u64 lim = 0, lim_quiet = 0;
    switch (g.scene) {
    case SC_EGG: lim = EGG_HATCH_MS; break;
    case SC_HATCH: lim = HATCH_MS; break;
    case SC_EAT: lim = ACTION_MS; break;
    case SC_WASH: lim = WASH_MS; break;
    case SC_PLAY: lim = CELEBRATE_MS; break;
    case SC_GAME: lim = GAME_MS; break;
    case SC_INFO: lim = INFO_MS; break;
    case SC_REFUSE: lim = REFUSE_MS; break;
    case SC_LEAVE: lim = LEAVE_MS; break;
    case SC_RETURN: lim = RETURN_MS; break;
    case SC_FEED: lim = FEED_PICK_MS; break;
    case SC_MUSIC: lim = MUSIC_MS; lim_quiet = MODE_IDLE_MS; break;
    case SC_CLOUD: case SC_MARBLE: case SC_BALLOON: lim = CLOUD_MS; break;
    case SC_TOYS: case SC_DRESS: lim_quiet = MODE_IDLE_MS; break;
    case SC_ALBUM: lim_quiet = ALBUM_IDLE_MS; break;
    case SC_ASK: lim = R_LISTEN_MS + R_BUTTONS_MS + R_REPLY_MS; break;   // never wedged waiting for an answer
    case SC_SETTINGS:                      // a grown-up who walked away: gone once the screen is dark
        lim_quiet = SCREEN_OFF_MS;
        break;
    }
    if (lim && in > lim + dt + SLACK) fail("scene outlived its natural length");
    if (lim_quiet && quiet > lim_quiet + dt + SLACK) fail("scene outlived its idle timeout");
    if (g.scene == SC_SLEEP && S.sleep_free > (u64)(TUCK_MS + 120 * 60000 + 60000)) fail("asleep for hours outside the night");
    if (g.scene == SC_LEAVE || g.scene == SC_AWAY) {   // the break, plus one autosave lost to a power cut
        if (g.break_ms() > S.break_max) S.break_max = g.break_ms();   // (the parent page may change it meanwhile)
        u64 most = LEAVE_MS + (u64)S.break_max + S.rewound + 5000 + dt;
        if (S.now - S.leave_at > most && g.away_ms) {
            if (trace) std::printf("  door: away_ms %u until %u utc %u my utc %u rtc %d\n", g.away_ms, g.s.away_until, g.utc_s, S.wall - S.tz, S.rtc);
            fail("the door stayed longer than the break");
        }
    }
    if (g.scene != SC_AWAY || g.away_ms || !g.screen_on) S.back_at = S.now;   // back as soon as someone looks
    if (S.now - S.back_at > SLACK) fail("the break is over, the screen is on, and she is not back");
    // the screen goes dark when nobody touches it
    u64 off = g.scene == SC_AWAY ? AWAY_SCREEN_OFF_MS : SCREEN_OFF_MS;
    if (g.screen_on && S.now - S.lit_at > off + SLACK) fail("the screen stayed lit with nobody there");
}

// ---- the board: one loop of fw/src/main.cpp ---------------------------------------------------

static void store(u32 n) {
    (void)n;
    G.save(S.saved);
    S.saved_at = S.now;
    std::memcpy(&S.last, S.saved, sizeof S.last);
    S.have_save = true;
}

static void sound(bool dark, bool asleep, u32 dt) {
    int line, clip, sfx = G.take_sfx(&line, &clip);
    if (sfx == SFX_NONE && line == V_NONE && clip < 0) return;
    RULE(clip < R_CLIPS && (clip < 0 || G.has_routines), "a routine clip that is not there");
    if (trace) std::printf("  %llu sfx %d line %d (clock %u voice_at %u)\n", S.real, sfx, line, G.clock_ms, G.voice_at);
    RULE(!bedtime_now(G), "sound at bedtime");
    // in the dark only the cable makes a sound (waking by herself is silent; a question lights the screen first)
    (void)asleep;
    // (a routine moment lights the screen first; only a long dark tick can see it dark again by the end)
    RULE(!dark || S.now - S.cable_at <= 2500 || (clip >= 0 && dt >= R_DARK_LINGER_MS), "a sound from a dark toy nobody touched");
    RULE(G.volume() != VOL_OFF, "sound with the volume off");
    RULE(line >= V_NONE && line < V_COUNT, "bad voice line");
    if (line == V_NONE || clip >= 0) return;
    // the core spaces the lines when it cues them; the platform hears a cue up to one tick later
    if (S.voice_at) RULE(S.real - S.voice_at + S.voice_dt + 100 >= 1500, "two spoken lines within 1.5 s");
    if (S.line_at[line]) RULE(S.real - S.line_at[line] + S.line_dt[line] + 100 >= 8000, "the same line again within 8 s");
    S.voice_at = S.line_at[line] = S.real;
    S.voice_dt = S.line_dt[line] = dt;
}

static void platform(u32 f);
static void power_cut(bool restart);

static void loop(u32 dt) {
    S.loops++;
    bool was = G.scene == SC_SETTINGS;
    u32 idle = G.idle_ms;
    u8 lit = G.screen_on, sc = G.scene;
    G.set_boot(S.boot);
    if (G.idle_ms != idle || G.screen_on != lit || G.scene != sc) S.input_at = S.lit_at = S.now;   // IN_NEXT came through
    if (!was && G.scene == SC_SETTINGS) {  // only the gesture: three quick presses, the last held 3 s
        bool held = S.boot && S.now - S.down_at[2] + 60 >= PARENT_HOLD_MS;
        bool quick = S.down_at[0] && S.down_at[1] - S.up_at[0] <= TAP_GAP_MS + 60 && S.down_at[2] - S.up_at[1] <= TAP_GAP_MS + 60;
        RULE(S.parent || (held && quick), "settings opened without the grown-up gesture");
    }
    S.now += dt; S.real += dt;             // the RTC is read at the start of the loop: it is already "now"
    for (S.wall_ms += dt; S.wall_ms >= 1000; S.wall_ms -= 1000) S.wall++;
    if (S.now - S.wall_at >= 1000) {
        S.wall_at = S.now;
        G.set_wall(S.rtc ? S.wall : 0, S.trusted, S.rtc ? S.wall - S.tz : 0);
        u8 p = G.plugged;
        G.set_plugged(S.usb);
        if (G.plugged != p) S.lit_at = S.cable_at = S.now;
    }
    if (S.now - S.batt_at >= 30000) { S.batt_at = S.now; G.set_battery(S.batt); }
    u8 hatched = G.s.hatched, lit_before = G.screen_on;
    bool asleep = G.scene == SC_SLEEP, night = asleep && G.bed_locked();
    u32 f = G.tick(dt);
    if (G.s.hatched != hatched) S.lit_at = S.now;
    RULE(!night || !G.bed_locked() || G.scene == SC_SLEEP, "up in the night window");
    if (f & F_SAVE) store(f);
    if (f & F_CLOCK) { S.wall = G.wall_s; S.wall_ms = 0; S.trusted = true; S.rtc = true; }   // the RTC write
    if (f & F_SOUND) sound(!lit_before && !G.screen_on, asleep, dt);
    // a sample of the frames (a scene's first frame more often): rendering is most of the run time
    if ((f & F_REDRAW) && (S.look.pm(G.scene != S.scene ? 200 : 1) || (G.scene == SC_SETTINGS && S.look.pm(20)))) {
        S.renders++;
        render(G, fb);
        RULE(text_overflows() == 0, "text ran off the screen");
    }
    check(dt);
    platform(f);
}

static void wait(u64 ms, int dt_lo = 16, int dt_hi = 250) {
    for (u64 t = 0; t < ms && !S.failed;) {
        u32 dt = (u32)S.r.in(dt_lo, dt_hi);
        loop(dt);
        t += dt;
    }
}

// The platform's half of the WiFi setup and the firmware update (fw/src/wifi_setup.cpp, ota.cpp).
static void platform(u32 f) {
    if (f & F_LISTEN) {                    // fw: audio.cpp records, net.cpp asks the listener
        int req = G.listen_req, kind = G.listen_kind;
        G.listen_req = LREQ_NONE;
        if (req == LREQ_CANCEL) S.hear_at = 0;
        else if (kind == LK_LEVEL) {       // the TV test's level check: loud, quiet, sustained or not
            S.levels++;
            G.level_result(S.r.in(-90, 0), S.r.in(0, 100));
            if (G.listen_req == LREQ_LISTEN && G.listen_kind == LK_TV) { G.listen_req = LREQ_NONE; S.samples++; }
        } else if (kind == LK_TV) S.samples++;
        else if (S.r.pm(900)) { S.hear_kind = kind; S.hear_at = S.now + (u64)S.r.in(500, 9000); }   // else: never
    }
    if (S.hear_at && S.now >= S.hear_at) {
        S.hear_at = 0;
        G.listen_started();
        static const int ANS[] = {ANS_YES, ANS_NO, ANS_UNCLEAR, ANS_SILENT, ANS_SAID, ANS_NO_LISTENER, ANS_NO_LISTENER, 42};
        int a = ANS[S.r.in(0, 7)];
        if (S.r.pm(700)) { G.listen_result(a); S.answers++; }   // sometimes the answer gets lost
    }
    if (f & F_WIFI) {
        int req = G.wifi_req;
        G.wifi_req = WREQ_NONE;
        if (req == WREQ_START) { S.wifi_next = -1; S.wifi_at = S.now + 1000; }
        else if (req == WREQ_RETRY) { G.wifi_status(WST_JOIN, "Home"); S.wifi_next = 0; S.wifi_at = S.now + 3000; }
        else if (req == WREQ_STOP) S.wifi_at = 0;
    }
    if (S.wifi_at && S.now >= S.wifi_at) {
        static const int RESULT[] = {WST_OK, WST_OK_NOCLOCK, WST_BAD_PASS, WST_NOT_FOUND, WST_TIMEOUT, WST_OFF};
        if (S.wifi_next < 0) { G.wifi_session("Unicorn-AB12", "kz7p2m9q"); S.wifi_next = 0; S.wifi_at = S.now + S.r.in(5, 300) * 1000; }
        else { G.wifi_status(RESULT[S.r.in(0, 5)], "Home"); S.wifi_at = 0; }
        if (G.wifi_stage >= WST_OK) S.wifi_at = 0;
    }
    if (f & F_UPDATE) {
        int req = G.update_req;
        G.update_req = UREQ_NONE;
        S.upd_next = req; S.upd_at = S.now + 2000; S.upd_pct = 0;
    }
    if (S.upd_at && S.now >= S.upd_at) {
        if (S.upd_next == UREQ_CHECK) {
            int k = S.r.in(0, 2);
            G.update_status(k == 0 ? UST_LATEST : k == 1 ? UST_AVAILABLE : UST_NO_WIFI, 0, k == 1 ? S.fw + 1 : 0);
            S.upd_at = 0;
        } else if (S.upd_pct < 100) {
            S.upd_pct += 10; G.update_status(UST_DOWNLOADING, S.upd_pct, 0); S.upd_at = S.now + 3000;
        } else if (G.update_stage == UST_DOWNLOADING) {
            if (S.r.pm(200)) { G.update_status(UST_BAD_SIG, 0, 0); S.upd_at = 0; }
            else { G.update_status(UST_RESTARTING, 100, 0); S.upd_at = S.now + 1500; }
        } else {                           // ota_poll: save her and restart into the new version
            S.upd_at = 0;
            S.fw++;
            store(0);
            power_cut(true);
        }
    }
}

// A power cut: the board comes back with whatever NVS holds (the last F_SAVE), like fw/src/main.cpp.
static void power_cut(bool restart) {
    const Game& g = G;
    if (S.have_save) {                     // what a cut must never lose: it was saved the moment it happened
        RULE(!g.s.hatched || S.last.hatched, "hatched but not saved");
        RULE(g.s.care_days == S.last.care_days, "a care day was not saved");
        RULE(!(g.s.birth_flags & BIRTH_CHOSEN) || (S.last.birth_flags & BIRTH_CHOSEN && S.last.character == g.s.character),
             "the chosen egg was not saved");
        RULE(g.s.accessory == S.last.accessory && g.s.lang == S.last.lang && g.s.volume == S.last.volume &&
             g.s.opts == S.last.opts && g.s.limit_min == S.last.limit_min && g.s.break_min == S.last.break_min &&
             g.s.bright == S.last.bright && g.s.bed10 == S.last.bed10 && g.s.wake10 == S.last.wake10 && g.s.min_ones == S.last.min_ones,
             "a setting or the dress-up was not saved");
        RULE(g.s.r_asked == S.last.r_asked && g.s.week[0] == S.last.week[0] && g.s.cfg_rev == S.last.cfg_rev &&
             g.s.wake_we10 == S.last.wake_we10 && g.s.q_off == S.last.q_off, "a question, a star or a synced setting was not saved");
        RULE(g.s.milestones == S.last.milestones && g.s.party_year == S.last.party_year &&
             g.s.tricks_seen == S.last.tricks_seen && g.s.hatch_day == S.last.hatch_day,
             "a celebration or trick was not saved");
    } else {
        RULE(!g.s.hatched && !(g.s.birth_flags & BIRTH_CHOSEN), "progress without any save");
    }
    // a save taken right now loads into a fresh game and saves to the same bytes
    u8 a[SAVE_SIZE], b[SAVE_SIZE];
    u32 n = G.save(a);
    FRESH.init(S.seed ^ 0x5EED);
    FRESH.set_wall(G.wall_s, G.cal_ok, G.utc_s);   // the clock as the game last saw it
    RULE(FRESH.load(a, n), "a fresh save does not load");
    FRESH.save(b);
    RULE(!std::memcmp(a, b, n), "a loaded save does not save to the same bytes");
    if (trace) for (u32 i = 0; i < n; i++) if (a[i] != b[i]) std::printf("  save byte %u: %d -> %d\n", i, a[i], b[i]);
    bool out = g.scene == SC_LEAVE || g.scene == SC_AWAY;

    if (!restart) {                        // off for a while: the RTC runs on, sometimes it went flat
        u32 off_s = (u32)S.r.in(1, S.r.pm(300) ? 4 * 3600 : 120);
        S.real += off_s * 1000ull;
        if (S.rtc) S.wall += off_s;
        if (S.rtc && S.r.pm(100)) { S.wall = 9497 * 86400u + 12 * 3600; S.trusted = false; }   // back to build time
    }
    S.real += 3000;                        // booting
    bool packs = G.has_routines;
    G.init(S.r.next());
    G.has_routines = packs;                // the SD card / LittleFS is still there
    G.fw_version = (u16)S.fw;
    G.wifi_list_add("Home", false);
    G.wifi_list_add("Grandma", true);
    G.set_wall(S.rtc ? S.wall : 0, S.trusted, S.rtc ? S.wall - S.tz : 0);
    if (S.have_save) RULE(G.load(S.saved, SAVE_SIZE), "the last save does not load");
    if (trace) std::printf("  %llu cut: loaded %s away_ms %u until %u utc %u (saved away_s %u until %u)\n", S.now, SCENES[G.scene],
                           G.away_ms, G.s.away_until, G.utc_s, S.last.away_s, S.last.away_until);
    S.wall_at = S.now;
    S.boot = false;
    S.voice_at = 0;                        // a new boot: the core's voice memory starts afresh
    for (int i = 0; i < V_COUNT; i++) S.line_at[i] = 0;
    S.wifi_at = S.upd_at = S.hear_at = 0;
    S.age_max = G.s.age_s;                 // time stood still at the last save
    S.scene = G.scene; S.scene_at = S.now; S.lit_at = S.now; S.sleep_free = 0;
    // without a clock, what passed since the last save is gone (the break never gets shorter)
    if (G.scene == SC_AWAY && out) S.rewound += S.now - S.saved_at;
    else if (G.scene == SC_AWAY) { S.leave_at = S.now; S.rewound = 0; }
    check(0);
}

// ---- the child --------------------------------------------------------------------------------

static void input(int type, int x = 0, int y = 0) {
    if (trace) std::printf("  %llu %s in %d (%d,%d)\n", S.now, SCENES[G.scene], type, x, y);
    bool was = G.scene == SC_SETTINGS, night = G.scene == SC_SLEEP && G.bed_locked();
    G.input(type, x, y);
    S.input_at = S.lit_at = S.now;
    RULE(!night || G.scene == SC_SLEEP || G.scene == SC_SETTINGS, "woken up in the night window");
    if (!was && G.scene == SC_SETTINGS) RULE(S.parent, "settings opened by a touch or a button");
    check(0);
}

static void spot(int* x, int* y) {         // where a five-year-old pokes
    int k = S.r.in(0, 99);
    if (k < 30) { *x = S.r.in(0, SCREEN_W - 1); *y = S.r.in(MENU_Y0, SCREEN_H - 1); }            // the menu strip
    else if (k < 50) { *x = S.r.in(PET_X0, PET_X1); *y = S.r.in(PET_Y0, PET_Y1); }                // her
    else if (k < 58) { *x = S.r.in(0, SCREEN_W - 1); *y = S.r.in(0, HEADER_Y1); }                // the name bar
    else if (k < 64) { *x = S.r.in(HOUSE_X0, SCREEN_W - 1); *y = S.r.in(0, HOUSE_Y1); }          // the house
    else if (k < 72) {                                                                             // the very edges
        *x = S.r.pm(500) ? (S.r.pm(500) ? 0 : SCREEN_W - 1) : S.r.in(0, SCREEN_W - 1);
        *y = S.r.pm(500) ? (S.r.pm(500) ? 0 : SCREEN_H - 1) : S.r.in(0, SCREEN_H - 1);
    } else { *x = S.r.in(0, SCREEN_W - 1); *y = S.r.in(0, SCREEN_H - 1); }
}

static void boot_edge(bool down) {         // the key, with a little contact bounce now and then
    if (S.r.pm(250))
        for (int i = S.r.in(1, 3); i > 0; i--) { S.boot = !down; loop((u32)S.r.in(2, 8)); S.boot = down; loop((u32)S.r.in(2, 8)); }
    S.boot = down;
    if (down) { for (int i = 0; i < 2; i++) S.down_at[i] = S.down_at[i + 1], S.up_at[i] = S.up_at[i + 1]; S.down_at[2] = S.now; S.up_at[2] = 0; }
    else S.up_at[2] = S.now;
    S.input_at = S.lit_at = S.now;
}

static void boot_press(int hold, int gap) {
    boot_edge(true);
    for (int t = 0; t < hold && !S.failed; t += 20) loop(20);
    boot_edge(false);
    wait((u64)gap, 10, 40);
    S.input_at = S.lit_at = S.now;         // IN_NEXT arrives once the taps are over
}

static void child() {
    int k = S.r.in(0, 99), x, y;
    if (k < 45) {                          // a burst of taps, often on the same spot
        S.action = "taps";
        spot(&x, &y);
        for (int n = S.r.in(1, 12); n > 0 && !S.failed; n--) {
            if (S.r.pm(400)) spot(&x, &y);
            int jx = x + S.r.in(-3, 3);
            input(IN_TOUCH, jx < 0 ? 0 : jx >= SCREEN_W ? SCREEN_W - 1 : jx, y);
            wait((u64)S.r.in(30, S.r.pm(200) ? 3000 : 600));
        }
    } else if (k < 55) {
        S.action = "pwr";
        for (int n = S.r.in(1, 4); n > 0 && !S.failed; n--) { input(IN_SELECT); wait((u64)S.r.in(50, 900)); }
    } else if (k < 62) {
        S.action = "shake";
        for (int n = S.r.in(1, 6); n > 0 && !S.failed; n--) { input(IN_SHAKE); wait((u64)S.r.in(40, 400)); }
    } else if (k < 75) {
        S.action = "boot";
        int m = S.r.in(0, 99);
        if (m < 50) for (int n = S.r.in(1, 4); n > 0; n--) boot_press(S.r.in(40, 300), S.r.in(60, 900));
        else if (m < 75) boot_press(S.r.in(500, 6000), S.r.in(100, 800));           // a long hold
        else if (m < 95) {                                                          // mashing, then holding on
            for (int n = S.r.in(2, 8); n > 0; n--) boot_press(S.r.in(30, 250), S.r.in(40, 500));
            boot_press(S.r.in(200, 5000), 500);
        } else { boot_press(100, 150); boot_press(100, 150); boot_press(3300, 600); }   // the gesture, by accident
        wait(500);
    } else if (k < 82) {
        S.action = "tilt";
        for (int n = S.r.in(5, 60); n > 0 && !S.failed; n--) {
            G.set_tilt(S.r.pm(50) ? S.r.in(-500, 500) : S.r.in(-100, 100));
            wait(100, 80, 120);
        }
    } else {
        S.action = "wait";
        wait((u64)S.r.in(500, 30000));
    }
}

static void parent_visit() {               // a grown-up opens the settings, pokes, usually closes them
    S.action = "parent";
    S.parent = true;
    input(IN_PARENT);
    for (int n = S.r.in(0, 8); n > 0 && !S.failed && G.scene == SC_SETTINGS; n--) {
        if (S.r.pm(300) && G.set_page == PG_MAIN) {   // the language row
            int slot = set_row_slot(SET_ROWS, SET_LANG);
            input(IN_TOUCH, 300, SET_Y0 + slot * SET_ROW_H + 18);
        } else if (S.r.pm(200)) input(IN_NEXT);
        else { int x, y; spot(&x, &y); input(IN_TOUCH, x, y); }
        wait((u64)S.r.in(100, 2000));
    }
    if (S.r.pm(850))
        for (int n = 0; n < 10 && G.scene == SC_SETTINGS && !S.failed; n++) {   // GERİ / ÇIKIŞ until out
            int rows = G.set_rows();
            if (rows) input(IN_TOUCH, 300, SET_Y0 + set_row_slot(rows, rows - 1) * SET_ROW_H + 18);
            else input(IN_TOUCH, 90, (WBTN_Y0 + WBTN_Y1) / 2);
            wait(200);
        }
    S.parent = false;
    wait(200);
}

static void clock_jump() {
    S.action = "clock";
    int k = S.r.in(0, 9);
    u32 day = S.wall / 86400 * 86400;
    static const int AT[] = {-1, 7 * 60, 17 * 60, 20 * 60, 24 * 60};   // -1: her bedtime
    if (k < 4) {                           // just before a change of the day: evening, night, bedtime, morning
        int m = AT[S.r.in(0, 4)];
        if (m < 0) m = S.r.pm(500) ? G.clock_min(T_BED) : G.clock_min(T_WAKE);
        S.wall = day + (u32)m * 60 - (u32)S.r.in(1, 90);
    } else if (k < 6) S.wall += (u32)S.r.in(60, 30 * 3600);              // a jump ahead
    else if (k < 7) { S.wall -= (u32)S.r.in(1, 7200); S.clock_back = true; }   // a correction backwards
    else if (k < 8) S.trusted = !S.trusted;                              // NTP sync, or a guess
    else if (k < 9) S.tz = S.tz == 3600 ? 7200 : 3600;                   // summer time
    else S.rtc = !S.rtc;
    if (trace) std::printf("  clock %u rtc %d trusted %d\n", S.wall, S.rtc, S.trusted);
}

static void away(u64 ms) {                 // nobody there: minutes to hours, the screen dark
    S.action = "away";
    for (u64 t = 0; t < ms && !S.failed;) {
        bool fast = G.s.hatched && (G.scene == SC_HOME || G.scene == SC_SLEEP || G.scene == SC_AWAY || G.scene == SC_LOUNGE);
        u32 dt = G.screen_on ? (u32)S.r.in(50, 250) : fast ? (u32)S.r.in(1000, 60000) : (u32)S.r.in(100, 1000);
        loop(dt);
        t += dt;
    }
}

static void play_session() {               // ten minutes and more of play, the screen never dark
    S.action = "session";
    u64 end = S.now + (u64)S.r.in(5, 25) * 60000;
    while (S.now < end && !S.failed) {
        int x, y;
        spot(&x, &y);
        int k = S.r.in(0, 9);
        input(k < 7 ? IN_TOUCH : k < 9 ? IN_SELECT : IN_SHAKE, x, y);
        wait((u64)S.r.in(200, 12000), 50, 400);
    }
}

// The parent page: now and then a config arrives (sometimes nonsense), and the status went out.
static void page_sync() {
    S.action = "page";
    Settings p = G.settings();
    for (int n = S.r.in(1, 5); n > 0; n--) {
        int k = S.r.in(0, K_COUNT - 1);
        p.v[k] = (u16)(S.r.pm(800) ? S.r.in(0, k == K_QUESTIONS ? 4095 : k >= K_WAKE_WEEKDAY && k <= K_BEDTIME ? 1439 : 3) : S.r.next());
    }
    for (int i = 0; i < HOL_MAX; i++) { p.hol[i][0] = (u16)(S.wall / 86400 + S.r.in(-3, 3)); p.hol[i][1] = (u16)(p.hol[i][0] + S.r.in(-1, 5)); }
    if (S.r.pm(800)) S.page_rev++;
    p.tv_day = (u16)(S.r.pm(500) ? 0 : S.wall / 86400 - (u32)S.r.in(-2, 10));
    SyncMark sent = G.sync_mark();
    G.apply_settings(S.page_rev, p, (u16)S.r.next());
    if (S.r.pm(800)) G.settings_synced(sent);
    wait(100);                             // the main loop ticks (and saves) right after
}

static bool run_seed(u32 seed) {
    std::memset(&S, 0, sizeof S);
    S.seed = seed;
    S.r.x = seed * 2654435761u + 1;
    S.look.x = seed * 40503u + 7;
    S.action = "boot";
    S.rtc = S.r.pm(850);
    S.trusted = S.r.pm(700);
    S.tz = S.r.pm(500) ? 3600 : 7200;
    S.wall = (9497u + (u32)S.r.in(0, 4 * 365)) * 86400u + (u32)S.r.in(0, 86399);   // 2026..2030
    S.usb = S.r.pm(200);
    S.batt = S.r.in(-1, 100);
    S.fw = 3;
    S.scene = -1;
    G.init(seed);
    G.fw_version = (u16)S.fw;
    G.has_routines = S.r.pm(700);          // routines_tr.bin on the card (or LittleFS), or not
    if (S.r.pm(200)) G.s.opts |= OPT_TV_TEST;
    G.set_wall(S.rtc ? S.wall : 0, S.trusted, S.rtc ? S.wall - S.tz : 0);
    G.set_plugged(S.usb);
    G.set_battery(S.batt);
    check(0);
    while (S.now < (u64)DAYS * 86400000 && !S.failed) {
        int k = S.r.in(0, 999);
        if (k < 600) child();
        else if (k < 700) away((u64)S.r.in(1, 60) * 60000);
        else if (k < 740) away((u64)S.r.in(1, 8) * 3600000);
        else if (k < 760) play_session();
        else if (k < 820) clock_jump();
        else if (k < 870) { S.action = "cable"; S.usb = !S.usb; if (S.r.pm(300)) { wait(1100); S.usb = !S.usb; wait(1100); S.usb = !S.usb; } }
        else if (k < 900) { S.action = "battery"; static const int B[] = {-1, 0, 3, 9, 10, 49, 50, 77, 100}; S.batt = B[S.r.in(0, 8)]; }
        else if (k < 930) { S.action = "cut"; power_cut(false); }
        else if (k < 950) parent_visit();
        else if (k < 965) page_sync();
        else { S.action = "wait"; wait((u64)S.r.in(100, 5000)); }
        if (trace) std::printf("%llu %s -> %s\n", S.now, S.action, SCENES[G.scene]);
    }
    return !S.failed;
}

int main(int argc, char** argv) {
    fb = (u16*)std::malloc(sizeof(u16) * SCREEN_W * SCREEN_H);
    u32 first = 1, last = SEEDS;
    if (argc > 1) first = last = (u32)std::strtoul(argv[1], 0, 10);
    trace = argc > 2;
    int bad = 0;
    u64 loops = 0, days = 0, renders = 0;
    for (u32 s = first; s <= last; s++) {
        if (!run_seed(s)) bad++;
        loops += S.loops;
        T.questions += S.questions; T.answers += S.answers; T.levels += S.levels; T.samples += S.samples;
        renders += S.renders;
        days += S.now / 86400000;
    }
    if (first != last)
        for (int sc = 0; sc <= SC_BALLOON; sc++)
            if (!visits[sc]) { std::printf("  never reached scene %s\n", SCENES[sc]); bad++; }
    std::printf("  scenes entered:");
    for (int sc = 0; sc <= SC_BALLOON; sc++) std::printf(" %s %llu", SCENES[sc], visits[sc]);
    std::printf("\n");
    for (int k = 0; k < nkinds; k++) std::printf("  %4d x %s\n", kind_n[k], kinds[k]);
    std::printf("  routine: %u questions, %u answers, %u level checks, %u TV samples\n", T.questions, T.answers, T.levels, T.samples);
    if (first != last && (!T.questions || !T.answers || !T.levels)) { std::printf("  the routine was never exercised\n"); bad++; }
    std::printf("monkey: %u seeds, %llu days played, %llu loops, %llu frames rendered, %d failed\n", last - first + 1, days,
                loops, renders, bad);
    std::free(fb);
    return bad ? 1 : 0;
}
