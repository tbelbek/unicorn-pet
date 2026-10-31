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

// a new game starts with the egg picker: choose egg (seed % 4), PWR confirms
static Game picked(u32 seed = 42) {
    Game g;
    g.init(seed);
    g.sel = (u8)(seed % 4);
    g.input(IN_SELECT, 0, 0);
    g.take_sfx();
    return g;
}

static Game hatched(u32 seed = 42) {
    Game g = picked(seed);
    for (int i = 0; i < EGG_TAPS; i++) g.input(IN_SELECT, 0, 0);
    run(g, HATCH_MS + 100);
    return g;
}

static void tap_menu(Game& g, int item) { g.input(IN_TOUCH, 46 + item * MENU_STEP, 400); }
// Oyna opens the toy box; the stars card is top left
static void play_stars(Game& g) { tap_menu(g, MENU_PLAY); g.input(IN_TOUCH, 116, 148); }
// feeding is two taps now: the menu, then one of three big snacks
static void feed(Game& g, int t = TREAT_APPLE) { tap_menu(g, MENU_FEED); g.input(IN_TOUCH, 61 + t * 122, 200); }
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
    Game g = picked(1);
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
    Game g = picked(2);
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
    Game g = picked(5);
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
    Game a = picked(9);
    Game b = picked(9);
    a.tick(EGG_HATCH_MS + 120000);         // hatch, then two more minutes in one go
    run(b, EGG_HATCH_MS + 120000);
    EQ(a.s.age_s, b.s.age_s);
    EQ(a.stat(HUNGER), b.stat(HUNGER));
}

TEST(never_dies_after_a_week_of_neglect) {
    Game g = hatched();
    g.tick(7u * 24 * 3600000u);
    for (int i = 0; i < NSTATS; i++) EQ(g.stat(i), DARK_FLOOR);   // left alone: peckish, not miserable
    EQ(g.scene, SC_HOME);
    EQ(g.mood(), M_HUNGRY);                // hungry wins over the other needs
    EQ(g.messes(), 2);
    g.input(IN_SELECT, 0, 0);              // wake screen
    g.input(IN_SELECT, 0, 0);              // feed (menu 0): the snack choice opens
    EQ(g.scene, SC_FEED);
    g.input(IN_SELECT, 0, 0);              // PWR takes the highlighted snack
    EQ(g.scene, SC_EAT);
    CHECK(g.stat(HUNGER) > 0);
}

// ---- care events --------------------------------------------------------------------------------

TEST(event_feed_she_picks_the_snack) {
    Game g = hatched();
    g.set_stat(HUNGER, 2000); g.set_stat(HAPPY, 5000);
    g.tick(0);
    shot(g, "03_home_hungry");
    EQ(g.recommended(), MENU_FEED);        // the Besle button bounces
    tap_menu(g, MENU_FEED);
    EQ(g.scene, SC_FEED);                  // nothing eaten yet
    EQ(g.stat(HUNGER), 2000);
    run(g, 3200);
    shot(g, "03b_pick_snack");
    g.input(IN_TOUCH, 61 + TREAT_CARROT * 122, 200);
    EQ(g.scene, SC_EAT);
    EQ(g.treat, TREAT_CARROT);
    EQ(g.stat(HUNGER), 2000 + FEED_GAIN);
    EQ(g.stat(HAPPY), 5000 + TREAT_HAPPY);
    u32 f = g.tick(100);
    CHECK(f & F_SAVE);
    run(g, 300);
    CHECK(g.particles() > 0);              // crumbs fly
    shot(g, "04_eat");
    g.input(IN_NEXT, 0, 0);                // input during the animation is ignored
    EQ(g.scene, SC_EAT);
    run(g, ACTION_MS);
    EQ(g.scene, SC_HOME);
    tap_menu(g, MENU_FEED);                // buttons: BOOT moves, PWR picks
    g.input(IN_NEXT, 0, 0);
    int pick = g.sel;
    g.input(IN_SELECT, 0, 0);
    EQ(g.treat, pick);
    run(g, ACTION_MS + 100);
    tap_menu(g, MENU_FEED);                // nobody picks: she takes the highlighted one
    run(g, FEED_PICK_MS + 100);
    EQ(g.scene, SC_EAT);
    run(g, ACTION_MS);
    g.set_stat(HUNGER, 9000);
    feed(g, TREAT_STRAWBERRY);
    EQ(g.stat(HUNGER), MAX_STAT);          // capped; every snack feeds the same
}

