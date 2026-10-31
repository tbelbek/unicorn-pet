// Host tests for the game core, one block per event. Also dumps a screenshot per event
// to test/out/*.ppm so every scene can be eyeballed.
//   make -C test        (builds with ASan + UBSan and runs)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
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
static void tap_pet(Game& g) { g.input(IN_TOUCH, SCREEN_W / 2 + g.pet_x, 230); }
static void tap_header(Game& g) { g.input(IN_TOUCH, SCREEN_W / 2, 60); }

// Pull the pending cue the way the firmware/emulator do: only when tick() says F_SOUND.
static int cue_after(Game& g) {
    u32 f = g.tick(0);
    return (f & F_SOUND) ? g.take_sfx() : SFX_NONE;
}

static void finish_game(Game& g) {        // play out the mini-game and the celebration
    run(g, GAME_MS + 100);
    run(g, CELEBRATE_MS + 100);
}

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

// ---- egg ------------------------------------------------------------------------------------

TEST(egg_hatches_after_a_minute) {
    Game g; g.init(1);
    EQ(g.scene, SC_EGG); EQ(g.s.hatched, 0);
    shot(g, "01_egg");
    run(g, EGG_HATCH_MS - 1000);
    EQ(g.scene, SC_EGG);
    u32 f = run(g, 2000);
    EQ(g.scene, SC_HATCH); EQ(g.s.hatched, 1);
    CHECK(f & F_SAVE);
    run(g, HATCH_MS);
    EQ(g.scene, SC_HOME);
}

TEST(egg_taps_wobble_crack_and_hatch) {
    Game g; g.init(2);
    g.input(IN_TOUCH, 184, 200);
    CHECK(g.wobbling);
    u32 f = g.tick(100);
    CHECK(f & F_REDRAW);                   // wobbling redraws
    run(g, 400);
    EQ(g.wobbling, 0);
    for (int i = 1; i < EGG_TAPS - 1; i++) g.input(IN_TOUCH, 184, 200);
    EQ(g.scene, SC_EGG); EQ(g.taps, EGG_TAPS - 1);
    g.tick(0);
    shot(g, "01b_egg_cracked");
    g.input(IN_TOUCH, 184, 200);
    EQ(g.scene, SC_HATCH);
    CHECK(g.particles() > 0);              // confetti burst
    g.tick(300);
    shot(g, "02a_hatch_split");
    g.tick(900);
    shot(g, "02b_hatch_hello");
}

TEST(hatching_cannot_be_skipped) {
    Game g; g.init(5);
    for (int i = 0; i < EGG_TAPS; i++) g.input(IN_SELECT, 0, 0);
    EQ(g.scene, SC_HATCH);
    g.input(IN_TOUCH, 184, 200);
    g.input(IN_SELECT, 0, 0);
    EQ(g.scene, SC_HATCH);
    run(g, HATCH_MS);
    EQ(g.scene, SC_HOME);
}

// ---- needs and time ---------------------------------------------------------------------------

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

TEST(big_tick_across_waking_up_matches_small_ticks) {
    Game a = hatched(8), b = hatched(8);
    for (Game* g : {&a, &b}) { g->set_stat(ENERGY, 2000); tap_menu(*g, MENU_SLEEP); g->tick(0); }
    a.tick(4 * 3600000u);                  // wakes after ~95 min, then 2+ h awake
    for (int i = 0; i < 4 * 36000; i++) b.tick(100);
    EQ(a.scene, SC_HOME); EQ(b.scene, SC_HOME);
    for (int i = 0; i < NSTATS; i++) EQ(a.stat(i), b.stat(i));
}

TEST(egg_time_after_hatching_counts_as_life) {
    Game a; a.init(9);
    Game b; b.init(9);
    a.tick(EGG_HATCH_MS + 120000);         // hatch, then two more minutes in one go
    run(b, EGG_HATCH_MS + 120000);
    EQ(a.s.age_s, b.s.age_s);
    EQ(a.stat(HUNGER), b.stat(HUNGER));
}

