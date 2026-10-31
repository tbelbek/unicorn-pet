// Host tests for the game core, one block per event. Also dumps a screenshot per event
// to test/out/*.ppm so every scene can be eyeballed.
//   make -C test        (builds with ASan + UBSan and runs)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/unicorn.h"

using namespace up;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define EQ(a, b) do { checks++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { failures++; \
    std::printf("  FAIL %s:%d  %s == %s  (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

static u16 fb[SCREEN_W * SCREEN_H];

// Advance in 100 ms ticks like the firmware loop; returns OR of all flags.
static u32 run(Game& g, u32 ms) {
    u32 flags = 0;
    for (u32 t = 0; t < ms; t += 100) flags |= g.tick(100);
    return flags;
}

static Game hatched(u32 seed = 42) {
    Game g;
    g.init(seed);
    for (int i = 0; i < EGG_TAPS; i++) g.input(IN_SELECT, 0, 0);
    run(g, HATCH_MS + 100);
    return g;
}

static void tap_menu(Game& g, int item) { g.input(IN_TOUCH, 46 + item * MENU_STEP, 400); }

static void shot(const Game& g, const char* name) {
    render(g, fb);
    char path[256];
    std::snprintf(path, sizeof path, "out/%s.ppm", name);
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u16 c = fb[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                                (unsigned char)((c & 31) << 3)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

static int lit_pixels(int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += fb[y * SCREEN_W + x] != 0;
    return n;
}

#define TEST(name) static void name()

TEST(egg_hatches_after_a_minute) {
    Game g; g.init(1);
    EQ(g.scene, SC_EGG); EQ(g.s.hatched, 0);
    shot(g, "01_egg");
    run(g, EGG_HATCH_MS - 1000);
    EQ(g.scene, SC_EGG);
    u32 f = run(g, 2000);
    EQ(g.scene, SC_HATCH); EQ(g.s.hatched, 1);
    CHECK(f & F_SAVE);
    shot(g, "02_hatch");
    run(g, HATCH_MS);
    EQ(g.scene, SC_HOME);
}

TEST(egg_hatches_after_five_taps) {
    Game g; g.init(2);
    for (int i = 0; i < EGG_TAPS - 1; i++) g.input(IN_TOUCH, 184, 200);
    EQ(g.scene, SC_EGG); EQ(g.taps, EGG_TAPS - 1);
    shot(g, "01b_egg_cracked");
    g.input(IN_TOUCH, 184, 200);
    EQ(g.scene, SC_HATCH);
}

TEST(needs_decay_per_minute) {
    Game g = hatched();
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 8000);
    run(g, 60 * 60000);
    EQ(g.stat(HUNGER), 8000 - 60 * DECAY_HUNGER);
    EQ(g.stat(HAPPY), 8000 - 60 * DECAY_HAPPY);
    EQ(g.stat(ENERGY), 8000 - 60 * DECAY_ENERGY);
    EQ(g.stat(CLEAN), 8000 - 60 * DECAY_CLEAN);
}

TEST(one_big_tick_equals_many_small_ones) {
    Game a = hatched(7), b = hatched(7);
    a.tick(8 * 3600000u);                  // e.g. waking from a long light-sleep
    run(b, 8 * 3600000u);
    for (int i = 0; i < NSTATS; i++) EQ(a.stat(i), b.stat(i));
    EQ(a.s.age_s, b.s.age_s);
}

TEST(never_dies_after_a_week_of_neglect) {
    Game g = hatched();
    g.tick(7u * 24 * 3600000u);
    for (int i = 0; i < NSTATS; i++) EQ(g.stat(i), 0);
    EQ(g.scene, SC_HOME);
    EQ(g.mood(), M_HUNGRY);                // hungry wins over the other needs
    g.input(IN_SELECT, 0, 0);              // wake screen
    g.input(IN_SELECT, 0, 0);              // feed (menu 0)
    EQ(g.scene, SC_EAT);
    CHECK(g.stat(HUNGER) > 0);
}

TEST(event_feed) {
    Game g = hatched();
    g.set_stat(HUNGER, 2000); g.set_stat(HAPPY, 5000);
    g.tick(0);
    shot(g, "03_home_hungry");
    u32 f;
    tap_menu(g, MENU_FEED);
    EQ(g.scene, SC_EAT);
    EQ(g.stat(HUNGER), 2000 + FEED_GAIN);
    EQ(g.stat(HAPPY), 5000 + TREAT_HAPPY);
    f = g.tick(100);
    CHECK(f & F_SAVE);
    CHECK(f & F_REDRAW);
    shot(g, "04_eat");
    g.input(IN_NEXT, 0, 0);                // input during the animation is ignored
    EQ(g.scene, SC_EAT);
    run(g, ACTION_MS);
    EQ(g.scene, SC_HOME);
    g.set_stat(HUNGER, 9000);
    g.input(IN_SELECT, 0, 0);
    EQ(g.stat(HUNGER), MAX_STAT);          // capped
}

TEST(event_play) {
    Game g = hatched();
    g.set_stat(HAPPY, 3000); g.set_stat(ENERGY, 6000); g.set_stat(HUNGER, 6000); g.set_stat(CLEAN, 6000);
    g.input(IN_NEXT, 0, 0);                // buttons: BOOT moves the cursor...
    EQ(g.menu, MENU_PLAY);
    g.input(IN_SELECT, 0, 0);              // ...PWR selects
    EQ(g.scene, SC_PLAY);
    EQ(g.stat(HAPPY), 3000 + PLAY_HAPPY);
    EQ(g.stat(ENERGY), 6000 - PLAY_COST);
    EQ(g.stat(HUNGER), 6000 - PLAY_HUNGER);
    EQ(g.stat(CLEAN), 6000 - PLAY_DIRT);
    g.tick(100);
    shot(g, "05_play");
    run(g, 350);
    shot(g, "05b_play_frame2");
    run(g, ACTION_MS);
    EQ(g.scene, SC_HOME);
}

TEST(event_play_refused_when_tired) {
    Game g = hatched();
    g.set_stat(ENERGY, PLAY_MIN_ENERGY - 1);
    g.set_stat(HAPPY, 4000);
    tap_menu(g, MENU_PLAY);
    EQ(g.scene, SC_REFUSE);
    EQ(g.stat(HAPPY), 4000);               // no reward, no cost
    EQ(g.stat(ENERGY), PLAY_MIN_ENERGY - 1);
    g.tick(100);
    shot(g, "06_refuse");
    run(g, REFUSE_MS);
    EQ(g.scene, SC_HOME);
}

TEST(event_sleep_until_rested) {
    Game g = hatched();
    g.set_stat(ENERGY, 2000); g.set_stat(HUNGER, 8000); g.set_stat(HAPPY, 5000);
    tap_menu(g, MENU_SLEEP);
    EQ(g.scene, SC_SLEEP);
    g.tick(100);
    shot(g, "07_sleep");
    // night colours: the moon is drawn in the top-right sky
    CHECK(lit_pixels(SCREEN_W - 90, 100, SCREEN_W - 40, 150) > 100);
    run(g, 60 * 60000);
    EQ(g.stat(ENERGY), 2000 + 60 * SLEEP_ENERGY_GAIN);
    EQ(g.stat(HUNGER), 8000 - 60 * (DECAY_HUNGER / 2));   // slower while asleep
    EQ(g.scene, SC_SLEEP);
    CHECK(!g.screen_on);                   // screen went dark while she sleeps
    int minutes = 0;
    while (g.scene == SC_SLEEP && minutes < 120) { run(g, 60000); minutes++; }
    EQ(g.scene, SC_HOME);                  // woke up by herself...
    CHECK(g.stat(ENERGY) >= MAX_STAT - DECAY_ENERGY);   // ...as soon as she was rested
    CHECK(minutes < 60);
    CHECK(g.stat(HAPPY) >= 5000 + TREAT_HAPPY - 120 * DECAY_HAPPY);
}

TEST(event_sleep_woken_early) {
    Game g = hatched();
    g.set_stat(ENERGY, 2000);
    tap_menu(g, MENU_SLEEP);
    run(g, 5000);
    g.input(IN_TOUCH, 184, 200);
    EQ(g.scene, SC_HOME);
}

TEST(event_wash) {
    Game g = hatched();
    g.set_stat(CLEAN, 1000); g.set_stat(HAPPY, 5000);
    g.tick(0);
    shot(g, "08_home_dirty");
    tap_menu(g, MENU_WASH);
    EQ(g.scene, SC_WASH);
    EQ(g.stat(CLEAN), MAX_STAT);
    EQ(g.stat(HAPPY), 5000 + TREAT_HAPPY);
    g.tick(100);
    shot(g, "09_wash");
    run(g, ACTION_MS);
    EQ(g.scene, SC_HOME);
}

TEST(event_info_shows_the_face) {
    Game g = hatched();
    g.input(IN_TOUCH, 184, 220);           // tap the unicorn
    EQ(g.scene, SC_INFO);
    g.tick(100);
    shot(g, "10_info");
    run(g, INFO_MS);
    EQ(g.scene, SC_HOME);
}

TEST(screen_turns_off_and_first_touch_only_wakes) {
    Game g = hatched();
    g.input(IN_NEXT, 0, 0);                // last interaction
    u32 f = run(g, SCREEN_OFF_MS - 200);
    CHECK(g.screen_on);
    f = run(g, 400);
    CHECK(!g.screen_on);
    f = g.tick(100);
    CHECK(!(f & F_SCREEN_ON));
    CHECK(!(f & F_REDRAW));
    u8 before = g.menu;
    g.input(IN_NEXT, 0, 0);
    CHECK(g.screen_on);
    EQ(g.menu, before);                    // the waking press did not move the cursor
    f = g.tick(100);
    CHECK(f & F_REDRAW);
}

TEST(save_load_roundtrip_and_corruption) {
    Game g = hatched(9);
    g.set_stat(HUNGER, 1234); g.set_stat(CLEAN, 4321);
    run(g, 90000);
    u8 buf[SAVE_SIZE];
    EQ(g.save(buf), SAVE_SIZE);
    Game h; h.init(123);
    CHECK(h.load(buf, sizeof buf));
    for (int i = 0; i < NSTATS; i++) EQ(h.stat(i), g.stat(i));
    EQ(h.s.character, g.s.character);
    EQ(h.s.age_s, g.s.age_s);
    EQ(h.scene, SC_HOME);
    buf[6] ^= 0x40;                        // flip a bit in the payload
    Game k; k.init(5);
    CHECK(!k.load(buf, sizeof buf));
    EQ(k.scene, SC_EGG);                   // stays a fresh egg
    CHECK(!k.load(buf, 3));                // truncated
}

TEST(power_off_freezes_time) {
    Game g = hatched(11);
    u8 buf[SAVE_SIZE];
    g.save(buf);
    // ...board is powered off for a weekend: no ticks happen...
    Game h; h.init(0);
    h.load(buf, sizeof buf);
    for (int i = 0; i < NSTATS; i++) EQ(h.stat(i), g.stat(i));
}

TEST(autosave_every_five_minutes) {
    Game g = hatched();
    g.tick(100);
    g.save_ms = 0;                         // align with the autosave clock
    u32 f = run(g, AUTOSAVE_MS - 1000);
    CHECK(!(f & F_SAVE));
    f = run(g, 2000);
    CHECK(f & F_SAVE);
}

TEST(moods_follow_needs) {
    Game g = hatched();
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 8000);
    EQ(g.mood(), M_HAPPY);
    g.tick(0); shot(g, "11_home_happy");
    g.set_stat(HAPPY, 6000); EQ(g.mood(), M_IDLE);
    g.set_stat(ENERGY, 1000); EQ(g.mood(), M_SLEEPY);
    g.tick(0); shot(g, "12_home_sleepy");
    g.set_stat(ENERGY, 8000); g.set_stat(CLEAN, 1000); EQ(g.mood(), M_SAD);
    g.tick(0); shot(g, "13_home_sad");
}

TEST(every_scene_renders_for_every_character) {
    for (u32 seed = 1; seed < 60; seed++) {
        Game g = hatched(seed);
        for (int item = 0; item < MENU_COUNT; item++) {
            g.set_stat(ENERGY, 5000);
            tap_menu(g, item);
            for (int f = 0; f < 4; f++) { g.tick(300); render(g, fb); }
            CHECK(lit_pixels(0, 0, SCREEN_W, SCREEN_H) > 5000);
            g.input(IN_SELECT, 0, 0);
            run(g, ACTION_MS);
        }
    }
}

// Pull the pending cue the way the firmware/emulator do: only when tick() says F_SOUND.
static int cue_after(Game& g) {
    u32 f = g.tick(0);
    return (f & F_SOUND) ? g.take_sfx() : SFX_NONE;
}

TEST(sound_cue_for_every_event) {
    Game g; g.init(3);
    g.input(IN_TOUCH, 184, 200);
    EQ(cue_after(g), SFX_TAP);                            // egg tap
    for (int i = 1; i < EGG_TAPS; i++) g.input(IN_TOUCH, 184, 200);
    EQ(cue_after(g), SFX_HATCH);
    run(g, HATCH_MS + 100);
    g.input(IN_NEXT, 0, 0);
    EQ(cue_after(g), SFX_TAP);                            // cursor move
    g.set_stat(ENERGY, 6000);
    tap_menu(g, MENU_FEED);  EQ(cue_after(g), SFX_EAT);   run(g, ACTION_MS);
    tap_menu(g, MENU_PLAY);  EQ(cue_after(g), SFX_PLAY);  run(g, ACTION_MS);
    tap_menu(g, MENU_WASH);  EQ(cue_after(g), SFX_WASH);  run(g, ACTION_MS);
    g.set_stat(ENERGY, 100);
    tap_menu(g, MENU_PLAY);  EQ(cue_after(g), SFX_REFUSE); run(g, REFUSE_MS);
    tap_menu(g, MENU_SLEEP); EQ(cue_after(g), SFX_SLEEP);
    g.input(IN_TOUCH, 184, 200);
    EQ(cue_after(g), SFX_WAKE);                           // woken early
    tap_menu(g, MENU_SLEEP); cue_after(g);
    int guard = 0;
    while (g.scene == SC_SLEEP && guard++ < 300) g.tick(60000);
    EQ(g.take_sfx(), SFX_WAKE);                           // woke up by herself
    g.input(IN_SELECT, 0, 0);                             // wake screen (went dark while asleep)
    g.input(IN_TOUCH, 184, 220);
    EQ(cue_after(g), SFX_INFO);
    EQ(cue_after(g), SFX_NONE);                           // a cue is delivered once
}

TEST(sound_toggle_on_info_screen) {
    Game g = hatched();
    EQ(g.s.muted, 0);
    g.input(IN_TOUCH, 184, 220);                          // open info
    g.tick(0); g.take_sfx();
    g.input(IN_TOUCH, 320, 18);                           // tap "SES ACIK"
    EQ(g.s.muted, 1);
    EQ(g.scene, SC_INFO);                                 // stays on the info screen
    u32 f = g.tick(0);
    CHECK(f & F_SAVE);
    CHECK(!(f & F_SOUND));                                // muted: even the toggle is silent
    shot(g, "14_info_muted");
    g.input(IN_TOUCH, 184, 300);                          // close
    tap_menu(g, MENU_FEED);
    f = g.tick(0);
    CHECK(!(f & F_SOUND));
    EQ(g.take_sfx(), SFX_NONE);
    u8 buf[SAVE_SIZE]; g.save(buf);                        // survives a power cut
    Game h; h.init(1); CHECK(h.load(buf, sizeof buf));
    EQ(h.s.muted, 1);
}

TEST(event_shake) {
    Game g = hatched();
    g.set_stat(ENERGY, 6000); g.set_stat(HAPPY, 3000);
    g.input(IN_SHAKE, 0, 0);                              // shaking = let's play
    EQ(g.scene, SC_PLAY);
    EQ(g.menu, MENU_PLAY);
    EQ(g.stat(HAPPY), 3000 + PLAY_HAPPY);
    g.input(IN_SHAKE, 0, 0);                              // ignored mid-animation
    EQ(g.stat(HAPPY), 3000 + PLAY_HAPPY);
    run(g, ACTION_MS);
    g.set_stat(ENERGY, 100);
    g.input(IN_SHAKE, 0, 0);
    EQ(g.scene, SC_REFUSE);                               // too tired, even when shaken
    run(g, REFUSE_MS);
    g.set_stat(ENERGY, 2000);
    tap_menu(g, MENU_SLEEP);
    g.input(IN_SHAKE, 0, 0);
    EQ(g.scene, SC_HOME);                                 // shaking wakes her
    run(g, SCREEN_OFF_MS + 500);
    CHECK(!g.screen_on);
    g.input(IN_SHAKE, 0, 0);
    CHECK(g.screen_on);
    EQ(g.scene, SC_HOME);                                 // first shake only wakes the screen
    Game e; e.init(4);
    for (int i = 0; i < EGG_TAPS; i++) e.input(IN_SHAKE, 0, 0);
    EQ(e.scene, SC_HATCH);                                // shaking the egg hatches it too
}

TEST(melodies_are_short_and_audible) {
    for (int id = SFX_NONE + 1; id < SFX_COUNT; id++) {
        int n = 0;
        const Note* m = melody(id, &n);
        CHECK(m != 0);
        CHECK(n > 0);
        int total = 0;
        for (int i = 0; i < n; i++) {
            total += m[i].ms;
            CHECK(m[i].hz == 0 || (m[i].hz >= 100 && m[i].hz <= 5000));
        }
        CHECK(total > 0 && total <= 1500);
    }
    int n = 7;
    CHECK(melody(SFX_NONE, &n) == 0 && n == 0);
    CHECK(melody(99, &n) == 0);
}

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
#define T(n) {#n, n}
        T(egg_hatches_after_a_minute), T(egg_hatches_after_five_taps), T(needs_decay_per_minute),
        T(one_big_tick_equals_many_small_ones), T(never_dies_after_a_week_of_neglect),
        T(event_feed), T(event_play), T(event_play_refused_when_tired), T(event_sleep_until_rested),
        T(event_sleep_woken_early), T(event_wash), T(event_info_shows_the_face),
        T(screen_turns_off_and_first_touch_only_wakes), T(save_load_roundtrip_and_corruption),
        T(power_off_freezes_time), T(autosave_every_five_minutes), T(moods_follow_needs),
        T(every_scene_renders_for_every_character), T(sound_cue_for_every_event),
        T(sound_toggle_on_info_screen), T(event_shake), T(melodies_are_short_and_audible),
    };
    for (auto& t : tests) {
        int before = failures;
        t.fn();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