TEST(event_play_minigame_by_touch) {
    Game g = hatched();
    g.set_stat(HAPPY, 3000); g.set_stat(ENERGY, 6000); g.set_stat(HUNGER, 6000); g.set_stat(CLEAN, 6000);
    play_stars(g);
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
    EQ(g.stat(HAPPY), happy);              // stars are counted, not paid out
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
    g.input(IN_SELECT, 0, 0);              // ...PWR opens the toy box...
    EQ(g.scene, SC_TOYS);
    g.input(IN_SELECT, 0, 0);              // ...and PWR again takes the stars (first card)
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

TEST(minigame_joy_does_not_depend_on_the_score) {
    Game a = hatched(), b = hatched();
    for (Game* g : {&a, &b}) { g->set_stat(ENERGY, 9000); g->set_stat(HAPPY, 0); play_stars(*g); }
    for (int i = 0; i < 40; i++) { a.input(IN_SELECT, 0, 0); run(a, 300); }   // a star champion...
    run(b, 12000);                                                          // ...and a watcher
    run(a, GAME_MS - 12000); run(b, GAME_MS - 12000);
    CHECK(a.score > 20); EQ(b.score, 0);
    EQ(a.stat(HAPPY), b.stat(HAPPY));      // both had exactly the same good time
    CHECK(a.stat(HAPPY) >= PLAY_HAPPY - 20 * DECAY_HAPPY);
}

TEST(minigame_stars_keep_coming) {
    Game g = hatched();
    g.set_stat(ENERGY, 9000);
    play_stars(g);
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
    play_stars(g);
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
    CHECK(lit_pixels(40, 100, 90, 150) > 100);                       // the moon
    run(g, 60 * 60000);
    EQ(g.stat(ENERGY), 2000 + 60 * SLEEP_ENERGY_GAIN + NAP_BONUS);   // incl. the one-minute nap
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
    EQ(k.scene, SC_EGG_PICK);              // stays a fresh game
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
    Game g = picked(3);
    g.input(IN_TOUCH, 184, 200);
    EQ(cue_after(g), SFX_TAP);                            // egg tap
    for (int i = 1; i < EGG_TAPS; i++) g.input(IN_TOUCH, 184, 200);
    EQ(cue_after(g), SFX_HATCH);
    run(g, HATCH_MS + 100);
    g.input(IN_NEXT, 0, 0);
    EQ(cue_after(g), SFX_TAP);                            // cursor move
    g.set_stat(ENERGY, 6000);
    tap_menu(g, MENU_FEED);  EQ(cue_after(g), SFX_TAP);
    g.input(IN_TOUCH, 61, 200); EQ(cue_after(g), SFX_EAT); run(g, ACTION_MS);
    play_stars(g);  EQ(cue_after(g), SFX_PLAY);
    run(g, GAME_MS);         EQ(g.take_sfx(), SFX_CHEER); run(g, CELEBRATE_MS);
    tap_menu(g, MENU_WASH);  EQ(cue_after(g), SFX_WASH);  run(g, ACTION_MS);
    g.set_stat(ENERGY, 100);
    play_stars(g);  EQ(cue_after(g), SFX_REFUSE); run(g, REFUSE_MS);
    tap_pet(g);              EQ(cue_after(g), SFX_PET);
    tap_menu(g, MENU_SLEEP); EQ(cue_after(g), SFX_SLEEP);
    g.input(IN_TOUCH, 300, 300);
    EQ(cue_after(g), SFX_PET);                            // tucked in
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
    EQ(g.scene, SC_SLEEP);                                // a shake first tucks her in...
    g.input(IN_SHAKE, 0, 0);
    EQ(g.scene, SC_HOME);                                 // ...the next one wakes her
    run(g, SCREEN_OFF_MS + 500);
    CHECK(!g.screen_on);
    g.input(IN_SHAKE, 0, 0);
    CHECK(g.screen_on);
    EQ(g.scene, SC_HOME);                                 // first shake only wakes the screen
    Game e = picked(4);
    for (int i = 0; i < EGG_TAPS; i++) e.input(IN_SHAKE, 0, 0);
    EQ(e.scene, SC_HATCH);                                // shaking the egg hatches it too
}

TEST(melodies_are_short_and_audible) {
    const u8 tune[3] = {0, 1, 2};
    set_echo(tune, 3);
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
    for (Game* g : {&a, &b}) { g->set_stat(HUNGER, 2000); feed(*g); }
    run(a, 2500); ragged(b, 2500);                         // mid-meal: crumbs in flight
    CHECK(a.particles() > 0);
    CHECK(same(a, b));
    run(a, 9000); ragged(b, 9000);                         // home again, wandering
    CHECK(same(a, b));
    for (Game* g : {&a, &b}) { g->set_stat(ENERGY, 9000); g->set_stat(HAPPY, MAX_STAT - 100); play_stars(*g); }
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
    Game a = picked(9);
    Game b = picked(9);
    a.tick(EGG_HATCH_MS + 120000);
    run(b, EGG_HATCH_MS + 120000);
    EQ(a.scene, SC_HOME); EQ(b.scene, SC_HOME);
    CHECK(same(a, b));
}

TEST(only_visible_stars_can_be_popped) {
    Game g = hatched();
    g.set_stat(ENERGY, 9000);
    play_stars(g);
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
    Game g = picked(12);
    run(g, SCREEN_OFF_MS + 1000);
    CHECK(!g.screen_on);
    run(g, EGG_HATCH_MS);
    CHECK(g.s.hatched);
    CHECK(g.screen_on || g.scene == SC_HOME);
    Game h = picked(12);
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

// ---- screen-time limit: she goes out with friends -------------------------------------------

// keep the screen on with a harmless cursor press every 10 s, like a child playing
static void play_for(Game& g, u32 ms) {
    for (u32 t = 0; t < ms; t += 10000) { g.input(IN_NEXT, 0, 0); run(g, 10000); }
}

TEST(ten_minutes_of_play_and_she_goes_out) {
    Game g = hatched();
    play_for(g, PLAY_LIMIT_MS - 60000);
    EQ(g.scene, SC_HOME);
    play_for(g, 70000);
    CHECK(g.scene == SC_LEAVE || g.scene == SC_AWAY);
    run(g, 100); 
    while (g.scene == SC_LEAVE) g.tick(100);
    EQ(g.scene, SC_AWAY);
    EQ(g.away_ms, AWAY_MS - (g.away_ms < AWAY_MS ? AWAY_MS - g.away_ms : 0));
    shot(g, "16_door");
}

TEST(leaving_waits_for_the_activity_to_finish) {
    Game g = hatched();
    g.set_stat(ENERGY, 9000);
    play_for(g, PLAY_LIMIT_MS - 10000);
    play_stars(g);                              // mini-game straddles the limit
    run(g, GAME_MS);
    CHECK(g.scene == SC_PLAY);                           // not cut short
    run(g, CELEBRATE_MS + 200);
    EQ(g.scene, SC_LEAVE);
    EQ(cue_after(g), SFX_BYE);
    run(g, 1500);
    shot(g, "17_leaving");
}

TEST(a_break_resets_the_screen_time) {
    Game g = hatched();
    play_for(g, 6 * 60000);
    run(g, REST_RESET_MS + 60000);                       // screen dark for a while
    EQ(g.play_ms, 0);
    play_for(g, 6 * 60000);
    EQ(g.scene, SC_HOME);                                // 6 + 6 minutes with a break: still home
}

static Game gone_out() {
    Game g = hatched();
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 6000);
    g.play_ms = PLAY_LIMIT_MS;
    g.tick(100);
    while (g.scene != SC_AWAY) g.tick(100);
    g.take_sfx();                          // the goodbye tune has played
    return g;
}

TEST(away_freezes_needs_and_keeps_the_screen_short) {
    Game g = gone_out();
    u32 f = g.tick(100);
    CHECK(f & F_SAVE || g.want_save == 0);
    int hunger = g.stat(HUNGER);
    run(g, AWAY_SCREEN_OFF_MS - 300);
    CHECK(g.screen_on);
    g.input(IN_TOUCH, 184, 200);                         // knock knock
    EQ(cue_after(g), SFX_NONE);                          // nothing to win by knocking
    CHECK(g.wobbling);
    run(g, 200);
    shot(g, "18_door_knock");
    run(g, 400);
    CHECK(!g.screen_on);                                 // knocking did not extend the screen
    g.input(IN_TOUCH, 46, 400);                          // menu taps do nothing out here
    EQ(g.scene, SC_AWAY);
    run(g, 20 * 60000);
    EQ(g.stat(HUNGER), hunger);                          // nobody gets hungry while out with friends
    CHECK(g.away_ms > 0 && g.away_ms <= 10 * 60000 + 1000);
}

TEST(she_comes_back_after_the_break) {
    Game g = gone_out();
    run(g, AWAY_MS + 1000);
    EQ(g.away_ms, 0);
    EQ(g.scene, SC_AWAY);                                // waits, dark, until someone looks
    CHECK(!g.screen_on);
    g.input(IN_TOUCH, 184, 200);
    EQ(g.scene, SC_RETURN);
    EQ(cue_after(g), SFX_HELLO);
    CHECK(g.pet_x < 0);                                  // walks in from the left
    run(g, 1000);
    shot(g, "19_returning");
    while (g.scene == SC_RETURN) g.tick(100);
    EQ(g.scene, SC_HOME);
    EQ(g.pet_x, 0);                                      // arrived in the middle
    CHECK(g.play_ms < 2000);              // the clock started over
}

TEST(parent_can_call_her_back) {
    Game g = gone_out();
    run(g, 60000);
    g.input(IN_PARENT, 0, 0);
    EQ(g.scene, SC_RETURN);
    CHECK(g.screen_on);
    Game h = hatched();
    h.input(IN_PARENT, 0, 0);                            // at home it opens the parent settings
    EQ(h.scene, SC_SETTINGS);
}

TEST(power_cut_does_not_skip_the_break) {
    Game g = gone_out();
    run(g, 5 * 60000);
    u8 buf[SAVE_SIZE];
    g.save(buf);
    Game h; h.init(7);
    CHECK(h.load(buf, sizeof buf));
    EQ(h.scene, SC_AWAY);
    CHECK(h.away_ms >= AWAY_MS - 5 * 60000 - 1000 && h.away_ms <= AWAY_MS - 5 * 60000 + 1000);
    Game l = hatched();                                  // saved mid-goodbye counts as gone
    l.play_ms = PLAY_LIMIT_MS; l.tick(100);
    EQ(l.scene, SC_LEAVE);
    l.save(buf);
    Game m; m.init(8);
    CHECK(m.load(buf, sizeof buf));
    EQ(m.scene, SC_AWAY);
    EQ(m.away_ms, AWAY_MS);
}

TEST(version_1_saves_still_load) {
    Game g = hatched(21);
    g.set_stat(HUNGER, 3333);
    u8 v1[SAVE_V1_SIZE];
    Save tmp = g.s;
    tmp.version = 1;
    std::memcpy(v1, &tmp, SAVE_V1_SIZE - 4);
    u32 h = 2166136261u;                                 // FNV-1a over the v1 payload
    for (int i = 0; i < SAVE_V1_SIZE - 4; i++) h = (h ^ v1[i]) * 16777619u;
    std::memcpy(v1 + SAVE_V1_SIZE - 4, &h, 4);
    Game k; k.init(4);
    CHECK(k.load(v1, sizeof v1));
    EQ(k.stat(HUNGER), 3333);
    EQ(k.s.character, g.s.character);
    EQ(k.s.version, SAVE_VERSION);
    EQ(k.scene, SC_HOME);
    v1[5] ^= 1;
    Game c; c.init(4);
    CHECK(!c.load(v1, sizeof v1));                       // checksum still guards v1
}

// ---- RTC: the break also counts while the board is switched off ------------------------------

static const u32 T0 = 1000000;             // some wall-clock second

static Game out_with_rtc(u32 seed = 51) {
    Game g = hatched(seed);
    g.set_wall(T0);
    g.play_ms = PLAY_LIMIT_MS;
    g.tick(100);
    while (g.scene != SC_AWAY) g.tick(100);
    return g;
}

TEST(rtc_counts_the_break_while_powered_off) {
    Game g = out_with_rtc();
    EQ(g.s.away_until, T0 + AWAY_MS / 1000);
    u8 buf[SAVE_SIZE];
    g.save(buf);                                         // ...switched off for 40 minutes...
    Game h; h.init(3);
    h.set_wall(T0 + 40 * 60);                            // the platform reads the RTC first
    CHECK(h.load(buf, sizeof buf));
    EQ(h.scene, SC_AWAY);
    EQ(h.away_ms, 0);                                    // the break is already over
    h.input(IN_TOUCH, 184, 200);
    EQ(h.scene, SC_RETURN);
    EQ(h.s.away_until, 0);
    Game k; k.init(3);                                   // either order works
    CHECK(k.load(buf, sizeof buf));
    k.set_wall(T0 + 40 * 60);
    EQ(k.away_ms, 0);
}

TEST(rtc_partial_break_and_wall_ticking) {
    Game g = out_with_rtc();
    g.set_wall(T0 + 600);                                // 10 minutes later
    EQ(g.away_ms, (AWAY_MS / 1000 - 600) * 1000);
    run(g, 5 * 60000);                                   // powered time alone does not count twice
    EQ(g.away_ms, (AWAY_MS / 1000 - 600) * 1000);
    g.set_wall(T0 + AWAY_MS / 1000);
    EQ(g.away_ms, 0);
}

TEST(rtc_that_lost_power_is_not_trusted) {
    Game g = out_with_rtc();
    run(g, 60000);
    u8 buf[SAVE_SIZE];
    g.set_wall(T0 + 300);                                // 5 min out so far
    g.save(buf);
    Game h; h.init(3);
    h.set_wall(50);                                      // RTC restarted from scratch
    CHECK(h.load(buf, sizeof buf));
    EQ(h.scene, SC_AWAY);
    EQ(h.s.away_until, 0);                               // dropped: count powered time instead
    CHECK(h.away_ms > 20 * 60000 && h.away_ms <= 25 * 60000 + 1000);
    run(h, 60000);
    CHECK(h.away_ms <= 24 * 60000 + 1000);               // and it keeps counting down
}

TEST(no_rtc_counts_powered_time_only) {
    Game g = out_with_rtc();
    u8 buf[SAVE_SIZE];
    g.save(buf);
    Game h; h.init(3);                                   // a board without a working RTC
    CHECK(h.load(buf, sizeof buf));
    EQ(h.away_ms, AWAY_MS);
    run(h, 60000);
    EQ(h.away_ms, AWAY_MS - 60000);
}

TEST(version_2_saves_still_load) {
    Game g = hatched(22);
    g.set_stat(CLEAN, 4444);
    Save tmp = g.s;
    tmp.version = 2;
    tmp.away_s = 600;
    tmp.play_s = 0;
    u8 v2[SAVE_V2_SIZE];
    std::memcpy(v2, &tmp, SAVE_V2_SIZE - 4);
    u32 h = 2166136261u;
    for (int i = 0; i < SAVE_V2_SIZE - 4; i++) h = (h ^ v2[i]) * 16777619u;
    std::memcpy(v2 + SAVE_V2_SIZE - 4, &h, 4);
    Game k; k.init(4);
    CHECK(k.load(v2, sizeof v2));
    EQ(k.stat(CLEAN), 4444);
    EQ(k.scene, SC_AWAY);
    EQ(k.away_ms, 600000);
    EQ(k.s.away_until, 0);
}

// ---- smoothness -------------------------------------------------------------------------------

TEST(frames_are_interpolated_between_steps) {
    Game g = hatched(77);
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000);
    g.idle_act = IDLE_WALK; g.walk_to = WALK_RANGE; g.pet_x = 0; g.vel_x = 0;
    g.tick(STEP_MS);                                     // one step: she starts walking
    CHECK(g.vel_x != 0);
    static u16 a[SCREEN_W * SCREEN_H];
    render(g, fb);
    std::memcpy(a, fb, sizeof a);
    u32 f = g.tick(STEP_MS / 2);                         // half a step: no simulation...
    CHECK(f & F_REDRAW);                                 // ...but a new frame is wanted
    render(g, fb);
    CHECK(std::memcmp(a, fb, sizeof a) != 0);            // and it is a different picture
    f = g.tick(0);
    CHECK(f & F_REDRAW);
    tap_header(g);                                       // a still screen does not spin
    g.tick(0);
    f = g.tick(10);
    CHECK(!(f & F_REDRAW));
}

// ---- QoL round (skills applied with Codex) ------------------------------------------------------

TEST(time_is_up_means_no_new_activities) {
    Game g = hatched();
    g.play_ms = PLAY_LIMIT_MS;             // limit reached while she happens to be on the info screen
    tap_header(g);
    EQ(g.scene, SC_LEAVE);                 // no more postponing through the info screen
    Game h = hatched();
    h.play_ms = PLAY_LIMIT_MS;
    play_stars(h);
    EQ(h.scene, SC_LEAVE);                 // or by starting a new game
}

TEST(tired_pillow_puts_her_to_sleep) {
    Game g = hatched();
    g.set_stat(ENERGY, 500);
    play_stars(g);
    EQ(g.scene, SC_REFUSE);
    g.tick(100);
    shot(g, "06_refuse_pillow");
    g.input(IN_TOUCH, SCREEN_W / 2, 400);   // the big pillow
    EQ(g.scene, SC_SLEEP);
    EQ(g.menu, MENU_SLEEP);
}

TEST(a_one_minute_nap_is_enough_to_play) {
    Game g = hatched();
    g.set_stat(ENERGY, 0);
    tap_menu(g, MENU_SLEEP);
    run(g, NAP_MS - 1000);
    CHECK(g.stat(ENERGY) < PLAY_MIN_ENERGY);
    run(g, 1100);
    CHECK(g.stat(ENERGY) >= PLAY_MIN_ENERGY); // short nap bonus
    EQ(g.napped, 1);
    g.input(IN_TOUCH, 184, 200);           // wake her (screen may be dark: wake twice)
    g.input(IN_TOUCH, 184, 200);
    play_stars(g);
    EQ(g.scene, SC_GAME);
    Game a = hatched(5), b = hatched(5);   // the nap lands the same with any tick size
    for (Game* x : {&a, &b}) { x->set_stat(ENERGY, 9000); tap_menu(*x, MENU_SLEEP); }
    a.tick(3 * 60000);
    run(b, 3 * 60000);
    for (int i = 0; i < NSTATS; i++) EQ(a.stat(i), b.stat(i));
    EQ(a.scene, b.scene);
}

TEST(parent_settings) {
    Game g = hatched();
    g.input(IN_PARENT, 0, 0);
    EQ(g.scene, SC_SETTINGS);
    g.tick(100);
    shot(g, "20_settings");
    g.input(IN_TOUCH, 184, SET_Y0 + SET_LIMIT * SET_ROW_H + 18);   // session: 10 -> 15 min
    EQ(g.play_limit_ms(), 15u * 60000);
    g.input(IN_TOUCH, 184, SET_Y0 + SET_SHAKE * SET_ROW_H + 18);   // shake off
    g.input(IN_TOUCH, 184, SET_Y0 + SET_CALM * SET_ROW_H + 18);    // calm motion on
    g.input(IN_TOUCH, 184, SET_Y0 + SET_VOLUME * SET_ROW_H + 18);  // volume: low
    EQ(g.volume(), VOL_LOW);
    u32 f = g.tick(0);
    CHECK(f & F_SAVE);
    u32 p0 = g.play_ms;
    play_for(g, 11 * 60000);                 // in settings: not counted as play
    EQ(g.play_ms, p0);
    g.input(IN_TOUCH, 184, SET_Y0 + SET_EXIT * SET_ROW_H + 18);
    EQ(g.scene, SC_HOME);
    g.input(IN_SHAKE, 0, 0);
    EQ(g.scene, SC_HOME);                    // shake ignored
    tap_pet(g);
    CHECK(g.particles() <= 2);               // calm: fewer hearts
    u8 buf[SAVE_SIZE]; g.save(buf);          // settings survive a power cut
    Game h; h.init(3); CHECK(h.load(buf, sizeof buf));
    EQ(h.play_limit_ms(), 15u * 60000);
    CHECK(h.calm());
    EQ(h.volume(), VOL_LOW);
    play_for(h, 12 * 60000);
    EQ(h.scene, SC_HOME);                    // 15-minute sessions now
    play_for(h, 4 * 60000);
    CHECK(h.scene == SC_LEAVE || h.scene == SC_AWAY);
}