TEST(never_dies_after_a_week_of_neglect) {
    Game g = hatched();
    g.tick(7u * 24 * 3600000u);
    for (int i = 0; i < NSTATS; i++) EQ(g.stat(i), 0);
    EQ(g.scene, SC_HOME);
    EQ(g.mood(), M_HUNGRY);                // hungry wins over the other needs
    EQ(g.messes(), 3);
    g.input(IN_SELECT, 0, 0);              // wake screen
    g.input(IN_SELECT, 0, 0);              // feed (menu 0)
    EQ(g.scene, SC_EAT);
    CHECK(g.stat(HUNGER) > 0);
}

// ---- care events --------------------------------------------------------------------------------

TEST(event_feed_with_rotating_treats) {
    Game g = hatched();
    g.set_stat(HUNGER, 2000); g.set_stat(HAPPY, 5000);
    g.tick(0);
    shot(g, "03_home_hungry");
    tap_menu(g, MENU_FEED);
    EQ(g.scene, SC_EAT);
    EQ(g.stat(HUNGER), 2000 + FEED_GAIN);
    EQ(g.stat(HAPPY), 5000 + TREAT_HAPPY);
    u32 f = g.tick(100);
    CHECK(f & F_SAVE);
    CHECK(f & F_REDRAW);
    int first = g.treat;
    run(g, 300);
    CHECK(g.particles() > 0);              // crumbs fly
    shot(g, "04_eat");
    g.input(IN_NEXT, 0, 0);                // input during the animation is ignored
    EQ(g.scene, SC_EAT);
    run(g, ACTION_MS);
    EQ(g.scene, SC_HOME);
    tap_menu(g, MENU_FEED); run(g, ACTION_MS + 100);
    EQ(g.treat, (first + 1) % TREAT_COUNT);
    tap_menu(g, MENU_FEED);
    EQ(g.treat, (first + 2) % TREAT_COUNT);
    run(g, 1500);
    shot(g, "04b_eat_third_treat");
    run(g, ACTION_MS);
    g.set_stat(HUNGER, 9000);
    tap_menu(g, MENU_FEED);
    EQ(g.stat(HUNGER), MAX_STAT);          // capped; every treat feeds the same
}

TEST(event_play_minigame_by_touch) {
    Game g = hatched();
    g.set_stat(HAPPY, 3000); g.set_stat(ENERGY, 6000); g.set_stat(HUNGER, 6000); g.set_stat(CLEAN, 6000);
    tap_menu(g, MENU_PLAY);
    EQ(g.scene, SC_GAME);
    EQ(g.stat(HAPPY), 3000 + PLAY_HAPPY);  // costs and the base reward are charged once, up front
    EQ(g.stat(ENERGY), 6000 - PLAY_COST);
    EQ(g.stat(HUNGER), 6000 - PLAY_HUNGER);
    EQ(g.stat(CLEAN), 6000 - PLAY_DIRT);
    run(g, 1500);                          // stars rise into view
    CHECK(g.tgt[0].y < SCREEN_H);
    shot(g, "05_minigame");
    g.input(IN_TOUCH, g.tgt[0].x + 20, g.tgt[0].y - 20);    // generous hit box
    EQ(g.score, 1);
    EQ(cue_after(g), SFX_POP);
    CHECK(g.particles() > 0);
    g.input(IN_TOUCH, 2, 2);               // a miss changes nothing
    EQ(g.score, 1);
    int happy = g.stat(HAPPY);
    run(g, GAME_MS);
    EQ(g.scene, SC_PLAY);                  // celebration
    EQ(g.stat(HAPPY), happy + GAME_BONUS);
    EQ(g.take_sfx(), SFX_CHEER);
    shot(g, "05b_celebrate");
    run(g, CELEBRATE_MS);
    EQ(g.scene, SC_HOME);
}