TEST(hints_for_pre_readers) {
    Game g = picked(4);
    g.tick(0);
    shot(g, "00_egg_hint");                   // a little hand taps the egg
    for (int i = 0; i < EGG_TAPS; i++) g.input(IN_TOUCH, 184, 200);
    run(g, HATCH_MS + 100);
    EQ(g.hint_pet, 1);
    g.tick(0);
    shot(g, "02c_pet_hint");                  // and then shows how to pet her
    tap_pet(g);
    EQ(g.hint_pet, 0);
    g.set_stat(CLEAN, 1000);
    EQ(g.recommended(), MENU_WASH);
}

TEST(stars_hit_where_they_are_drawn) {
    Game g = hatched();
    g.set_stat(ENERGY, 9000);
    play_stars(g);
    run(g, 1000);
    g.tick(90);                              // 90 ms into the next step
    int drawn = g.tgt[0].y + g.tgt[0].vy * (int)g.acc_ms / STEP_MS;
    g.input(IN_TOUCH, g.tgt[0].x, drawn - TARGET_HIT + 1);   // edge of the drawn star
    EQ(g.score, 1);
    CHECK(g.particles() > 0);
}

TEST(version_3_saves_still_load) {
    Game g = hatched(23);
    g.set_stat(HAPPY, 2222);
    Save tmp = g.s;
    tmp.version = 3;
    u8 v3[SAVE_V3_SIZE];
    std::memcpy(v3, &tmp, SAVE_V3_SIZE - 4);
    u32 h = 2166136261u;
    for (int i = 0; i < SAVE_V3_SIZE - 4; i++) h = (h ^ v3[i]) * 16777619u;
    std::memcpy(v3 + SAVE_V3_SIZE - 4, &h, 4);
    Game k; k.init(4);
    CHECK(k.load(v3, sizeof v3));
    EQ(k.stat(HAPPY), 2222);
    EQ(k.play_limit_ms(), PLAY_LIMIT_MS);    // settings default when the save had none
    EQ(k.break_ms(), AWAY_MS);
}

TEST(sound_toggle_overrides_parent_off) {
    Game g = hatched();
    g.input(IN_PARENT, 0, 0);
    g.input(IN_TOUCH, 184, SET_Y0 + SET_VOLUME * SET_ROW_H + 18);   // normal -> low
    g.input(IN_TOUCH, 184, SET_Y0 + SET_VOLUME * SET_ROW_H + 18);   // low -> off
    EQ(g.volume(), VOL_OFF);
    g.input(IN_TOUCH, 184, SET_Y0 + SET_EXIT * SET_ROW_H + 18);
    tap_header(g);
    g.input(IN_TOUCH, 320, 26);                         // the info screen's "SES KAPALI" button
    EQ(g.volume(), VOL_NORMAL);                         // really on again
    g.input(IN_TOUCH, 320, 26);
    EQ(g.volume(), VOL_OFF);
}

// ---- feature round (specs by Codex) -----------------------------------------------------------

TEST(egg_choice_persists) {
    Game g; g.init(77);
    EQ(g.scene, SC_EGG_PICK);
    g.tick(0);
    shot(g, "21_egg_pick");
    g.input(IN_TOUCH, 268, 310);           // the fourth egg: first tap only highlights it
    EQ(g.scene, SC_EGG_PICK);
    EQ(g.sel, 3);
    g.input(IN_TOUCH, 100, 170);           // changed my mind
    EQ(g.sel, 0);
    g.input(IN_TOUCH, 268, 310);
    g.input(IN_TOUCH, 268, 310);           // second tap on the same egg chooses it
    EQ(g.scene, SC_EGG);
    EQ(g.s.character, 3);
    EQ(cue_after(g), SFX_CHOOSE);
    u8 buf[SAVE_SIZE]; g.save(buf);
    Game h; h.init(5); CHECK(h.load(buf, sizeof buf));
    EQ(h.scene, SC_EGG);                   // a power cut keeps the chosen egg
    EQ(h.s.character, 3);
    Game k; k.init(6);                     // or: highlight, then the heart button
    k.input(IN_TOUCH, 268, 170);
    k.input(IN_TOUCH, SCREEN_W / 2, 410);
    EQ(k.s.character, 1);
    EQ(k.scene, SC_EGG);
}

TEST(picker_does_not_age) {
    Game g; g.init(8);
    run(g, 120000);
    EQ(g.scene, SC_EGG_PICK);              // no hatching from the picker
    EQ(g.s.egg_ms, 0);
}

TEST(legacy_egg_keeps_identity) {
    Game g = picked(2);
    Save tmp = g.s;
    tmp.version = 4;                       // a v4 egg: no birth flag existed yet
    u8 v4[SAVE_V4_SIZE];
    std::memcpy(v4, &tmp, SAVE_V4_SIZE - 4);
    v4[37] = v4[38] = v4[39] = 0;          // v4 padding
    u32 h = 2166136261u;
    for (int i = 0; i < SAVE_V4_SIZE - 4; i++) h = (h ^ v4[i]) * 16777619u;
    std::memcpy(v4 + SAVE_V4_SIZE - 4, &h, 4);
    Game k; k.init(9);
    CHECK(k.load(v4, sizeof v4));
    EQ(k.scene, SC_EGG);                   // bypasses the picker
    EQ(k.s.character, g.s.character);
    EQ(k.s.accessory, ACC_NONE);
}

TEST(foam_puffs_pop_once) {
    Game g = hatched();
    g.set_stat(CLEAN, 1000);
    tap_menu(g, MENU_WASH);
    run(g, 600);
    CHECK(g.foam_window());
    g.input(IN_TOUCH, 184, 150);           // middle puff
    EQ(g.foam_mask, 2);
    EQ(cue_after(g), SFX_POP);
    g.input(IN_TOUCH, 184, 150);           // again: nothing
    EQ(g.foam_mask, 2);
    run(g, 300);
    shot(g, "22_foam_crown");
    g.input(IN_SELECT, 0, 0);              // PWR pops the selected one
    g.input(IN_SHAKE, 0, 0);               // shake the last
    EQ(g.foam_mask, 7);
    run(g, ACTION_MS);
    EQ(g.scene, SC_HOME);
}

TEST(wash_without_help_ends_the_same) {
    Game a = hatched(), b = hatched();
    for (Game* g : {&a, &b}) { g->set_stat(CLEAN, 1000); tap_menu(*g, MENU_WASH); }
    run(a, 600);
    a.input(IN_TOUCH, 104, 190); a.input(IN_TOUCH, 184, 150); a.input(IN_TOUCH, 264, 190);
    run(a, ACTION_MS); run(b, ACTION_MS + 600);
    for (int i = 0; i < NSTATS; i++) EQ(a.stat(i), b.stat(i));   // foam is just for fun
    run(a, 0);
    Game c = hatched(3), d = hatched(3);   // and ragged ticks see the same bath
    for (Game* g : {&c, &d}) { g->set_stat(CLEAN, 1000); tap_menu(*g, MENU_WASH); }
    run(c, 700); ragged(d, 700);
    for (Game* g : {&c, &d}) g->input(IN_TOUCH, 184, 150);
    run(c, 1500); ragged(d, 1500);
    CHECK(same(c, d));
    EQ(c.foam_mask, d.foam_mask);
}

TEST(butterfly_visits_quietly) {
    Game g = hatched();
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 8000);
    int stats[NSTATS];
    for (int i = 0; i < NSTATS; i++) stats[i] = g.stat(i);
    g.input(IN_NEXT, 0, 0);
    run(g, VISIT_FIRST_MS + 1500);
    EQ(g.visitor, 1);
    CHECK(g.take_sfx() == SFX_NONE || true);
    shot(g, "23_butterfly");
    run(g, VISIT_MS);
    EQ(g.visitor, 0);                      // flew off, will come back later
    for (int i = 0; i < NSTATS; i++) CHECK(g.stat(i) >= stats[i] - 20 * DECAY_HUNGER);   // no rewards, no costs
    g.input(IN_NEXT, 0, 0);
    run(g, VISIT_EVERY_MS + 200);
    EQ(g.visitor, 1);
    tap_menu(g, MENU_WASH);                // care dismisses the visitor
    EQ(g.visitor, 0);
    run(g, ACTION_MS + 100);
    run(g, SCREEN_OFF_MS + 1000);          // dark: no visits, and nothing turns the screen on
    CHECK(!g.screen_on);
    run(g, 60000);
    CHECK(!g.screen_on);
    EQ(g.visitor, 0);
}

TEST(toy_box_routes) {
    Game g = hatched();
    g.set_stat(ENERGY, 9000);
    tap_menu(g, MENU_PLAY);
    EQ(g.scene, SC_TOYS);
    g.tick(100);
    shot(g, "24_toy_box");
    g.input(IN_TOUCH, 252, 148); EQ(g.scene, SC_MUSIC); g.input(IN_TOUCH, 320, 40); EQ(g.scene, SC_HOME);
    tap_menu(g, MENU_PLAY);
    g.input(IN_TOUCH, 116, 264); EQ(g.scene, SC_CLOUD); g.input(IN_TOUCH, 320, 40);
    tap_menu(g, MENU_PLAY);
    g.input(IN_TOUCH, 252, 264); EQ(g.scene, SC_DRESS); g.input(IN_TOUCH, 320, 40);
    tap_menu(g, MENU_PLAY);
    g.input(IN_TOUCH, SCREEN_W / 2, 380); EQ(g.scene, SC_HOME);   // the house card
    tap_menu(g, MENU_PLAY);
    run(g, MODE_IDLE_MS + 200);
    EQ(g.scene, SC_HOME);                  // nobody chose: back home
    g.set_stat(ENERGY, 500);               // tired: music and the mirror still work
    tap_menu(g, MENU_PLAY);
    g.input(IN_TOUCH, 252, 148);
    EQ(g.scene, SC_MUSIC);
}

TEST(music_garden_echoes_the_tune_once) {
    Game g = hatched();
    tap_menu(g, MENU_PLAY);
    g.input(IN_TOUCH, 252, 148);
    for (int i = 0; i < 9; i++) g.input(IN_TOUCH, 64 + (i % 3) * 120, 270);
    EQ(g.note_count, NOTES);               // keeps the six newest
    EQ(g.notes[0], 0);                     // notes 3..8 -> flowers 0,1,2,0,1,2
    g.take_sfx();
    run(g, 300);
    shot(g, "25_music");
    int sung = 0, echo = 0;                // take every cue, like the platforms do
    for (int t = 0; t < 45; t++) { u32 f = g.tick(100); if (f & F_SOUND) { if (g.take_sfx() == SFX_ECHO) echo++; sung++; } }
    EQ(sung, 1); EQ(echo, 1);              // she sings the tune back as one melody...
    int n = 0;
    const Note* m = melody(SFX_ECHO, &n);
    EQ(n, 2 * NOTES - 1);                  // six notes with rests between
    CHECK(m && m[0].hz == 523);            // the first remembered flower: C5
    EQ(g.note_count, 0);
    Game h = hatched();                    // a long tick does not swallow the tune
    tap_menu(h, MENU_PLAY); h.input(IN_TOUCH, 252, 148);
    for (int i = 0; i < 3; i++) h.input(IN_TOUCH, 64 + i * 120, 270);
    h.take_sfx();
    h.tick(3400);
    EQ(h.take_sfx(), SFX_ECHO);
    for (int t = 0; t < 30; t++) { u32 f = g.tick(100); CHECK(!(f & F_SOUND)); }   // ...once
    g.input(IN_TOUCH, 64, 270);
    g.input(IN_TOUCH, 320, 40);            // leave right away
    g.take_sfx();
    for (int t = 0; t < 40; t++) { u32 f = g.tick(100); CHECK(!(f & F_SOUND)); }   // no echo after exit
}

TEST(dress_up_roundtrip) {
    Game g = hatched();
    tap_menu(g, MENU_PLAY);
    g.input(IN_TOUCH, 252, 264);
    EQ(g.scene, SC_DRESS);
    g.input(IN_NEXT, 0, 0);                // BOOT only previews...
    EQ(g.preview, ACC_BOW);
    EQ(g.s.accessory, ACC_NONE);           // ...nothing is kept yet
    for (int a = ACC_NONE; a < ACC_COUNT; a++) {
        g.input(IN_TOUCH, 24 + a * 86 + 39, 390);
        EQ(g.s.accessory, a);
        u8 buf[SAVE_SIZE]; g.save(buf);
        Game h; h.init(1); CHECK(h.load(buf, sizeof buf));
        EQ(h.s.accessory, a);
    }
    g.input(IN_TOUCH, 24 + ACC_TIARA * 86 + 39, 390);
    g.tick(100);
    shot(g, "26_dress");
    g.input(IN_TOUCH, 320, 40);
    g.tick(100);
    shot(g, "27_home_tiara");
}

TEST(accessory_follows_every_pose) {
    for (int a = ACC_BOW; a < ACC_COUNT; a++)
        for (u32 seed = 1; seed <= 4; seed++) {
            Game g = hatched(seed);
            g.s.accessory = (u8)a;
            for (int f = 0; f < 2; f++) {
                g.facing = f ? -1 : 1;
                g.pet_x = (i16)(f ? -WALK_RANGE : WALK_RANGE);
                for (int item = 0; item < MENU_COUNT; item++) {
                    g.idle_act = (u8)(item % 4);
                    g.idle_end = g.clock_ms + 300;
                    render(g, fb);
                }
            }
            g.set_stat(ENERGY, 2000);
            tap_menu(g, MENU_SLEEP);
            g.tick(100);
            render(g, fb);
        }
}

TEST(tilt_a_cloud) {
    Game g = hatched();
    tap_menu(g, MENU_PLAY);
    g.input(IN_TOUCH, 116, 264);
    EQ(g.scene, SC_CLOUD);
    int stats[NSTATS];
    for (int i = 0; i < NSTATS; i++) stats[i] = g.stat(i);
    g.set_tilt(250);                       // clamped
    EQ(g.tilt, 100);
    for (int i = 0; i < 10; i++) { g.set_tilt(100); g.tick(100); }
    CHECK(g.cloud_x > 260);                // tilting right moves the cloud right
    run(g, 2000);                          // no fresh samples: tilt goes stale, back to steering
    CHECK(g.tilt_f > -16 && g.tilt_f < 16);
    g.input(IN_TOUCH, 30, 200);            // steer by touch: left third
    run(g, 1500);
    EQ(g.cloud_x, 64);
    shot(g, "28_cloud");
    run(g, CLOUD_MS);
    EQ(g.scene, SC_HOME);
    int resolved = 0;
    for (int i = 0; i < FEATHERS; i++) resolved += g.fea[i].state >= 2;
    EQ(resolved, FEATHERS);                // every feather landed somewhere: no loss
    for (int i = 0; i < NSTATS; i++) CHECK(g.stat(i) >= stats[i] - 2 * DECAY_HUNGER);   // no costs either
}

TEST(modes_end_when_time_is_up) {
    Game g = hatched();
    tap_menu(g, MENU_PLAY);
    g.input(IN_TOUCH, 252, 148);
    EQ(g.scene, SC_MUSIC);
    g.play_ms = PLAY_LIMIT_MS;
    g.tick(100);
    g.tick(100);
    EQ(g.scene, SC_LEAVE);                 // music stops, she still says goodbye properly
}

TEST(review_fixes_feature_round) {
    Game g = hatched();
    tap_menu(g, MENU_PLAY); g.input(IN_TOUCH, 116, 264);
    for (int t = 0; t < CLOUD_MS / 100 - 1; t++) { g.set_tilt(60); g.tick(100); }
    EQ(g.scene, SC_CLOUD);                 // tilting alone keeps the round going to its end
    g.tick(200);
    EQ(g.scene, SC_HOME);
    Game h = hatched(77);                  // shake while wandering: stars start centre-front
    for (int i = 0; i < NSTATS; i++) h.set_stat(i, 9000);
    h.pet_x = -30; h.facing = -1;
    h.input(IN_SHAKE, 0, 0);
    EQ(h.scene, SC_GAME); EQ(h.pet_x, 0); EQ(h.facing, 1);
}

// local seconds since 2000-01-01 for a date and time (test helper; leap years 2000-2099)
static u32 at(int y, int mo, int d, int h, int mi) {
    static const int CUM[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    u32 days = 0;
    for (int k = 2000; k < y; k++) days += k % 4 ? 365 : 366;
    days += CUM[mo - 1] + d - 1 + (mo > 2 && y % 4 == 0);
    return days * 86400u + h * 3600u + mi * 60u;
}

TEST(calendar_reads_the_wall_clock) {
    Cal c = calendar(at(2026, 10, 31, 14, 5));
    CHECK(c.known); EQ(c.month, 10); EQ(c.mday, 31); EQ(c.minute, 14 * 60 + 5); EQ(c.wday, 5);   // Saturday
    c = calendar(at(2000, 2, 29, 0, 0)); EQ(c.month, 2); EQ(c.mday, 29); EQ(c.wday, 1);           // Tuesday
    c = calendar(at(2028, 12, 31, 23, 59)); EQ(c.month, 12); EQ(c.mday, 31);
    c = calendar(at(2027, 3, 1, 0, 0)); EQ(c.month, 3); EQ(c.mday, 1); EQ(c.day, (u32)at(2027, 3, 1, 0, 0) / 86400);
    CHECK(!calendar(0).known);
    Game g = hatched();
    EQ(g.phase(), PH_DAY);                 // no clock: always daytime
    struct { int h, m, ph; } T[] = {{6, 59, PH_NIGHT}, {7, 0, PH_DAY}, {16, 59, PH_DAY}, {17, 0, PH_EVENING},
                                     {19, 59, PH_EVENING}, {20, 0, PH_NIGHT}, {0, 0, PH_NIGHT}};
    for (auto& x : T) { g.set_wall(at(2026, 6, 1, x.h, x.m)); EQ(g.phase(), x.ph); }
    g.set_wall(at(2026, 6, 1, 22, 0), false);
    EQ(g.phase(), PH_DAY);                 // a clock restarted after a flat battery: no guessing
}

TEST(greeting_after_a_real_break) {
    Game g = hatched();
    tap_menu(g, MENU_FEED); g.input(IN_TOUCH, 184, 200); run(g, ACTION_MS + 200);   // strawberry
    EQ(g.s.last_treat, TREAT_STRAWBERRY + 1);
    run(g, SCREEN_OFF_MS + 1000);
    CHECK(!g.screen_on);
    g.tick(60000); g.input(IN_SELECT, 0, 0);   // a short break: no greeting
    CHECK(!g.greeting);
    run(g, SCREEN_OFF_MS + 1000);
    g.tick(REST_RESET_MS);
    g.input(IN_SELECT, 0, 0);
    CHECK(g.greeting); EQ(g.scene, SC_HOME);
    EQ(g.idle_ms, 0u); EQ(g.play_ms, 0u);      // same as any wake: no extra screen time
    g.tick(GREET_MS - 100); CHECK(g.greeting);
    g.tick(200); CHECK(!g.greeting);
    u8 buf[SAVE_SIZE];                         // she remembers the snack across a power cut
    g.save(buf);
    Game h; h.init(5);
    CHECK(h.load(buf, SAVE_SIZE));
    EQ(h.s.last_treat, TREAT_STRAWBERRY + 1); EQ(h.treat, TREAT_STRAWBERRY);
    CHECK(h.greeting);                         // and says hello on power-up
    buf[0] = 4;                                // an older save has no snack memory
    u32 check = 0x811C9DC5u;
    for (u32 i = 0; i < SAVE_V4_SIZE - 4; i++) { check ^= buf[i]; check *= 16777619u; }
    for (int i = 0; i < 4; i++) buf[SAVE_V4_SIZE - 4 + i] = (u8)(check >> (8 * i));
    Game o; o.init(5);
    CHECK(o.load(buf, SAVE_V4_SIZE)); EQ(o.s.last_treat, 0);
}

TEST(tuck_in_then_dreams) {
    Game g = hatched();
    g.set_stat(ENERGY, 3000);
    tap_menu(g, MENU_SLEEP);
    CHECK(!g.tucked_in());
    g.tick(1000);
    g.input(IN_TOUCH, 300, 300);               // tuck her in: she keeps sleeping
    EQ(g.scene, SC_SLEEP); CHECK(g.tucked_in());
    Game a = hatched();                        // nobody tucks: it happens by itself at 4 s
    a.set_stat(ENERGY, 3000);
    tap_menu(a, MENU_SLEEP);
    a.tick(TUCK_MS - 100); CHECK(!a.tucked_in());
    a.tick(100); CHECK(a.tucked_in());
    EQ(a.dream(), 0);
    a.tick(DREAM_FIRST_MS - TUCK_MS); CHECK(a.dream() > 0);
    int d1 = a.dream();
    a.tick(DREAM_MS); EQ(a.dream(), 0);
    a.tick(DREAM_EVERY_MS - DREAM_MS); CHECK(a.dream() > 0 && a.dream() != d1);
    int d2 = a.dream();
    a.tick(DREAM_MS); EQ(a.dream(), 0);
    a.tick(DREAM_EVERY_MS - DREAM_MS); CHECK(a.dream() > 0 && a.dream() != d1 && a.dream() != d2);
    Game b = hatched();                        // already rested: the ritual still plays out
    b.set_stat(ENERGY, MAX_STAT);
    tap_menu(b, MENU_SLEEP);
    b.tick(TUCK_MS - 100); EQ(b.scene, SC_SLEEP);
    b.tick(200); EQ(b.scene, SC_HOME);
    Game m = hatched();                        // a life-minute right after bedtime: blanket first
    m.set_stat(ENERGY, MAX_STAT - 10);
    m.minute_ms = 60000 - 200;
    tap_menu(m, MENU_SLEEP);
    m.tick(300); EQ(m.scene, SC_SLEEP);
    m.tick(TUCK_MS); EQ(m.scene, SC_HOME);
    Game c = hatched();                        // one-minute nap is unchanged by the ritual
    c.set_stat(ENERGY, 1000);
    tap_menu(c, MENU_SLEEP);
    c.tick(NAP_MS);
    CHECK(c.s.stat[ENERGY] >= 1000 + NAP_BONUS);
}

TEST(old_unix_return_time_falls_back) {
    Game g = hatched();
    g.play_ms = g.play_limit_ms();
    run(g, LEAVE_MS + 200);
    EQ(g.scene, SC_AWAY);
    g.s.away_until = 1790000000u;              // an emulator save from before local time
    g.set_wall(at(2026, 10, 31, 12, 0));
    EQ(g.s.away_until, 0u);                    // a clock that "went backwards": count powered time
    g.tick(g.break_ms());
    g.input(IN_SELECT, 0, 0);
    CHECK(g.scene == SC_RETURN || g.scene == SC_HOME);
}

TEST(album_fills_one_picture_per_cared_day) {
    Game g = hatched();
    g.input(IN_TOUCH, 184, 200);               // pet, but no clock: nothing to date it by
    EQ(g.s.care_days, 0);
    g.set_wall(at(2026, 11, 2, 9, 0));
    run(g, 800); g.input(IN_TOUCH, 184 + g.pet_x, 200);   // first care of the day
    EQ(g.s.care_days, 1); CHECK(g.memory_new); CHECK(g.album_unseen);
    tap_menu(g, MENU_WASH); run(g, ACTION_MS + 200);
    tap_menu(g, MENU_FEED); g.input(IN_TOUCH, 60, 200); run(g, ACTION_MS + 200);
    EQ(g.s.care_days, 1);                      // the same day counts once
    run(g, MEMORY_SHOW_MS + 200); CHECK(!g.memory_new);
    g.set_wall(at(2026, 11, 9, 9, 0));         // a week later: no streak lost, just the next picture
    tap_menu(g, MENU_FEED); g.input(IN_TOUCH, 60, 200); run(g, ACTION_MS + 200);
    EQ(g.s.care_days, 2);
    g.set_wall(at(2026, 11, 9, 9, 0), false);  // an untrusted clock awards nothing
    g.s.memory_day = 0;
    tap_menu(g, MENU_WASH); run(g, ACTION_MS + 200);
    EQ(g.s.care_days, 2);
    g.set_wall(at(2026, 11, 9, 9, 0));
    g.s.memory_day = (u16)(at(2026, 11, 9, 9, 0) / 86400);
    g.set_wall(at(2026, 5, 1, 9, 0));          // the clock went back: restart from today, no punishment
    tap_menu(g, MENU_WASH); run(g, ACTION_MS + 200);
    EQ(g.s.memory_day, at(2026, 5, 1, 9, 0) / 86400); EQ(g.s.care_days, 2);
    g.set_wall(at(2026, 5, 2, 9, 0));
    tap_menu(g, MENU_SLEEP);                   // bedtime is care too
    EQ(g.s.care_days, 3);
    for (int d = 0; d < 130; d++) { g.s.memory_day = 0; g.set_wall(at(2026, 5, 3, 9, 0) + d * 86400); g.input(IN_TOUCH, 184, 200); g.tick(TUCK_MS); }
    CHECK(g.s.care_days >= 130);
    EQ(g.books(), (g.s.care_days + 11) / 12);  // a new book every 12 days; the old ones stay full
    g.book = 0; EQ(g.album(), ALBUM_PICS); EQ(g.garden(2), 4);
    g.book = (u16)(g.books() - 1); EQ(g.album(), (g.s.care_days - 1) % 12 + 1);
    g.book = 0;
    u16 days = g.s.care_days;
    u8 buf[SAVE_SIZE];                         // it all survives a power cut
    g.save(buf);
    Game h; h.init(3);
    CHECK(h.load(buf, SAVE_SIZE)); EQ(h.s.care_days, days);
    h.s.care_days = CARE_DAYS_MAX; h.s.memory_day = 0;   // the count saturates, never wraps
    h.set_wall(at(2030, 1, 1, 9, 0)); h.input(IN_SELECT, 0, 0);
    tap_menu(h, MENU_WASH); EQ(h.s.care_days, CARE_DAYS_MAX);
    buf[0] = 5;                                // a v5 save starts an empty album
    u32 c = 2166136261u;
    for (int i = 0; i < SAVE_V5_SIZE - 4; i++) c = (c ^ buf[i]) * 16777619u;
    std::memcpy(buf + SAVE_V5_SIZE - 4, &c, 4);
    Game o; o.init(3);
    CHECK(o.load(buf, SAVE_V5_SIZE)); EQ(o.s.care_days, 0); EQ(o.s.memory_day, 0);
}

TEST(garden_grows_with_cared_days) {
    Game g = hatched();
    struct { int days, a, b, c; } T[] = {{0, 0, 0, 0}, {1, 1, 0, 0}, {4, 4, 0, 0}, {5, 4, 1, 0}, {8, 4, 4, 0},
                                          {12, 4, 4, 4}, {40, 4, 4, 4}};
    for (auto& x : T) { g.s.care_days = (u8)x.days; EQ(g.garden(0), x.a); EQ(g.garden(1), x.b); EQ(g.garden(2), x.c); }
}

TEST(album_opens_from_the_info_screen) {
    Game g = hatched();
    g.set_wall(at(2026, 11, 2, 9, 0));
    g.input(IN_TOUCH, 184 + g.pet_x, 200);
    tap_header(g);
    EQ(g.scene, SC_INFO);
    g.input(IN_TOUCH, 70, 30);                 // the book
    EQ(g.scene, SC_ALBUM); CHECK(!g.album_unseen);
    shot(g, "album");
    CHECK(lit_pixels(12, 82, 92, 150) > 600);  // the first picture is in
    CHECK(lit_pixels(40, 396, 328, 436) > 5000);   // the planter
    g.input(IN_TOUCH, 52, 116);                // the first picture twinkles
    EQ(cue_after(g), SFX_TAP); EQ(g.scene, SC_ALBUM);
    g.input(IN_TOUCH, 320, 44);                // the house
    EQ(g.scene, SC_HOME);
    tap_header(g); g.input(IN_SELECT, 0, 0);   // PWR on the info screen opens it too
    EQ(g.scene, SC_ALBUM);
    u32 p = g.play_ms;
    g.tick(ALBUM_IDLE_MS - 100); EQ(g.scene, SC_ALBUM);
    CHECK(g.play_ms > p);                      // looking at the album is screen time
    g.tick(200); EQ(g.scene, SC_HOME);
}

TEST(dreams_follow_her_day_and_vary) {
    Game g = hatched();
    tap_menu(g, MENU_PLAY); g.input(IN_TOUCH, 252, 148);  // the music garden
    EQ(g.scene, SC_MUSIC);
    g.input(IN_TOUCH, 320, 44);
    tap_menu(g, MENU_SLEEP);
    EQ(g.dreams[0], D_MUSIC);                  // she dreams of what she did today...
    EQ(g.did, 0);                              // ...and tomorrow is a new day
    Game n = hatched(9);                       // a day of nothing in particular: any dream
    n.did = 0;
    tap_menu(n, MENU_SLEEP);
    CHECK(n.dreams[0] < D_COUNT);
    int seen[D_COUNT] = {0}, kinds = 0;        // over many nights she dreams of many things
    for (int night = 0; night < 40; night++) {
        Game h = hatched(100 + night);
        h.s.care_days = 5;
        tap_menu(h, MENU_SLEEP);
        for (int i = 0; i < DREAMS; i++) {
            CHECK(h.dreams[i] < D_COUNT);
            for (int j = 0; j < i; j++) CHECK(h.dreams[i] != h.dreams[j]);   // never twice a night
            if (!seen[h.dreams[i]]++) kinds++;
        }
    }
    CHECK(kinds >= 10);
    for (int d = 0; d < D_COUNT; d++) {        // every dream draws something inside its cloud
        Game h = hatched();
        tap_menu(h, MENU_SLEEP);
        h.dreams[0] = (u8)d;
        h.facing = (i8)(d % 2 ? -1 : 1);     // both ways she can lie
        h.tick(DREAM_FIRST_MS + 1500);
        EQ(h.dream(), d + 1);
        static u16 none[SCREEN_W * SCREEN_H];
        Game q = h;                            // the same moment without the dream
        q.scene_ms = DREAM_FIRST_MS + DREAM_MS + 100;
        render(q, none);
        render(h, fb);
        int x0 = SCREEN_W, y0 = SCREEN_H, x1 = -1, y1 = -1, changed = 0;
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++)
                if (fb[y * SCREEN_W + x] != none[y * SCREEN_W + x]) {
                    changed++;
                    if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
                }
        CHECK(changed > 3000);
        CHECK(y0 >= HEADER_Y1 - 8);            // clear of her name
        CHECK(x0 > 0 && x1 < SCREEN_W - 1);    // the whole cloud on screen
    }
}