TEST(minigame_with_buttons_and_shake) {
    Game g = hatched();
    g.set_stat(ENERGY, 6000);
    g.input(IN_NEXT, 0, 0);                // BOOT moves the cursor to Play...
    EQ(g.menu, MENU_PLAY);
    g.input(IN_SELECT, 0, 0);              // ...PWR starts it
    EQ(g.scene, SC_GAME);
    run(g, 1000);
    g.input(IN_NEXT, 0, 0);
    EQ(g.sel, 1);
    g.input(IN_SELECT, 0, 0);              // PWR pops the selected star
    EQ(g.score, 1);
    g.input(IN_SHAKE, 0, 0);               // a shake pops one too
    EQ(g.score, 2);
    run(g, GAME_MS);
    EQ(g.scene, SC_PLAY);
}

TEST(minigame_zero_stars_still_rewarded_and_bonus_capped) {
    Game g = hatched();
    g.set_stat(ENERGY, 9000); g.set_stat(HAPPY, 0);
    tap_menu(g, MENU_PLAY);
    finish_game(g);
    EQ(g.scene, SC_HOME);
    CHECK(g.stat(HAPPY) >= PLAY_HAPPY - 20 * DECAY_HAPPY);   // no stars: still a good time
    Game h = hatched();
    h.set_stat(ENERGY, 9000); h.set_stat(HAPPY, 0);
    tap_menu(h, MENU_PLAY);
    for (int i = 0; i < 40; i++) { h.input(IN_SELECT, 0, 0); run(h, 300); }   // ~12 s of pressing
    EQ(h.score, 40);
    run(h, GAME_MS);
    EQ(h.stat(HAPPY), PLAY_HAPPY + GAME_BONUS_MAX);          // capped
}

TEST(minigame_stars_keep_coming) {
    Game g = hatched();
    g.set_stat(ENERGY, 9000);
    tap_menu(g, MENU_PLAY);
    for (int t = 0; t < GAME_MS / 100 - 2; t++) {
        g.tick(100);
        for (int i = 0; i < GAME_TARGETS; i++) {
            CHECK(g.tgt[i].alive);
            CHECK(g.tgt[i].y >= 60);               // respawned at the bottom before leaving
            CHECK(g.tgt[i].x >= 44 && g.tgt[i].x <= SCREEN_W - 44);
        }
    }
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
    CHECK(lit_pixels(SCREEN_W - 90, 100, SCREEN_W - 40, 150) > 100);   // the moon
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
}

TEST(event_sleep_woken_early) {
    Game g = hatched();
    g.set_stat(ENERGY, 2000);
    tap_menu(g, MENU_SLEEP);
    run(g, 5000);
    g.input(IN_TOUCH, 184, 200);
    EQ(g.scene, SC_HOME);
}

TEST(event_wash_dissolves_messes) {
    Game g = hatched();
    g.set_stat(CLEAN, 900); g.set_stat(HAPPY, 5000);
    EQ(g.messes(), 3);
    g.tick(0);
    shot(g, "08_home_messy");
    tap_menu(g, MENU_WASH);
    EQ(g.scene, SC_WASH);
    EQ(g.stat(CLEAN), MAX_STAT);
    EQ(g.messes(), 0);
    EQ(g.wash_messes, 3);                  // still drawn, shrinking, during the bath
    EQ(g.stat(HAPPY), 5000 + TREAT_HAPPY);
    run(g, 600);
    CHECK(g.particles() > 0);              // bubbles
    shot(g, "09_wash");
    run(g, ACTION_MS - 800);
    EQ(g.celebrated, 1);                   // sparkle burst near the end
    run(g, 400);
    EQ(g.scene, SC_HOME);
}

TEST(messes_follow_cleanliness) {
    Game g = hatched();
    g.set_stat(CLEAN, 9000); EQ(g.messes(), 0);
    g.set_stat(CLEAN, 2400); EQ(g.messes(), 1);
    g.set_stat(CLEAN, 1500); EQ(g.messes(), 2);
    g.set_stat(CLEAN, 200);  EQ(g.messes(), 3);
    Game e; e.init(3);
    e.set_stat(CLEAN, 0); EQ(e.messes(), 0);              // eggs make no mess
}