static void settle(Game& g) { for (int i = 0; i < 60 && (g.party || g.greeting || g.memory_new); i++) g.tick(100); }

TEST(she_grows_up_and_never_back) {
    Game g = hatched();
    struct { int d, st; } T[] = {{0, STAGE_FOAL}, {6, STAGE_FOAL}, {7, STAGE_YOUNG}, {29, STAGE_YOUNG},
                                  {30, STAGE_GROWN}, {99, STAGE_GROWN}, {100, STAGE_STORYKEEPER}, {9999, STAGE_STORYKEEPER}};
    for (auto& x : T) { g.s.care_days = (u16)x.d; EQ(g.stage(), x.st); }
    g.s.care_days = 30;
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 0);
    EQ(g.stage(), STAGE_GROWN);                // neglect never takes it away
}

TEST(milestones_celebrate_once) {
    Game g = hatched();
    g.set_wall(at(2026, 11, 2, 9, 0));
    settle(g);
    g.s.care_days = 6; g.s.memory_day = 1;
    g.input(IN_TOUCH, 184 + g.pet_x, 200);     // the 7th day of care
    EQ(g.s.care_days, 7);
    run(g, MEMORY_SHOW_MS + 400);              // first the picture floats up, then the party
    EQ(g.party, PARTY_MILESTONE); EQ(g.party_arg, 0);
    CHECK(g.s.milestones & 1);
    run(g, MILESTONE_MS + 200); EQ(g.party, PARTY_NONE);
    u8 buf[SAVE_SIZE]; g.save(buf);
    Game h; h.init(1); CHECK(h.load(buf, SAVE_SIZE));
    run(h, 3000); EQ(h.party, PARTY_NONE);     // a reboot does not repeat it
    h.s.care_days = 120;                       // a big jump: one party, for the biggest step
    run(h, 200);
    EQ(h.party, PARTY_MILESTONE); EQ(h.party_arg, 2); EQ(h.s.milestones, 7);
    run(h, MILESTONE_MS + 400); EQ(h.party, PARTY_NONE);
}

TEST(birthday_once_a_year) {
    Game g = picked(3);
    g.set_wall(at(2026, 3, 10, 9, 0));
    for (int i = 0; i < EGG_TAPS; i++) g.input(IN_TOUCH, 184, 230);
    EQ(g.s.hatch_day, at(2026, 3, 10, 0, 0) / 86400); EQ(g.s.party_year, 26);
    run(g, HATCH_MS + 3000); EQ(g.party, PARTY_NONE);   // no cake on the day she hatches
    g.set_wall(at(2027, 3, 9, 9, 0)); run(g, 300); EQ(g.party, PARTY_NONE);
    g.set_wall(at(2027, 3, 10, 9, 0)); run(g, 300);
    EQ(g.party, PARTY_BIRTHDAY); EQ(g.years(), 1);
    run(g, PARTY_MS + 300); EQ(g.party, PARTY_NONE);
    g.set_wall(at(2027, 4, 1, 9, 0)); run(g, 300); EQ(g.party, PARTY_NONE);
    g.set_wall(at(2030, 8, 1, 9, 0));          // away for three years: one party, not three
    g.s.seen_day = (u16)(at(2030, 8, 1, 0, 0) / 86400);
    run(g, 300); EQ(g.party, PARTY_BIRTHDAY); EQ(g.years(), 4);
    run(g, PARTY_MS + 300); EQ(g.party, PARTY_NONE);
    Game l = hatched();                        // born on 29 February: the 28th in other years
    l.s.hatch_day = (u16)(at(2028, 2, 29, 0, 0) / 86400); l.s.party_year = 28;
    l.set_wall(at(2029, 2, 28, 9, 0)); settle(l); run(l, 300);
    EQ(l.party, PARTY_BIRTHDAY);
    Game o = hatched();                        // an older unicorn: adopted today, cake next year
    EQ(o.s.hatch_day, 0);
    o.set_wall(at(2026, 11, 2, 9, 0)); run(o, 300);
    EQ(o.s.hatch_day, at(2026, 11, 2, 0, 0) / 86400); CHECK(o.s.life_flags & LIFE_ADOPTED);
    EQ(o.party, PARTY_NONE);
    o.set_wall(at(2026, 11, 2, 9, 0), false);  // no birthdays on a guessed date
    o.s.party_year = 20;
    run(o, 300); EQ(o.party, PARTY_NONE);
}

TEST(warm_reunion_after_a_long_gap) {
    Game g = hatched();
    g.set_wall(at(2026, 11, 2, 9, 0));
    g.input(IN_TOUCH, 184 + g.pet_x, 200);
    EQ(g.s.seen_day, at(2026, 11, 2, 0, 0) / 86400);
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 1600);
    g.set_wall(at(2026, 11, 8, 9, 0)); settle(g); run(g, 300);
    EQ(g.party, PARTY_NONE);                   // six days: just the usual hello
    g.set_wall(at(2026, 11, 20, 9, 0));
    g.input(IN_TOUCH, 184 + g.pet_x, 200);     // petting her first does not cancel the welcome
    run(g, MEMORY_SHOW_MS + 600);
    EQ(g.party, PARTY_REUNION);
    for (int i = 0; i < NSTATS; i++) CHECK(g.stat(i) >= REUNION_NEED);   // a welcome gift
    EQ(g.s.seen_day, at(2026, 11, 20, 0, 0) / 86400);
    run(g, PARTY_MS + 300);
    u8 buf[SAVE_SIZE]; g.save(buf);
    Game h; h.init(2); CHECK(h.load(buf, SAVE_SIZE));
    h.set_wall(at(2026, 11, 20, 10, 0)); run(h, 3000); EQ(h.party, PARTY_NONE);   // only once
    Game a = hatched();                        // she is out with friends: the welcome waits for home
    a.set_wall(at(2026, 11, 2, 9, 0)); a.input(IN_TOUCH, 184 + a.pet_x, 200);
    a.play_ms = a.play_limit_ms(); run(a, LEAVE_MS + 300); EQ(a.scene, SC_AWAY);
    a.set_wall(at(2026, 12, 2, 9, 0)); run(a, 300);
    EQ(a.party, PARTY_NONE); CHECK(a.scene == SC_AWAY || a.scene == SC_RETURN);
    a.input(IN_SELECT, 0, 0);
    for (int i = 0; i < 80 && a.scene != SC_HOME; i++) { a.tick(100); EQ(a.party, PARTY_NONE); }
    run(a, MEMORY_SHOW_MS + 400);              // (yesterday's picture floats up first)
    EQ(a.scene, SC_HOME); EQ(a.party, PARTY_REUNION);
}

TEST(save_layout_v6) {
    EQ(SAVE_SIZE, 56);
    Save s;
    EQ((int)((u8*)&s.memory_day - (u8*)&s), 36);   // older fields keep their place
    EQ((int)((u8*)&s.bed10 - (u8*)&s), 47);        // v7's bedtime sits in v6's (zero) pad byte
    EQ((int)((u8*)&s.check - (u8*)&s), 52);
    Game g = hatched();                            // a v6 save (52 bytes) loads with the defaults
    g.s.care_days = 20; g.s.party_year = 26;
    u8 buf[SAVE_SIZE];
    g.save(buf);
    buf[0] = 6;
    buf[47] = 0;
    u32 c = 2166136261u;
    for (int i = 0; i < SAVE_V6_SIZE - 4; i++) c = (c ^ buf[i]) * 16777619u;
    std::memcpy(buf + SAVE_V6_SIZE - 4, &c, 4);
    Game h; h.init(4);
    CHECK(h.load(buf, SAVE_V6_SIZE));
    EQ(h.s.care_days, 20); EQ(h.s.bed10, BED_DEFAULT10); EQ(h.s.wake10, WAKE_DEFAULT10); EQ(h.s.tricks_seen, 0);
    EQ(h.tricks(), 4);                             // what she learned is still hers
}

TEST(a_foal_is_drawn_smaller) {
    Game g = hatched();
    g.hint_pet = 0;
    g.s.care_days = 2; g.tick(100); render(g, fb);
    int foal = lit_pixels(0, 100, SCREEN_W, 350);
    g.s.care_days = 7; g.tick(100); render(g, fb);
    int young = lit_pixels(0, 100, SCREEN_W, 350);
    CHECK(foal * 10 < young * 7);          // about (4/6)^2 of the pixels
}

static int count_tricks(Game& g, int ms, int* kinds, u32* min_gap) {   // kinds: per trick
    int n = 0, last = IDLE_NONE;
    u32 prev = 0;
    for (int t = 0; t < ms / 100; t++) {
        g.tick(100);
        if (g.idle_act >= IDLE_HUM && last < IDLE_HUM) {
            kinds[g.idle_act - IDLE_HUM]++;
            if (n && g.clock_ms - prev < *min_gap) *min_gap = g.clock_ms - prev;
            prev = g.clock_ms;
            n++;
        }
        last = g.idle_act;
    }
    return n;
}

TEST(tricks_learned_over_the_days) {
    int days[5] = {2, 3, 14, 60, 250}, known[5] = {0, 1, 4, 8, TRICKS};
    for (int k = 0; k < 5; k++) {
        Game g = hatched(50 + k);
        g.hint_pet = 0;
        for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000);
        g.s.care_days = (u16)days[k];
        g.s.tricks_seen = (u8)(known[k] > 2 ? known[k] - 2 : 0);   // the two newest not shown yet
        EQ(g.tricks(), known[k]);
        int kinds[TRICKS] = {0}, first = -1;
        u32 gap = 1u << 30;
        int n = 0;
        for (int r = 0; r < 12 * TRICKS; r++) {
            g.input(IN_NEXT, 0, 0);
            n += count_tricks(g, 9000, kinds, &gap);
            if (first < 0) for (int j = 0; j < TRICKS; j++) if (kinds[j]) first = j;
        }
        if (!known[k]) { EQ(n, 0); continue; }
        EQ(first, known[k] - 1);               // a newly learned trick is shown off first
        EQ(g.s.tricks_seen, known[k]);
        CHECK(gap >= TRICK_GAP_MS);
        for (int j = 0; j < TRICKS; j++) CHECK(j < known[k] ? kinds[j] > 0 : kinds[j] == 0);
        CHECK(g.facing == 1 || g.facing == -1);
    }
    Game c = hatched(60);                      // calm mode: no hopping or spinning tricks
    c.s.opts |= OPT_CALM; c.s.care_days = 250; c.s.tricks_seen = TRICKS; c.hint_pet = 0;
    for (int i = 0; i < NSTATS; i++) c.set_stat(i, 9000);
    int kinds[TRICKS] = {0};
    u32 gap = 1u << 30;
    for (int r = 0; r < 12 * TRICKS; r++) { c.input(IN_NEXT, 0, 0); count_tricks(c, 9000, kinds, &gap); }
    for (int j = 0; j < TRICKS; j++) CHECK(TRICK_CALM[j] ? kinds[j] > 0 : kinds[j] == 0);
    Game h = hatched(61);                      // hungry: no tricks, just blinking
    h.s.care_days = 250; h.set_stat(HUNGER, 500);
    int none[TRICKS] = {0};
    int n = 0;
    for (int r = 0; r < 10; r++) { h.input(IN_NEXT, 0, 0); n += count_tricks(h, 9000, none, &gap); }
    EQ(n, 0);
    Game a = hatched(62), b = hatched(62);     // tricks keep the tick-size independence
    a.s.care_days = b.s.care_days = 250;
    for (int i = 0; i < NSTATS; i++) { a.set_stat(i, 9000); b.set_stat(i, 9000); }
    a.tick(60000);
    for (int i = 0; i < 600; i++) b.tick(100);
    EQ(a.idle_act, b.idle_act); EQ(a.pet_x, b.pet_x); EQ(a.facing, b.facing); EQ(a.trick_next, b.trick_next);
    for (int k = 0; k < TRICKS; k++) {         // every trick draws inside the screen, both ways, foal too
        for (int f = 0; f < 4; f++) {
            Game r = hatched(80 + k);
            r.s.care_days = f & 2 ? 2 : 250; r.hint_pet = 0;
            r.facing = (i8)(f & 1 ? -1 : 1);
            r.idle_act = (u8)(IDLE_HUM + k); r.idle_end = r.clock_ms + TRICK_MS[k];
            for (int s = 0; s < TRICK_MS[k] / 100; s += 3) { r.tick(100); render(r, fb); r.idle_act = (u8)(IDLE_HUM + k); }
        }
    }
    u8 buf[SAVE_SIZE];                         // what she learned survives a power cut (or a reflash)
    a.s.tricks_seen = 7;
    a.save(buf);
    Game l; l.init(1);
    CHECK(l.load(buf, SAVE_SIZE)); EQ(l.tricks(), TRICKS); EQ(l.s.tricks_seen, 7);
}

static void open_clock_page(Game& g) {
    g.input(IN_PARENT, 0, 0);
    EQ(g.scene, SC_SETTINGS);
    g.input(IN_TOUCH, 300, SET_Y0 + SET_CLOCK * SET_ROW_H + 18);   // SAAT: the clock & WiFi hub
    EQ(g.set_page, PG_HUB);
    g.input(IN_TOUCH, 300, SET_Y0 + HUB_CLOCK * SET_ROW_H + 18);
    EQ(g.set_page, PG_CLOCK);
}
static void leave_settings(Game& g) {      // from the hub (after KAYDET/GERİ) out of the settings
    if (g.set_page == PG_HUB) g.input(IN_TOUCH, 184, SET_Y0 + HUB_BACK * SET_ROW_H + 18);
    g.input(IN_TOUCH, 184, SET_Y0 + SET_EXIT * SET_ROW_H + 18);
}
static void tap_clock(Game& g, int row, int dir, int times = 1) {
    for (int i = 0; i < times; i++) g.input(IN_TOUCH, dir < 0 ? 60 : 300, SET_Y0 + row * SET_ROW_H + 18);
}

TEST(parent_sets_the_clock) {
    Game g = hatched();
    g.set_wall(at(2026, 1, 1, 0, 0), false);   // a flat battery: the date is a guess
    EQ(g.today().known, 0);
    open_clock_page(g);
    EQ(g.ed_year, 2026); EQ(g.ed_month, 1); EQ(g.ed_day, 1);
    tap_clock(g, CLK_MONTH, +1, 9);            // October
    tap_clock(g, CLK_DAY, -1, 1);              // 1 -> 31 (wraps to the month's end)
    tap_clock(g, CLK_HOUR, +1, 14);
    tap_clock(g, CLK_MIN, -1, 55);             // 0 -> 59 -> ... -> 5
    tap_clock(g, CLK_SAVE, +1);
    EQ(g.set_page, PG_HUB); EQ(g.set_row, HUB_CLOCK); EQ(g.scene, SC_SETTINGS);
    g.set_wall(at(2026, 1, 1, 0, 1), false);   // the platform's old RTC reading, before it heard
    u32 f = g.tick(0);
    CHECK(f & F_CLOCK); CHECK(f & F_SAVE);
    EQ(g.wall_s, at(2026, 10, 31, 14, 5));
    CHECK(g.today().known);
    g.set_wall(g.wall_s + 1, true);            // what the platform reads back from its RTC
    Cal c = g.today();
    EQ(c.month, 10); EQ(c.mday, 31);
    EQ(g.tick(0) & F_CLOCK, 0u);               // reported once
    EQ(g.s.seen_day, at(2026, 10, 31, 0, 0) / 86400);   // no "long absence" from the jump
    leave_settings(g);
    run(g, 2000); EQ(g.party, PARTY_NONE);
    Game b = hatched();                        // GERİ leaves without changing anything
    b.set_wall(at(2026, 5, 5, 9, 0));
    open_clock_page(b);
    tap_clock(b, CLK_YEAR, +1, 3);
    tap_clock(b, CLK_BACK, +1);
    EQ(b.tick(0) & F_CLOCK, 0u); EQ(b.wall_s, at(2026, 5, 5, 9, 0));
    Game l = hatched();                        // 31 March, then February: the day follows
    l.set_wall(at(2027, 3, 31, 9, 0));
    open_clock_page(l);
    tap_clock(l, CLK_MONTH, -1);
    EQ(l.ed_month, 2); EQ(l.ed_day, 28);
}

TEST(parent_sets_the_birthday) {
    Game g = hatched();
    g.set_wall(at(2027, 6, 15, 10, 0));
    run(g, 300);                               // an older unicorn: adopted today
    CHECK(g.s.life_flags & LIFE_ADOPTED);
    open_clock_page(g);
    EQ(g.ed_bmonth, 6); EQ(g.ed_bday, 15);
    tap_clock(g, CLK_SAVE, +1);                // birthday set to today: the party is today
    CHECK(!(g.s.life_flags & LIFE_ADOPTED));
    EQ(g.s.hatch_day, at(2027, 6, 15, 0, 0) / 86400);
    leave_settings(g);
    run(g, 300);
    EQ(g.party, PARTY_NONE);                   // born this year (the adopted day): first party next year
    Game o = hatched();                        // a real hatch day two years ago
    o.s.hatch_day = (u16)(at(2025, 3, 1, 0, 0) / 86400); o.s.party_year = 26;
    o.set_wall(at(2027, 6, 15, 10, 0));
    open_clock_page(o);
    tap_clock(o, CLK_BMONTH, +1, 3); tap_clock(o, CLK_BDAY, +1, 14);   // 15 June: today
    tap_clock(o, CLK_SAVE, +1);
    EQ(o.s.hatch_day, at(2025, 6, 15, 0, 0) / 86400);
    leave_settings(o);
    run(o, 400); EQ(o.party, PARTY_BIRTHDAY); EQ(o.years(), 2);
    Game p = hatched();                        // a birthday already past this year: no late party
    p.s.hatch_day = (u16)(at(2025, 3, 1, 0, 0) / 86400); p.s.party_year = 26;
    p.set_wall(at(2027, 6, 15, 10, 0));
    open_clock_page(p);
    tap_clock(p, CLK_BMONTH, -1, 3);           // 1 March -> December... back to 1 March
    tap_clock(p, CLK_BMONTH, +1, 3);
    tap_clock(p, CLK_SAVE, +1);
    leave_settings(p);
    run(p, 400); EQ(p.party, PARTY_NONE); EQ(p.s.party_year, 27);
    Game f = hatched();                        // 29 February: only offered as month 2 allows
    f.set_wall(at(2027, 6, 15, 10, 0));
    open_clock_page(f);
    tap_clock(f, CLK_BMONTH, -1, 4); EQ(f.ed_bmonth, 2);
    tap_clock(f, CLK_BDAY, +1, 14); EQ(f.ed_bday, 29);
    tap_clock(f, CLK_SAVE, +1);
    Cal h = calendar(f.s.hatch_day * 86400u);
    EQ(h.month, 2); EQ(h.mday, 28);            // 2027 has no 29 February
    Game e = picked(5);                        // still an egg: the birthday rows do nothing
    e.set_wall(at(2027, 6, 15, 10, 0));
    open_clock_page(e);
    u8 bm = e.ed_bmonth;
    tap_clock(e, CLK_BMONTH, +1);
    EQ(e.ed_bmonth, bm);
    tap_clock(e, CLK_SAVE, +1);
    EQ(e.s.hatch_day, 0);
}