// ---- petting, info, idle life ------------------------------------------------------------------

TEST(event_pet_hearts_and_rationed_happiness) {
    Game g = hatched();
    g.set_stat(HAPPY, 5000);
    run(g, PET_COOLDOWN_MS);
    tap_pet(g);
    EQ(g.scene, SC_HOME);                  // petting does not leave home
    EQ(g.stat(HAPPY), 5000 + PET_HAPPY);
    CHECK(g.particles() >= 3);
    EQ(cue_after(g), SFX_PET);
    CHECK(g.petting);
    shot(g, "10_pet");
    tap_pet(g);                            // again right away: hearts, but no extra points
    EQ(g.stat(HAPPY), 5000 + PET_HAPPY);
    EQ(cue_after(g), SFX_PET);
    run(g, PET_COOLDOWN_MS);
    tap_pet(g);
    CHECK(g.stat(HAPPY) > 5000 + PET_HAPPY);
    for (int i = 0; i < 100; i++) tap_pet(g);
    CHECK(g.particles() <= NPART);         // fixed pool, never grows
    run(g, 3000);
    EQ(g.particles(), 0);                  // and everything fades out
}

TEST(event_info_from_the_header) {
    Game g = hatched();
    tap_header(g);
    EQ(g.scene, SC_INFO);
    EQ(cue_after(g), SFX_INFO);
    g.tick(100);
    shot(g, "11_info");
    run(g, INFO_MS);
    EQ(g.scene, SC_HOME);
}

TEST(idle_life_blinks_walks_and_hit_box_follows) {
    Game g = hatched(77);
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000);      // happy: lively
    int walked = 0, blinked = 0, hopped = 0, minx = 0, maxx = 0, flipped = 0;
    for (int t = 0; t < 1200; t++) {       // 2 minutes, screen kept on
        if (t % 150 == 0) g.input(IN_NEXT, 0, 0);
        g.tick(100);
        walked += g.idle_act == IDLE_WALK;
        blinked += g.idle_act == IDLE_BLINK;
        hopped += g.idle_act == IDLE_HOP;
        flipped += g.facing < 0;
        if (g.pet_x < minx) minx = g.pet_x;
        if (g.pet_x > maxx) maxx = g.pet_x;
        if (g.idle_act == IDLE_WALK && walked == 5) shot(g, "12_walking");
    }
    CHECK(walked > 0); CHECK(blinked > 0); CHECK(hopped > 0); CHECK(flipped > 0);
    CHECK(minx >= -WALK_RANGE && maxx <= WALK_RANGE);
    // the pet hit box moves with her: tap where she is, not where she was
    while (g.idle_act != IDLE_NONE) g.tick(100);
    g.pet_x = WALK_RANGE;
    int before = g.stat(HAPPY);
    run(g, PET_COOLDOWN_MS);
    g.input(IN_TOUCH, PET_X1 + WALK_RANGE - 4, 230);
    CHECK(g.petting);
    CHECK(g.stat(HAPPY) >= before);
}

TEST(needy_unicorn_stays_put) {
    Game g = hatched(78);
    g.set_stat(HUNGER, 500);
    for (int t = 0; t < 600; t++) {
        if (t % 150 == 0) g.input(IN_NEXT, 0, 0);
        g.tick(100);
        CHECK(g.idle_act != IDLE_WALK && g.idle_act != IDLE_HOP);
    }
}

TEST(cosmetics_pause_while_the_screen_is_dark) {
    Game g = hatched();
    g.input(IN_NEXT, 0, 0);
    run(g, SCREEN_OFF_MS + 500);
    CHECK(!g.screen_on);
    i16 x = g.pet_x;
    u32 f = run(g, 60000);
    EQ(g.pet_x, x);
    CHECK(!(f & F_REDRAW));                // nothing to push while dark
}

// ---- screen, menu, save ----------------------------------------------------------------------

TEST(screen_turns_off_and_first_touch_only_wakes) {
    Game g = hatched();
    g.input(IN_NEXT, 0, 0);                // last interaction
    run(g, SCREEN_OFF_MS - 200);
    CHECK(g.screen_on);
    run(g, 400);
    CHECK(!g.screen_on);
    u32 f = g.tick(100);
    CHECK(!(f & F_SCREEN_ON));
    CHECK(!(f & F_REDRAW));
    u8 before = g.menu;
    g.input(IN_NEXT, 0, 0);
    CHECK(g.screen_on);
    EQ(g.menu, before);                    // the waking press did not move the cursor
    f = g.tick(100);
    CHECK(f & F_REDRAW);
}

TEST(menu_gutter_touches_are_ignored) {
    Game g = hatched();
    g.input(IN_TOUCH, 3, 400);             // left of the first button
    EQ(g.scene, SC_HOME);
    g.input(IN_TOUCH, SCREEN_W - 3, 400);  // right of the last one
    EQ(g.scene, SC_HOME);
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
    g.tick(0); shot(g, "13_home_happy");
    g.set_stat(HAPPY, 6000); EQ(g.mood(), M_IDLE);
    g.set_stat(ENERGY, 1000); EQ(g.mood(), M_SLEEPY);
    g.tick(0); shot(g, "14_home_sleepy");
    g.set_stat(ENERGY, 8000); g.set_stat(CLEAN, 1000); EQ(g.mood(), M_SAD);
}

// ---- sound ------------------------------------------------------------------------------------

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
    tap_menu(g, MENU_PLAY);  EQ(cue_after(g), SFX_PLAY);
    run(g, GAME_MS);         EQ(g.take_sfx(), SFX_CHEER); run(g, CELEBRATE_MS);
    tap_menu(g, MENU_WASH);  EQ(cue_after(g), SFX_WASH);  run(g, ACTION_MS);
    g.set_stat(ENERGY, 100);
    tap_menu(g, MENU_PLAY);  EQ(cue_after(g), SFX_REFUSE); run(g, REFUSE_MS);
    tap_pet(g);              EQ(cue_after(g), SFX_PET);
    tap_menu(g, MENU_SLEEP); EQ(cue_after(g), SFX_SLEEP);
    g.input(IN_TOUCH, 184, 200);
    EQ(cue_after(g), SFX_WAKE);                           // woken early
    tap_menu(g, MENU_SLEEP); cue_after(g);
    int guard = 0;
    while (g.scene == SC_SLEEP && guard++ < 300) g.tick(60000);
    EQ(g.take_sfx(), SFX_WAKE);                           // woke up by herself
    g.input(IN_SELECT, 0, 0);                             // wake screen (went dark while asleep)
    tap_header(g);
    EQ(cue_after(g), SFX_INFO);
    EQ(cue_after(g), SFX_NONE);                           // a cue is delivered once
}