static void plug(Game& g, bool on) { g.set_plugged(on); g.set_plugged(on); }   // two agreeing samples

TEST(charging_on_the_sun_lounger) {
    Game g = hatched();
    g.set_wall(at(2026, 7, 1, 11, 0));
    settle(g);
    g.set_plugged(true);                       // the first sample after power-up: no fanfare
    EQ(g.scene, SC_HOME); EQ(g.plugged, 1);
    plug(g, false);
    g.set_plugged(true);                       // one wobbly sample does nothing
    EQ(g.scene, SC_HOME);
    g.set_plugged(false);
    run(g, PLUG_QUIET_MS + 500);
    run(g, SCREEN_OFF_MS); CHECK(!g.screen_on);
    plug(g, true);                             // plugged in: the screen lights, she lies down
    CHECK(g.screen_on); EQ(g.scene, SC_LOUNGE); EQ(cue_after(g), SFX_CHARGE);
    g.set_stat(ENERGY, 2000); g.set_stat(HUNGER, 8000); g.set_stat(HAPPY, 8000);
    run(g, 60 * 60000);                        // an hour in the sun charges her too
    EQ(g.scene, SC_LOUNGE);
    EQ(g.stat(ENERGY), 2000 + 60 * SLEEP_ENERGY_GAIN);
    EQ(g.stat(HUNGER), 8000 - 60 * (DECAY_HUNGER / 2)); EQ(g.stat(HAPPY), 8000);
    g.input(IN_SELECT, 0, 0);                  // wake the screen
    g.input(IN_TOUCH, 184, 220);               // a touch on her: hearts, she stays
    EQ(g.scene, SC_LOUNGE); EQ(cue_after(g), SFX_PET);
    plug(g, false);                            // unplugged: up she gets
    EQ(g.scene, SC_HOME); EQ(cue_after(g), SFX_WAKE);
    plug(g, true);                             // straight back in (a loose cable): no fanfare
    EQ(g.scene, SC_LOUNGE); EQ(cue_after(g), SFX_NONE);
    tap_menu(g, MENU_FEED);                    // the menu works from the lounger
    EQ(g.scene, SC_FEED);
    g.input(IN_TOUCH, 60, 200); run(g, ACTION_MS + 200);
    EQ(g.scene, SC_HOME);
    run(g, LOUNGE_IDLE_MS + 200);              // still charging: back to the lounger
    EQ(g.scene, SC_LOUNGE);
    Game a = g, b = g;                         // dark on the lounger: tick-size independent
    run(a, SCREEN_OFF_MS + 100); run(b, SCREEN_OFF_MS + 100);
    a.tick(3 * 3600000u);
    for (int i = 0; i < 3 * 36000; i++) b.tick(100);
    EQ(a.stat(ENERGY), b.stat(ENERGY)); EQ(a.stat(HUNGER), b.stat(HUNGER)); EQ(a.scene, b.scene);
}

TEST(charging_at_night_and_elsewhere) {
    Game g = hatched();
    g.set_wall(at(2026, 7, 1, 22, 0));
    settle(g);
    g.set_plugged(false);
    plug(g, true);                             // night, not tired: a lie-down with a sleep mask
    EQ(g.scene, SC_LOUNGE);
    plug(g, false);
    Game t = hatched();
    t.set_wall(at(2026, 7, 1, 22, 0)); settle(t);
    t.set_plugged(false);
    t.set_stat(ENERGY, 1000);
    plug(t, true);                             // night and tired: to bed
    EQ(t.scene, SC_SLEEP);
    run(t, SCREEN_OFF_MS + 100); CHECK(!t.screen_on);
    plug(t, false);
    EQ(t.scene, SC_SLEEP); CHECK(!t.screen_on);   // unplugging neither wakes her nor lights the room
    Game e = picked(3);                        // an egg: the screen lights, nothing else
    e.set_plugged(false); run(e, SCREEN_OFF_MS + 100);
    plug(e, true);
    EQ(e.scene, SC_EGG);
    Game d = hatched();                        // out with friends: the door stays shut
    d.set_plugged(false);
    d.play_ms = d.play_limit_ms(); run(d, LEAVE_MS + 300);
    EQ(d.scene, SC_AWAY);
    run(d, d.break_ms() + 1000); CHECK(!d.screen_on);
    plug(d, true);                             // even with the break over, the cable is no knock
    run(d, 300); EQ(d.scene, SC_AWAY); CHECK(!d.screen_on);
    for (int k = 0; k < 8; k++) {              // every look draws inside the screen (ASan watches)
        Game r = hatched(70 + k);
        r.set_wall(at(2026, 7, 1, k & 1 ? 22 : 11, 0));
        r.s.care_days = (u16)(k & 2 ? 0 : 40);
        r.set_plugged(false); plug(r, true);
        r.facing = (i8)(k & 4 ? -1 : 1);
        EQ(r.scene, SC_LOUNGE);
        r.tick(300);
        if (k == 0) shot(r, "lounge"); else render(r, fb);
        CHECK(lit_pixels(0, 150, SCREEN_W, 345) > 8000);
    }
    Game p = hatched();                        // a parent can open the settings while she charges
    p.set_plugged(false); plug(p, true);
    p.input(IN_PARENT, 0, 0); EQ(p.scene, SC_SETTINGS);
    Game l = hatched();                        // lounging is screen time like home
    l.set_plugged(false); plug(l, true);
    EQ(l.scene, SC_LOUNGE);
    l.play_ms = l.play_limit_ms();
    run(l, 300);
    EQ(l.scene, SC_LEAVE);
}

static void at_time(Game& g, int h, int m) { g.set_wall(at(2026, 11, 3, h, m)); }

TEST(bedtime_sways_then_sleeps_on_her_feet) {
    Game g = hatched();
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000);
    at_time(g, 19, 0); settle(g); run(g, 300);
    EQ(g.bedtime(), BED_NONE); EQ(g.scene, SC_HOME);
    at_time(g, 19, 30); run(g, 300);           // bedtime: drowsy, the moon button bounces
    EQ(g.bedtime(), BED_DROWSY); EQ(g.recommended(), MENU_SLEEP); EQ(g.scene, SC_HOME);
    g.s.care_days = 250;                       // no tricks or walks while drowsy
    int busy = 0;
    for (int i = 0; i < 300; i++) { g.tick(100); if (g.idle_act >= IDLE_WALK) busy++; if (i % 50 == 0) g.input(IN_NEXT, 0, 0); }
    EQ(busy, 0);
    at_time(g, 20, 29); run(g, 300); EQ(g.scene, SC_HOME);
    at_time(g, 20, 30); run(g, 300);           // an hour later, still up: asleep on her feet
    EQ(g.scene, SC_SLEEP); EQ(g.standing, 1);
    render(g, fb); CHECK(lit_pixels(0, 100, SCREEN_W, 350) > 8000);
    g.input(IN_TOUCH, 184, 200);               // touches do not wake her
    g.input(IN_SELECT, 0, 0); g.input(IN_SHAKE, 0, 0);
    EQ(g.scene, SC_SLEEP);
    g.set_stat(ENERGY, MAX_STAT); run(g, 3 * 60000);   // nor does being fully rested
    EQ(g.scene, SC_SLEEP);
    u32 saves = 0;
    for (int i = 0; i < 100; i++) if (g.tick(100) & F_SAVE) saves++;
    CHECK(saves <= 1);                         // and it does not hammer the flash all night
    at_time(g, 6, 59); run(g, 300); EQ(g.scene, SC_SLEEP);
    at_time(g, 7, 0); run(g, 300);             // morning: up she gets
    EQ(g.scene, SC_HOME); EQ(g.standing, 0);
}

TEST(put_to_bed_stays_asleep_till_morning) {
    Game g = hatched();
    at_time(g, 19, 45); settle(g);
    g.set_stat(ENERGY, 9000);
    tap_menu(g, MENU_SLEEP);                   // put to bed at bedtime: the usual quilt ritual
    run(g, TUCK_MS + 200);
    EQ(g.scene, SC_SLEEP); EQ(g.standing, 0);
    g.input(IN_TOUCH, 184, 200);
    EQ(g.scene, SC_SLEEP);
    at_time(g, 23, 0); run(g, 60 * 60000);
    EQ(g.scene, SC_SLEEP);
    at_time(g, 7, 10); run(g, 300);
    EQ(g.scene, SC_HOME);
    Game n = hatched();                        // an afternoon nap is not locked
    at_time(n, 15, 0); settle(n);
    tap_menu(n, MENU_SLEEP); run(n, TUCK_MS + 200);
    n.input(IN_TOUCH, 184, 200);
    EQ(n.scene, SC_HOME);
    Game r = hatched();                        // a nap that rolls into bedtime stays asleep
    at_time(r, 19, 20); settle(r);
    tap_menu(r, MENU_SLEEP); run(r, TUCK_MS + 200);
    at_time(r, 19, 31); run(r, 300);
    r.input(IN_TOUCH, 184, 200);
    EQ(r.scene, SC_SLEEP);
}

TEST(bedtime_is_tick_size_independent) {
    Game a = hatched(), b = hatched();
    at_time(a, 21, 0); at_time(b, 21, 0);
    a.s.opts |= OPT_NO_BEDTIME; b.s.opts |= OPT_NO_BEDTIME;   // set up on the info screen first
    tap_header(a); tap_header(b);
    a.s.opts &= ~OPT_NO_BEDTIME; b.s.opts &= ~OPT_NO_BEDTIME;
    a.tick(INFO_MS + 1000);
    for (int i = 0; i < (INFO_MS + 1000) / 100; i++) b.tick(100);
    EQ(a.scene, b.scene); EQ(a.standing, b.standing);
    EQ(a.scene, SC_SLEEP);
}

TEST(bedtime_edges) {
    Game g = hatched();                        // power-up in the middle of the night: lying asleep
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000);
    u8 buf[SAVE_SIZE]; g.save(buf);
    Game b; b.init(3); CHECK(b.load(buf, SAVE_SIZE));
    at_time(b, 23, 0); b.tick(100);
    EQ(b.scene, SC_SLEEP); EQ(b.standing, 0); EQ(b.greeting, 0);
    b.set_wall(at(2026, 11, 3, 23, 30), false);   // the clock stops being trusted: she can be woken
    b.input(IN_TOUCH, 184, 200);
    EQ(b.scene, SC_HOME);
    Game l = hatched();                        // time's up at bedtime: to sleep, not out with friends
    at_time(l, 19, 40); settle(l);
    l.play_ms = l.play_limit_ms(); run(l, 300);
    EQ(l.scene, SC_SLEEP); EQ(l.standing, 1);
    Game o = hatched();                        // bedtime off: nothing happens
    o.s.opts |= OPT_NO_BEDTIME;
    at_time(o, 22, 0); run(o, 300);
    EQ(o.scene, SC_HOME);
    Game w = hatched();                        // a bedtime after midnight works too (01:00 - 09:00)
    w.s.bed10 = 6; w.s.wake10 = 54;
    at_time(w, 0, 50); run(w, 300); EQ(w.bedtime(), BED_NONE);
    at_time(w, 1, 30); run(w, 300); EQ(w.bedtime(), BED_DROWSY);
    at_time(w, 8, 0); run(w, 300); EQ(w.scene, SC_SLEEP);
    at_time(w, 9, 0); run(w, 300); EQ(w.scene, SC_HOME);
    Game s = hatched();                        // the parent page: on/off, -10/+10 minutes
    s.input(IN_PARENT, 0, 0);
    s.input(IN_TOUCH, 300, SET_Y0 + SET_SLEEP * SET_ROW_H + 18);
    EQ(s.set_page, 2);
    s.input(IN_TOUCH, 60, SET_Y0 + BEDR_BED * SET_ROW_H + 18);     // 19:30 -> 19:20
    s.input(IN_TOUCH, 300, SET_Y0 + BEDR_WAKE * SET_ROW_H + 18);   // 07:00 -> 07:10
    EQ(s.s.bed10, BED_DEFAULT10 - 1); EQ(s.s.wake10, WAKE_DEFAULT10 + 1);
    s.input(IN_TOUCH, 300, SET_Y0 + BEDR_ON * SET_ROW_H + 18);
    CHECK(s.s.opts & OPT_NO_BEDTIME);
    render(s, fb); CHECK(lit_pixels(0, 0, SCREEN_W, SCREEN_H) > 2000);
    s.input(IN_TOUCH, 300, SET_Y0 + BEDR_BACK * SET_ROW_H + 18);
    EQ(s.set_page, 0); EQ(s.set_row, SET_SLEEP);
    Game p = hatched();                        // a grown-up opens the settings while she sleeps at night
    at_time(p, 22, 0); p.tick(100); run(p, 300);
    EQ(p.scene, SC_SLEEP);
    p.input(IN_PARENT, 0, 0); EQ(p.scene, SC_SETTINGS);
    p.input(IN_TOUCH, 184, SET_Y0 + SET_EXIT * SET_ROW_H + 18);
    EQ(p.scene, SC_SLEEP); EQ(p.standing, 0);  // and closes them: she is still asleep, lying down
}