TEST(sound_toggle_on_info_screen) {
    Game g = hatched();
    EQ(g.s.muted, 0);
    tap_header(g);
    g.tick(0); g.take_sfx();
    g.input(IN_TOUCH, 320, 26);                           // tap "SES ACIK"
    EQ(g.s.muted, 1);
    EQ(g.scene, SC_INFO);                                 // stays on the info screen
    u32 f = g.tick(0);
    CHECK(f & F_SAVE);
    CHECK(!(f & F_SOUND));                                // muted: even the toggle is silent
    shot(g, "15_info_muted");
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
    g.set_stat(ENERGY, 6000);
    g.input(IN_SHAKE, 0, 0);                              // shaking = let's play
    EQ(g.scene, SC_GAME);
    EQ(g.menu, MENU_PLAY);
    finish_game(g);
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

// ---- review fixes (Codex): determinism, visibility, wrap, dark hatching ------------------------

static bool same(const Game& a, const Game& b) {
    bool ok = a.scene == b.scene && a.scene_ms == b.scene_ms && a.clock_ms == b.clock_ms &&
              a.crng == b.crng && a.pet_x == b.pet_x && a.idle_act == b.idle_act && a.score == b.score &&
              a.treat == b.treat && a.s.age_s == b.s.age_s;
    for (int i = 0; i < NSTATS; i++) ok = ok && a.s.stat[i] == b.s.stat[i];
    for (int i = 0; i < NPART; i++)
        ok = ok && a.part[i].kind == b.part[i].kind && a.part[i].x == b.part[i].x && a.part[i].y == b.part[i].y;
    for (int i = 0; i < GAME_TARGETS; i++) ok = ok && a.tgt[i].x == b.tgt[i].x && a.tgt[i].y == b.tgt[i].y;
    return ok;
}

// advance b by `ms` in irregular slices that still add up exactly
static void ragged(Game& b, u32 ms) {
    static const u32 slices[] = {37, 263, 1000, 3, 97, 600};
    u32 done = 0;
    for (int k = 0; done < ms; k++) {
        u32 d = slices[k % 6];
        if (done + d > ms) d = ms - done;
        b.tick(d);
        done += d;
    }
}

TEST(any_tick_partition_gives_the_same_world) {
    Game a = hatched(31), b = hatched(31);
    CHECK(same(a, b));
    for (Game* g : {&a, &b}) { g->set_stat(HUNGER, 2000); tap_menu(*g, MENU_FEED); }
    run(a, 2500); ragged(b, 2500);                         // mid-meal: crumbs in flight
    CHECK(a.particles() > 0);
    CHECK(same(a, b));
    run(a, 9000); ragged(b, 9000);                         // home again, wandering
    CHECK(same(a, b));
    for (Game* g : {&a, &b}) { g->set_stat(ENERGY, 9000); g->set_stat(HAPPY, MAX_STAT - 100); tap_menu(*g, MENU_PLAY); }
    run(a, 1200); ragged(b, 1200);
    for (Game* g : {&a, &b}) g->input(IN_SELECT, 0, 0);
    run(a, GAME_MS + CELEBRATE_MS + 500); ragged(b, GAME_MS + CELEBRATE_MS + 500);
    EQ(a.scene, SC_HOME);
    CHECK(same(a, b));                                     // bonus, cap and decay in the same order
    Game c = hatched(32), d = hatched(32);
    c.tick(GAME_MS);                                       // one huge tick vs. small ones
    run(d, GAME_MS);
    CHECK(same(c, d));
}

TEST(big_tick_does_not_strand_a_hatching_egg) {
    Game a; a.init(9);
    Game b; b.init(9);
    a.tick(EGG_HATCH_MS + 120000);
    run(b, EGG_HATCH_MS + 120000);
    EQ(a.scene, SC_HOME); EQ(b.scene, SC_HOME);
    CHECK(same(a, b));
}

TEST(only_visible_stars_can_be_popped) {
    Game g = hatched();
    g.set_stat(ENERGY, 9000);
    tap_menu(g, MENU_PLAY);
    for (int i = 0; i < 30; i++) g.input(IN_SELECT, 0, 0);   // no time passes between presses
    CHECK(g.score <= GAME_TARGETS);                          // only what was on screen
    int s0 = g.score;
    g.input(IN_SHAKE, 0, 0);
    EQ(g.score, s0);                                         // nothing left in view to pop
    run(g, 600);                                             // they rise back into view
    g.input(IN_SHAKE, 0, 0);
    EQ(g.score, s0 + 1);
}

TEST(hatching_in_the_dark_turns_the_screen_on) {
    Game g; g.init(12);
    run(g, SCREEN_OFF_MS + 1000);
    CHECK(!g.screen_on);
    run(g, EGG_HATCH_MS);
    CHECK(g.s.hatched);
    CHECK(g.screen_on || g.scene == SC_HOME);
    Game h; h.init(12);
    run(h, SCREEN_OFF_MS + 1000);
    while (!h.s.hatched) h.tick(100);
    CHECK(h.screen_on);                                      // lit up for the hatch itself
    EQ(h.scene, SC_HATCH);
}

TEST(clock_wrap_keeps_petting_and_idle_alive) {
    Game g = hatched();
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000);
    g.clock_ms = 0xFFFFFFFFu - 1000;                         // ~49.7 days of uptime
    g.idle_next = g.clock_ms + 100;
    g.set_stat(HAPPY, 5000);
    tap_pet(g);
    EQ(g.stat(HAPPY), 5000 + PET_HAPPY);
    run(g, PET_COOLDOWN_MS + 500);                           // the clock wraps in here
    CHECK(g.clock_ms < 10000);
    tap_pet(g);
    EQ(g.stat(HAPPY), 5000 + 2 * PET_HAPPY - 0);
    int acted = 0;
    for (int t = 0; t < 300; t++) {
        if (t % 100 == 0) g.input(IN_NEXT, 0, 0);
        g.tick(100);
        acted += g.idle_act != IDLE_NONE;
    }
    CHECK(acted > 0);
}

// ---- rendering --------------------------------------------------------------------------------

TEST(every_scene_renders_for_every_character) {
    for (u32 seed = 1; seed < 40; seed++) {
        Game g = hatched(seed);
        for (int item = 0; item < MENU_COUNT; item++) {
            g.set_stat(ENERGY, 5000);
            g.set_stat(CLEAN, 500);
            tap_menu(g, item);
            for (int f = 0; f < 6; f++) {
                g.tick(300);
                if (g.scene == SC_GAME) g.input(IN_SELECT, 0, 0);
                render(g, fb);
            }
            CHECK(lit_pixels(0, 0, SCREEN_W, SCREEN_H) > 5000);
            if (g.scene == SC_SLEEP) g.input(IN_SELECT, 0, 0);
            int guard = 0;
            while (g.scene != SC_HOME && guard++ < 400) { g.tick(100); render(g, fb); }
            EQ(g.scene, SC_HOME);
        }
        for (int t = 0; t < 300; t++) {    // idle wandering, both facings, near the edges
            g.tick(100);
            if (t % 100 == 0) g.input(IN_NEXT, 0, 0);
            render(g, fb);
        }
    }
}

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
#define T(n) {#n, n}
        T(egg_hatches_after_a_minute), T(egg_taps_wobble_crack_and_hatch), T(hatching_cannot_be_skipped),
        T(needs_decay_per_minute), T(one_big_tick_equals_many_small_ones),
        T(big_tick_across_waking_up_matches_small_ticks), T(egg_time_after_hatching_counts_as_life),
        T(never_dies_after_a_week_of_neglect),
        T(event_feed_with_rotating_treats), T(event_play_minigame_by_touch), T(minigame_with_buttons_and_shake),
        T(minigame_zero_stars_still_rewarded_and_bonus_capped), T(minigame_stars_keep_coming),
        T(event_play_refused_when_tired), T(event_sleep_until_rested), T(event_sleep_woken_early),
        T(event_wash_dissolves_messes), T(messes_follow_cleanliness),
        T(event_pet_hearts_and_rationed_happiness), T(event_info_from_the_header),
        T(idle_life_blinks_walks_and_hit_box_follows), T(needy_unicorn_stays_put),
        T(cosmetics_pause_while_the_screen_is_dark),
        T(screen_turns_off_and_first_touch_only_wakes), T(menu_gutter_touches_are_ignored),
        T(save_load_roundtrip_and_corruption), T(power_off_freezes_time), T(autosave_every_five_minutes),
        T(moods_follow_needs), T(sound_cue_for_every_event), T(sound_toggle_on_info_screen), T(event_shake),
        T(melodies_are_short_and_audible), T(any_tick_partition_gives_the_same_world),
        T(big_tick_does_not_strand_a_hatching_egg), T(only_visible_stars_can_be_popped),
        T(hatching_in_the_dark_turns_the_screen_on), T(clock_wrap_keeps_petting_and_idle_alive),
        T(every_scene_renders_for_every_character),
    };
    for (auto& t : tests) {
        int before = failures;
        t.fn();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