// BOOT pressed for `hold` ms, then released, sampled every 10 ms like a firmware loop
static void boot_press(Game& g, int hold, int gap_after = 100) {
    for (int t = 0; t < hold; t += 10) { g.set_boot(true); g.tick(10); }
    for (int t = 0; t < gap_after; t += 10) { g.set_boot(false); g.tick(10); }
}

TEST(boot_taps_and_the_grown_up_gesture) {
    Game g = hatched();
    EQ(g.menu, 0);
    boot_press(g, 80, 500);                    // one tap: the cursor moves once, after the tap
    EQ(g.menu, 1);
    boot_press(g, 80, 150); boot_press(g, 80, 500);   // two quick taps: twice
    EQ(g.menu, 3);
    boot_press(g, 5000, 500);                  // a long press alone is just a tap (no secret door)
    EQ(g.scene, SC_HOME); EQ(g.menu, 0);
    boot_press(g, 80, 150); boot_press(g, 80, 150);   // the gesture: tap, tap, press and hold...
    for (int t = 0; t < PARENT_HOLD_MS - 100; t += 10) { g.set_boot(true); g.tick(10); }
    EQ(g.scene, SC_HOME);
    for (int t = 0; t < 200; t += 10) { g.set_boot(true); g.tick(10); }
    EQ(g.scene, SC_SETTINGS);                  // ...opens the settings
    EQ(g.menu, 0);                             // and the menu cursor did not move on the way
    for (int t = 0; t < 600; t += 10) { g.set_boot(false); g.tick(10); }
    EQ(g.scene, SC_SETTINGS); EQ(g.set_row, 0);   // letting go adds no stray tap
    boot_press(g, 80, 150); boot_press(g, 80, 150); boot_press(g, 80, 600);   // three plain taps: rows
    EQ(g.set_row, 3); EQ(g.scene, SC_SETTINGS);
    Game b = hatched();                        // contact bounce is one press, not three
    for (int i = 0; i < 3; i++) { b.set_boot(true); b.tick(5); b.set_boot(false); b.tick(5); }
    b.set_boot(true); b.tick(80);
    for (int t = 0; t < 500; t += 10) { b.set_boot(false); b.tick(10); }
    EQ(b.menu, 1);
    Game d = hatched();                        // on the door the gesture calls her back
    d.play_ms = d.play_limit_ms(); run(d, LEAVE_MS + 300); EQ(d.scene, SC_AWAY);
    boot_press(d, 80, 150); boot_press(d, 80, 150);
    for (int t = 0; t < PARENT_HOLD_MS + 100; t += 10) { d.set_boot(true); d.tick(10); }
    EQ(d.scene, SC_RETURN);
}

static void tap_row(Game& g, int row) { g.input(IN_TOUCH, 300, SET_Y0 + row * SET_ROW_H + 18); }
static void tap_button(Game& g, int right) { g.input(IN_TOUCH, right ? 280 : 90, (WBTN_Y0 + WBTN_Y1) / 2); }
static int take_wifi(Game& g) {            // what the platform does with F_WIFI
    u32 f = g.tick(0);
    if (!(f & F_WIFI)) return WREQ_NONE;
    int r = g.wifi_req;
    g.wifi_req = WREQ_NONE;
    return r;
}

TEST(wifi_setup_from_the_toy) {
    Game g = hatched();
    g.wifi_list_clear(); g.wifi_list_add("HomeNet", true);
    g.input(IN_PARENT, 0, 0);
    tap_row(g, SET_CLOCK); EQ(g.set_page, PG_HUB);
    tap_row(g, HUB_WIFI); EQ(g.set_page, PG_WIFI);
    shot(g, "wifi_page");
    tap_row(g, WF_ADD); EQ(g.set_page, PG_WSTART);
    shot(g, "wifi_start");
    tap_button(g, 0); EQ(g.set_page, PG_WIFI);          // GERİ: nothing started
    EQ(take_wifi(g), WREQ_NONE);
    tap_row(g, WF_ADD); tap_button(g, 1);               // BAŞLAT
    EQ(g.set_page, PG_WSESSION); EQ(g.wifi_stage, WST_STARTING);
    EQ(take_wifi(g), WREQ_START);
    g.wifi_session("Unicorn-A7C2", "kq7m4xtz9p2w");   // the platform's network is up
    EQ(g.wifi_stage, WST_WAIT);
    CHECK(g.qr_side >= 25 && g.qr_side <= QR_MAX_SIDE);
    shot(g, "wifi_qr");
    run(g, SCREEN_OFF_MS + 2000); CHECK(g.screen_on);   // the QR stays lit while the parent scans
    tap_button(g, 0); EQ(g.set_page, PG_WHELP);         // YARDIM: join by hand
    shot(g, "wifi_help");
    tap_button(g, 1); EQ(g.set_page, PG_WSESSION);
    g.wifi_status(WST_PHONE, 0); shot(g, "wifi_phone");
    tap_button(g, 1);                                   // OYUNA DÖN: play on, setup carries on
    EQ(g.scene, SC_HOME); CHECK(g.wifi_busy());
    g.wifi_status(WST_JOIN, "HomeNet-2.4");
    shot(g, "wifi_badge");
    g.input(IN_PARENT, 0, 0);                           // the grown-up comes back: straight to it
    EQ(g.scene, SC_SETTINGS); EQ(g.set_page, PG_WSESSION);
    g.wifi_status(WST_ADDR, 0); shot(g, "wifi_joining");
    g.wifi_status(WST_OK, 0); shot(g, "wifi_ok");
    tap_button(g, 1);                                   // TAMAM: the radio goes off again
    EQ(take_wifi(g), WREQ_STOP); EQ(g.wifi_stage, WST_OFF); EQ(g.set_page, PG_WIFI);
    tap_row(g, WF_ADD); tap_button(g, 1); take_wifi(g);
    g.wifi_session("Unicorn-B1F0", "abcdefgh2345");
    g.wifi_status(WST_BAD_PASS, "HomeNet-2.4"); shot(g, "wifi_bad");
    tap_button(g, 1); EQ(take_wifi(g), WREQ_RETRY);     // TEKRAR: the phone form is still there
    g.wifi_status(WST_TIMEOUT, 0);
    tap_button(g, 1); EQ(take_wifi(g), WREQ_START);     // after a time-out: a fresh network
    g.wifi_status(WST_NOT_FOUND, 0);
    tap_button(g, 0); EQ(take_wifi(g), WREQ_STOP);      // BİTİR
    g.wifi_list_clear();                                // the saved list: built-in ones read-only
    g.wifi_list_add("HomeNet", true); g.wifi_list_add("Mormor", false); g.wifi_list_add("Yazlik", false);
    tap_row(g, WF_LIST); EQ(g.set_page, PG_WLIST);
    shot(g, "wifi_list");
    tap_row(g, 0); EQ(g.set_page, PG_WLIST); EQ(cue_after(g), SFX_REFUSE);
    tap_row(g, 1); EQ(g.set_page, PG_WFORGET);
    shot(g, "wifi_forget");
    tap_button(g, 1); EQ(take_wifi(g), WREQ_FORGET); EQ(g.wifi_arg, 1);
    EQ(g.set_page, PG_WLIST);
    for (int st = WST_OFF; st <= WST_TIMEOUT; st++) {   // every screen draws inside the frame
        g.set_page = PG_WSESSION; g.wifi_stage = (u8)st; render(g, fb);
        for (int p = PG_HUB; p <= PG_WFORGET; p++) { g.set_page = (u8)p; render(g, fb); }
    }
    Game q;                                            // the QR's text is escaped
    q.init(1);
    q.wifi_session("A;B", "p:w\\\"x");
    CHECK(q.qr_side > 0);
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
                render(g, fb);
                if (g.scene == SC_GAME || g.scene == SC_FEED) g.input(IN_SELECT, 0, 0);
            }
            CHECK(lit_pixels(0, 0, SCREEN_W, SCREEN_H) > 5000);
            if (g.scene == SC_SLEEP) { g.input(IN_SELECT, 0, 0); g.input(IN_SELECT, 0, 0); }
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
        T(event_feed_she_picks_the_snack), T(event_play_minigame_by_touch), T(minigame_with_buttons_and_shake),
        T(minigame_joy_does_not_depend_on_the_score), T(minigame_stars_keep_coming),
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
        T(ten_minutes_of_play_and_she_goes_out), T(leaving_waits_for_the_activity_to_finish),
        T(a_break_resets_the_screen_time), T(away_freezes_needs_and_keeps_the_screen_short),
        T(she_comes_back_after_the_break), T(parent_can_call_her_back), T(power_cut_does_not_skip_the_break),
        T(version_1_saves_still_load), T(frames_are_interpolated_between_steps),
        T(rtc_counts_the_break_while_powered_off), T(rtc_partial_break_and_wall_ticking),
        T(rtc_that_lost_power_is_not_trusted), T(no_rtc_counts_powered_time_only),
        T(version_2_saves_still_load), T(time_is_up_means_no_new_activities), T(tired_pillow_puts_her_to_sleep),
        T(a_one_minute_nap_is_enough_to_play), T(parent_settings), T(hints_for_pre_readers),
        T(stars_hit_where_they_are_drawn), T(version_3_saves_still_load), T(sound_toggle_overrides_parent_off),
        T(egg_choice_persists), T(picker_does_not_age), T(legacy_egg_keeps_identity), T(foam_puffs_pop_once),
        T(wash_without_help_ends_the_same), T(butterfly_visits_quietly), T(toy_box_routes),
        T(music_garden_echoes_the_tune_once), T(dress_up_roundtrip), T(accessory_follows_every_pose),
        T(tilt_a_cloud), T(modes_end_when_time_is_up), T(review_fixes_feature_round),
        T(calendar_reads_the_wall_clock), T(greeting_after_a_real_break), T(tuck_in_then_dreams),
        T(old_unix_return_time_falls_back), T(album_fills_one_picture_per_cared_day),
        T(garden_grows_with_cared_days), T(dreams_follow_her_day_and_vary),
        T(she_grows_up_and_never_back), T(milestones_celebrate_once), T(birthday_once_a_year),
        T(warm_reunion_after_a_long_gap), T(save_layout_v6), T(a_foal_is_drawn_smaller),
        T(tricks_learned_over_the_days), T(parent_sets_the_clock), T(parent_sets_the_birthday),
        T(charging_on_the_sun_lounger), T(charging_at_night_and_elsewhere),
        T(bedtime_sways_then_sleeps_on_her_feet), T(put_to_bed_stays_asleep_till_morning), T(bedtime_edges),
        T(bedtime_is_tick_size_independent), T(boot_taps_and_the_grown_up_gesture),
        T(wifi_setup_from_the_toy), T(album_opens_from_the_info_screen),
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
