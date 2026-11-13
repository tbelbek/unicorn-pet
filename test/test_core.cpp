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
static void tap_set(Game& g, int x, int row) {   // a settings row, turning to its half of a long list first
    int n = g.set_rows();
    if (row != n - 1 && set_half(n, row) != set_half(n, g.set_row))
        g.input(IN_TOUCH, x, SET_Y0 + (set_half(n, row) ? SET_SPLIT : 0) * SET_ROW_H + 18);
    g.input(IN_TOUCH, x, SET_Y0 + set_row_slot(n, row) * SET_ROW_H + 18);
}
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
    g.input(IN_TOUCH, 2, 2);               // a miss scores nothing, but still answers
    EQ(g.score, 1);
    EQ(cue_after(g), SFX_TAP);
    CHECK(g.particles() > 0);
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
    run(g, WASH_MS - 800);
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

TEST(menu_strip_takes_the_nearest_button) {
    static const int X[] = {0, 91, 92, 183, 185, 275, 277, SCREEN_W - 1};
    for (int x : X)
        for (int y : {(int)MENU_Y0, 400, SCREEN_H - 1}) {   // gutters and the strip below the buttons too
            Game g = hatched();
            g.set_stat(ENERGY, 9000);
            g.input(IN_TOUCH, x, y);
            EQ(g.menu, x / MENU_STEP);
            CHECK(g.scene != SC_HOME);
        }
    Game g = hatched();
    g.input(IN_TOUCH, 184, MENU_Y0 - 1);    // above the strip: not the menu
    EQ(g.menu, 0); EQ(g.scene, SC_HOME);
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
    tap_menu(g, MENU_WASH);  EQ(cue_after(g), SFX_WASH);  run(g, WASH_MS);
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
    EQ(g.take_sfx(), SFX_NONE);                           // woke up by herself, in the dark: not a sound
    g.input(IN_SELECT, 0, 0);                             // wake screen (went dark while asleep)
    tap_header(g);
    EQ(cue_after(g), SFX_INFO);
    EQ(cue_after(g), SFX_NONE);                           // a cue is delivered once
}

TEST(the_child_cannot_mute_and_old_mutes_become_volume_off) {
    Game g = hatched();
    tap_header(g);
    g.tick(0); g.take_sfx();
    g.input(IN_TOUCH, 320, 26);                           // where the old "SES ACIK" switch was
    EQ(g.scene, SC_HOME);                                 // just closes the info screen
    EQ(g.volume(), VOL_NORMAL);
    shot(g, "15_info");
    Game m = hatched();                                   // a save muted by the old switch...
    m.s.muted = 1;
    u8 buf[SAVE_SIZE]; m.save(buf);
    Game h; h.init(1); CHECK(h.load(buf, sizeof buf));
    EQ(h.s.muted, 0);                                     // ...loads as the parent's "off"
    EQ(h.volume(), VOL_OFF);
    h.input(IN_PARENT, 0, 0);
    tap_set(h, 184, SET_EXIT);
    tap_menu(h, MENU_FEED);
    CHECK(!(h.tick(0) & F_SOUND));                        // and stays silent
    EQ(h.take_sfx(), SFX_NONE);
    Game k; k.init(1); CHECK(k.load(buf, sizeof buf));
    k.input(IN_PARENT, 0, 0);
    tap_set(k, 184, SET_VOLUME);                          // which a grown-up can turn back on
    EQ(k.volume(), VOL_NORMAL);
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
    CHECK(g.away_ms <= AWAY_MS && g.away_ms + 10000 >= AWAY_MS);   // the whole break, less the seconds she is out
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
    int hunger = g.stat(HUNGER);
    run(g, AWAY_SCREEN_OFF_MS - 300);
    CHECK(g.screen_on);
    g.input(IN_TOUCH, 184, 200);                         // knock knock
    EQ(cue_after(g), SFX_KNOCK);                         // heard, but nothing to win by knocking
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
    tap_set(g, 184, SET_LIMIT);   // session: 10 -> 15 min
    EQ(g.play_limit_ms(), 15u * 60000);
    tap_set(g, 184, SET_SHAKE);   // shake off
    tap_set(g, 184, SET_CALM);    // calm motion on
    tap_set(g, 184, SET_VOLUME);  // volume: low
    EQ(g.volume(), VOL_LOW);
    u32 f = g.tick(0);
    CHECK(f & F_SAVE);
    u32 p0 = g.play_ms;
    play_for(g, 11 * 60000);                 // in settings: not counted as play
    EQ(g.play_ms, p0);
    tap_set(g, 184, SET_EXIT);
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

TEST(parent_volume_off_stays_off) {
    Game g = hatched();
    g.input(IN_PARENT, 0, 0);
    tap_set(g, 184, SET_VOLUME);   // normal -> low
    tap_set(g, 184, SET_VOLUME);   // low -> off
    EQ(g.volume(), VOL_OFF);
    tap_set(g, 184, SET_EXIT);
    tap_header(g);
    g.input(IN_TOUCH, 320, 26);                         // nothing on the info screen turns it back on
    EQ(g.volume(), VOL_OFF);
    tap_menu(g, MENU_FEED);
    EQ(cue_after(g), SFX_NONE);
}

TEST(long_settings_lists_turn_pages) {
    Game g = hatched();
    g.input(IN_PARENT, 0, 0);
    shot(g, "20b_settings_half1");
    g.input(IN_TOUCH, 184, SET_Y0 + SET_SPLIT * SET_ROW_H + 18);   // the "more" arrow
    EQ(g.set_row, SET_SPLIT); EQ(g.set_page, PG_MAIN);
    shot(g, "20c_settings_half2");
    int calm = g.calm();
    g.input(IN_TOUCH, 184, SET_Y0 + set_row_slot(SET_ROWS, SET_CALM) * SET_ROW_H + 18);
    CHECK(g.calm() != calm);                            // a row on the second half
    g.input(IN_TOUCH, 184, SET_Y0 + 18);                // the "up" arrow
    EQ(g.set_row, 0);
    g.input(IN_TOUCH, 184, SET_Y0 + (SET_SLOTS - 1) * SET_ROW_H + 18);   // ÇIKIŞ, on both halves
    EQ(g.scene, SC_HOME);
    g.input(IN_PARENT, 0, 0);                           // BOOT walks every row, across the halves
    for (int i = 0; i < SET_ROWS; i++) { EQ(g.set_row, i % SET_ROWS); g.input(IN_NEXT, 0, 0); }
    EQ(g.set_row, 0);
    for (int i = 0; i < SET_SLOTS; i++) CHECK(SET_Y0 + (i + 1) * SET_ROW_H <= SCREEN_H);
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
    run(g, WASH_MS);
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
    g.take_sfx();                          // the cursor's own click
    int sounds = 0;
    for (int t = 0; t < VISIT_FIRST_MS + 1500; t += 100) if (g.tick(100) & F_SOUND) sounds++;
    EQ(g.visitor, 1);
    EQ(sounds, 0);                         // she arrives in silence
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
    g.input(IN_TOUCH, 252, 148);           // the house answers in the whole top-right corner
    g.input(IN_TOUCH, HOUSE_X0 - 1, 40); EQ(g.scene, SC_MUSIC);
    g.input(IN_TOUCH, 320, HOUSE_Y1 + 1); EQ(g.scene, SC_MUSIC);
    g.input(IN_TOUCH, HOUSE_X0, HOUSE_Y1); EQ(g.scene, SC_HOME);
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
    tap_menu(g, MENU_WASH); run(g, WASH_MS + 200);
    tap_menu(g, MENU_FEED); g.input(IN_TOUCH, 60, 200); run(g, ACTION_MS + 200);
    EQ(g.s.care_days, 1);                      // the same day counts once
    run(g, MEMORY_SHOW_MS + 200); CHECK(!g.memory_new);
    g.set_wall(at(2026, 11, 9, 9, 0));         // a week later: no streak lost, just the next picture
    tap_menu(g, MENU_FEED); g.input(IN_TOUCH, 60, 200); run(g, ACTION_MS + 200);
    EQ(g.s.care_days, 2);
    g.set_wall(at(2026, 11, 9, 9, 0), false);  // an untrusted clock awards nothing
    g.s.memory_day = 0;
    tap_menu(g, MENU_WASH); run(g, WASH_MS + 200);
    EQ(g.s.care_days, 2);
    g.set_wall(at(2026, 11, 9, 9, 0));
    g.s.memory_day = (u16)(at(2026, 11, 9, 9, 0) / 86400);
    g.set_wall(at(2026, 5, 1, 9, 0));          // the clock went back: restart from today, no punishment
    tap_menu(g, MENU_WASH); run(g, WASH_MS + 200);
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
    EQ(SAVE_SIZE, 128);
    Save s;
    EQ((int)((u8*)&s.memory_day - (u8*)&s), 36);   // older fields keep their place
    EQ((int)((u8*)&s.bed10 - (u8*)&s), 47);        // v7's bedtime sits in v6's (zero) pad byte
    EQ((int)((u8*)&s.wake_we10 - (u8*)&s), 51);    // v8's weekend wake-up sits in v7's (zero) spare byte
    EQ((int)((u8*)&s.r_day - (u8*)&s), 52);        // and the rest comes after v7's checksum
    EQ((int)((u8*)&s.check - (u8*)&s), 124);
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

// counts trick onsets; the gap to the previous onset is measured across calls (the state is kept)
struct TrickWatch { int last = IDLE_NONE; u32 prev = 0; bool any = false; int gaps = 0; };
static TrickWatch watch;
static int count_tricks(Game& g, int ms, int* kinds, u32* min_gap) {   // kinds: per trick
    int n = 0;
    for (int t = 0; t < ms / 100; t++) {
        g.tick(100);
        if (g.idle_act >= IDLE_HUM && watch.last < IDLE_HUM) {
            kinds[g.idle_act - IDLE_HUM]++;
            if (watch.any) { if (g.clock_ms - watch.prev < *min_gap) *min_gap = g.clock_ms - watch.prev; watch.gaps++; }
            watch.prev = g.clock_ms;
            watch.any = true;
            n++;
        }
        watch.last = g.idle_act;
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
        watch = TrickWatch();
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
        CHECK(watch.gaps >= 3);                // gaps were really measured, across the windows
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
    tap_set(g, 300, SET_CLOCK);   // SAAT: the clock & WiFi hub
    EQ(g.set_page, PG_HUB);
    tap_set(g, 300, HUB_CLOCK);
    EQ(g.set_page, PG_CLOCK);
}
static void leave_settings(Game& g) {      // from the hub (after KAYDET/GERİ) out of the settings
    if (g.set_page == PG_HUB) tap_set(g, 184, HUB_BACK);
    tap_set(g, 184, SET_EXIT);
}
static void tap_clock(Game& g, int row, int dir, int times = 1) {
    for (int i = 0; i < times; i++) tap_set(g, dir < 0 ? 60 : 300, row);
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
    EQ(h.month, 2); EQ(h.mday, 29); EQ(h.year, 2024);   // born on a real 29 February (a leap year)
    tap_set(f, 184, HUB_BACK);
    tap_set(f, 184, SET_EXIT);
    f.tick(0);                                 // the platform takes F_CLOCK and writes its RTC
    f.set_wall(at(2028, 2, 29, 10, 0)); run(f, 400);
    EQ(f.party, PARTY_REUNION);                // months later: first the welcome...
    run(f, PARTY_MS + 400);
    EQ(f.party, PARTY_BIRTHDAY);               // ...then the cake, on the 29th when there is one
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
    tap_set(s, 300, SET_SLEEP);
    EQ(s.set_page, 2);
    tap_set(s, 60, BEDR_BED);     // 19:30 -> 19:20
    tap_set(s, 300, BEDR_WAKE);   // 07:00 -> 07:10
    EQ(s.s.bed10, BED_DEFAULT10 - 1); EQ(s.s.wake10, WAKE_DEFAULT10 + 1);
    tap_set(s, 300, BEDR_ON);
    CHECK(s.s.opts & OPT_NO_BEDTIME);
    render(s, fb); CHECK(lit_pixels(0, 0, SCREEN_W, SCREEN_H) > 2000);
    tap_set(s, 300, BEDR_BACK);
    EQ(s.set_page, 0); EQ(s.set_row, SET_SLEEP);
    Game p = hatched();                        // a grown-up opens the settings while she sleeps at night
    at_time(p, 22, 0); p.tick(100); run(p, 300);
    EQ(p.scene, SC_SLEEP);
    p.input(IN_PARENT, 0, 0); EQ(p.scene, SC_SETTINGS);
    tap_set(p, 184, SET_EXIT);
    EQ(p.scene, SC_SLEEP); EQ(p.standing, 0);  // and closes them: she is still asleep, lying down
    Game z = hatched();                        // the settings do not restart a sleep (no second nap bonus)
    z.set_stat(ENERGY, 1000);
    tap_menu(z, MENU_SLEEP);
    run(z, NAP_MS + 200);
    EQ(z.stat(ENERGY), 1000 + NAP_BONUS + SLEEP_ENERGY_GAIN);
    for (int i = 0; i < 3; i++) {
        z.input(IN_SELECT, 0, 0); z.input(IN_PARENT, 0, 0); EQ(z.scene, SC_SETTINGS);
        tap_set(z, 184, SET_EXIT); EQ(z.scene, SC_SLEEP);
        run(z, NAP_MS + 200);
    }
    CHECK(z.stat(ENERGY) <= 1000 + NAP_BONUS + 5 * SLEEP_ENERGY_GAIN);
    Game n = hatched();                        // a nap at 18:00, the settings left open all night
    at_time(n, 18, 0); n.tick(100);
    n.set_stat(ENERGY, 1000);
    tap_menu(n, MENU_SLEEP); run(n, 300); EQ(n.scene, SC_SLEEP);
    n.input(IN_PARENT, 0, 0); EQ(n.scene, SC_SETTINGS);
    at_time(n, 22, 0); n.tick(100);
    at_time(n, 7, 30); n.set_wall(at(2026, 11, 4, 7, 30)); n.tick(100);
    tap_set(n, 184, SET_EXIT);
    EQ(n.scene, SC_HOME);                      // the night passed: good morning, not a nap until noon
}

TEST(fifth_pass_fixes) {
    Game g = hatched();                        // a daytime nap on the charger, the screen dark
    g.set_plugged(true); g.set_plugged(true);
    g.set_stat(ENERGY, MAX_STAT - 200);
    tap_menu(g, MENU_SLEEP); run(g, 300); EQ(g.scene, SC_SLEEP);
    g.screen_on = 0;
    g.tick(2 * 3600 * 1000u);                  // rested, she gets up: back onto the lounger, not home
    EQ(g.scene, SC_LOUNGE);
    u16 e = g.stat(ENERGY);
    g.tick(600000u);
    CHECK(g.stat(ENERGY) >= e);                // and the charger keeps her rested
    Game b = hatched();                        // an empty battery is not an unknown one
    render(b, fb); int none = lit_pixels(SCREEN_W - 60, 40, SCREEN_W, 74);
    b.set_battery(0); render(b, fb);
    CHECK(lit_pixels(SCREEN_W - 60, 40, SCREEN_W, 74) > none);
    b.set_battery(-1); render(b, fb);
    EQ(lit_pixels(SCREEN_W - 60, 40, SCREEN_W, 74), none);
    Game m = hatched();                        // asleep on the charger all night, the screen dark
    at_time(m, 22, 0); m.tick(100); run(m, 300);
    EQ(m.scene, SC_SLEEP);
    m.set_plugged(true); m.set_plugged(true);
    m.screen_on = 0;
    at_time(m, 7, 30); m.set_wall(at(2026, 11, 4, 7, 30)); m.tick(100);
    EQ(m.scene, SC_LOUNGE);                    // morning: onto the lounger, still charging
}

TEST(no_sound_at_bedtime) {
    Game g = hatched();
    at_time(g, 12, 0); g.tick(100); g.take_sfx();
    tap_menu(g, MENU_FEED);
    CHECK(g.take_sfx() != SFX_NONE);           // by day she makes her sounds
    Game n = hatched();
    at_time(n, 19, 45); n.tick(100); n.take_sfx();   // drowsy hour: already bedtime
    tap_menu(n, MENU_FEED);
    EQ(n.take_sfx(), SFX_NONE);
    CHECK(!(n.tick(100) & F_SOUND));
    at_time(n, 23, 0); n.tick(100); n.input(IN_SHAKE, 0, 0);
    EQ(n.take_sfx(), SFX_NONE);                // nor in the night
    Game o = hatched();                        // a parent who turned the bedtime off hears everything
    o.s.opts |= OPT_NO_BEDTIME; at_time(o, 23, 0); o.tick(100); o.take_sfx();
    tap_menu(o, MENU_FEED);
    CHECK(o.take_sfx() != SFX_NONE);
}

TEST(she_speaks_her_lines) {
    Game g = hatched();
    at_time(g, 12, 0); g.tick(100); g.take_sfx();
    int line = -1;
    tap_menu(g, MENU_FEED);
    g.take_sfx(&line);
    CHECK(line == V_NONE || line == V_YUM);     // the picker first, or straight to her snack
    run(g, 6000);
    g.take_sfx();
    int giggles = 0;                           // petting again and again: one giggle, not a chorus
    for (int i = 0; i < 20; i++) {
        tap_pet(g); g.tick(100);
        g.take_sfx(&line);
        giggles += line == V_GIGGLE;
        run(g, 300);
    }
    EQ(giggles, 1);
    run(g, 9000); g.take_sfx();                // one line between two of another: still not again within 8 s
    tap_pet(g); g.tick(100); g.take_sfx(&line); EQ(line, V_GIGGLE);
    run(g, 2000); g.take_sfx();
    g.voice_at = 0;                            // as if a different line had been said in between
    tap_pet(g); g.tick(100); g.take_sfx(&line);
    CHECK(line != V_GIGGLE);
    Game n = hatched();                        // at bedtime: not a word
    at_time(n, 19, 45); n.tick(100); n.take_sfx();
    tap_menu(n, MENU_FEED); run(n, 300);
    EQ(n.take_sfx(&line), SFX_NONE); EQ(line, V_NONE);
    Game m = hatched();                        // muted: not a word either
    at_time(m, 12, 0); m.tick(100); m.take_sfx();
    m.s.muted = 1;
    tap_menu(m, MENU_FEED);
    m.take_sfx(&line); EQ(line, V_NONE);
}

// every clip of a pack: found, checked, and its Opus packets add up to its length (20 ms each)
static int pack_clips(const u8* bin, u32 len, int langs, int per, bool strict = true) {
    int ok = 0;
    for (int lang = 0; lang < langs; lang++)
        for (int i = 0; i < per; i++) {
            Clip c;
            if (!pack_clip(bin, len, lang, i, &c)) continue;
            const u8* p;
            int n, packets = 0;
            while ((n = ogg_packet(&c, &p)) > 0) {
                CHECK(p >= c.data && p + n <= c.data + c.len);   // always: even a damaged pack stays inside
                if (strict) CHECK(n < 400);                       // 16 kbps, 20 ms: about 40 bytes
                packets++;
            }
            if (strict) {                  // a quarter to eight seconds, and the packets add up to that
                CHECK(c.samples > VOICE_RATE / 4 && c.samples < VOICE_RATE * 8);
                CHECK((u32)packets * 320 >= c.samples && (u32)packets * 320 < c.samples + 640 + c.pre_skip);
            }
            ok++;
        }
    return ok;
}

static u32 read_file(const char* path, u8* buf, u32 cap) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    u32 len = (u32)std::fread(buf, 1, cap, f);
    std::fclose(f);
    return len;
}

TEST(her_voice_file_decodes) {                // fw/data/voice.bin: every line in every language
    static u8 bin[4 << 20];
    u32 len = read_file("../fw/data/voice.bin", bin, sizeof bin);
    CHECK(len > 1000 && len < sizeof bin);
    EQ(bin[4] | bin[5] << 8, LANG_COUNT * (V_COUNT - 1)); EQ(bin[6] | bin[7] << 8, V_COUNT - 1);
    EQ(pack_clips(bin, len, LANG_COUNT, V_COUNT - 1), LANG_COUNT * (V_COUNT - 1));
    Clip c;
    CHECK(!pack_clip(bin, len, LANG_COUNT, 0, &c));    // out of range: nothing
    CHECK(!pack_clip(bin, len, 0, V_COUNT - 1, &c));
    CHECK(!pack_clip(bin, len, -1, 0, &c) && !pack_clip(bin, len, 0, -1, &c));
    CHECK(!pack_clip(bin, 100, 0, 0, &c));             // a cut-short file: nothing
    CHECK(!pack_clip(0, len, 0, 0, &c));
    // the private packs, when they are there (gitignored): the same checks
    static u8 priv[4 << 20];
    if (u32 n = read_file("../fw/personal/voice_name.bin", priv, sizeof priv)) EQ(pack_clips(priv, n, 1, NL_COUNT), NL_COUNT);
    if (u32 n = read_file("../fw/personal/routines_tr.bin", priv, sizeof priv)) EQ(pack_clips(priv, n, 1, R_CLIPS), R_CLIPS);
}

// A pack from a swapped SD card can hold anything: no damage may read outside it (ASan watches).
TEST(damaged_packs_never_read_outside) {
    static u8 bin[4 << 20];
    u32 len = read_file("../fw/data/voice.bin", bin, sizeof bin);
    if (!len) return;
    u32 cut = 8 + 8 * 4 + 3000;                          // a pack cut short: in the table, in a clip, mid-page
    for (u32 n = 0; n < cut && n < len; n += n < 64 ? 1 : 97) {
        u8* copy = (u8*)std::malloc(n ? n : 1);          // exactly n bytes: one past is an ASan error
        std::memcpy(copy, bin, n);
        pack_clips(copy, n, 1, 4, false);
        std::free(copy);
    }
    u32 rng = 12345;
    u8* copy = (u8*)std::malloc(len);
    for (int round = 0; round < 300; round++) {          // random bytes flipped in the table and the first clips
        std::memcpy(copy, bin, len);
        for (int k = 0; k < 8; k++) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            u32 at = rng % (round < 100 ? 64 : 12000);
            copy[at] = (u8)(rng >> 8);
        }
        pack_clips(copy, len, 2, V_COUNT - 1, false);
    }
    std::free(copy);
    u8 bad[40] = {'U', 'P', 'O', '1', 1, 0, 1, 0, 16, 0, 0, 0, 0xF0, 0xFF, 0xFF, 0xFF};   // a clip size that wraps
    Clip c;
    CHECK(!pack_clip(bad, sizeof bad, 0, 0, &c));
}

TEST(the_screen_dims_at_night) {
    Game g = hatched();
    at_time(g, 12, 0); g.tick(100);
    EQ(g.screen_brightness(), g.brightness());       // by day: the parent's level
    at_time(g, 19, 45); g.tick(100);
    CHECK(g.screen_brightness() <= g.brightness() / 5 + 1);   // bedtime: a glow
    CHECK(g.screen_brightness() >= 12);
    at_time(g, 23, 0); g.tick(100);
    CHECK(g.screen_brightness() < g.brightness());
    g.input(IN_PARENT, 0, 0); EQ(g.scene, SC_SETTINGS);
    EQ(g.screen_brightness(), g.brightness());       // a parent at night still reads the settings
    Game o = hatched();                              // bedtime switched off: it still dims after dark
    o.s.opts |= OPT_NO_BEDTIME; at_time(o, 23, 0); o.tick(100);
    CHECK(o.screen_brightness() < o.brightness());
}

TEST(old_local_break_deadlines_are_not_read_as_utc) {
    Game g = hatched();
    g.set_wall(at(2026, 11, 3, 11, 0));
    g.play_ms = g.play_limit_ms(); run(g, LEAVE_MS + 300); EQ(g.scene, SC_AWAY);
    u8 buf[SAVE_SIZE];
    g.save(buf);
    Save sv; std::memcpy(&sv, buf, SAVE_SIZE);
    CHECK(sv.life_flags & LIFE_UTC_AWAY);
    CHECK(sv.away_until);
    sv.life_flags &= (u8)~LIFE_UTC_AWAY;       // as an older firmware wrote it (a local deadline)
    std::memcpy(buf, &sv, SAVE_SIZE);
    u32 c = 2166136261u;
    for (int i = 0; i < SAVE_SIZE - 4; i++) c = (c ^ buf[i]) * 16777619u;
    std::memcpy(buf + SAVE_SIZE - 4, &c, 4);
    Game h; h.init(4);
    CHECK(h.load(buf, SAVE_SIZE));
    EQ(h.s.away_until, 0u);                    // dropped: what was left is counted on powered time
    EQ(h.scene, SC_AWAY);
    u32 left = h.away_ms;
    h.set_wall(at(2026, 11, 3, 11, 40));       // a UTC reading 40 minutes "later" changes nothing
    EQ(h.scene, SC_AWAY); EQ(h.away_ms, left);
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

static void tap_row(Game& g, int row) { tap_set(g, 300, row); }
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
        for (int p = PG_HUB; p <= PG_UPDATE; p++) { g.set_page = (u8)p; render(g, fb); }
    }
    Game q;                                            // the QR's text is escaped
    q.init(1);
    q.wifi_session("A;B", "p:w\\\"x");
    CHECK(q.qr_side > 0);
}

static int take_update(Game& g) {          // what the platform does with F_UPDATE
    u32 f = g.tick(0);
    if (!(f & F_UPDATE)) return UREQ_NONE;
    int r = g.update_req;
    g.update_req = UREQ_NONE;
    return r;
}
static u32 row_hash(Game& g, int row);

TEST(firmware_update_page) {
    Game g = hatched();
    g.fw_version = 3; g.set_battery(80);
    g.input(IN_PARENT, 0, 0);
    tap_row(g, SET_CLOCK); EQ(g.set_page, PG_HUB);
    u32 plain = row_hash(g, HUB_UPDATE);
    tap_row(g, HUB_UPDATE); EQ(g.set_page, PG_UPDATE); EQ(g.update_view(), UST_IDLE);
    shot(g, "update_idle");
    tap_button(g, 1);                                   // KONTROL ET
    EQ(g.update_stage, UST_CHECKING); EQ(take_update(g), UREQ_CHECK); EQ(take_update(g), UREQ_NONE);
    shot(g, "update_checking");
    run(g, SCREEN_OFF_MS + 2000); CHECK(g.screen_on);   // lit while it checks
    tap_button(g, 1); EQ(take_update(g), UREQ_NONE);    // nothing more to ask meanwhile
    g.update_status(UST_LATEST, 0, 3); EQ(g.update_view(), UST_LATEST); CHECK(!g.update_waiting());
    shot(g, "update_latest");
    tap_button(g, 1); EQ(take_update(g), UREQ_CHECK);   // check again
    g.update_status(UST_AVAILABLE, 0, 4); EQ(g.update_view(), UST_AVAILABLE); CHECK(g.update_waiting());
    shot(g, "update_available");
    g.set_battery(49); CHECK(!g.update_allowed());      // the battery rule: below 50 % and no cable
    shot(g, "update_blocked");
    tap_button(g, 1); EQ(take_update(g), UREQ_NONE); EQ(cue_after(g), SFX_REFUSE);
    g.input(IN_NEXT, 0, 0); g.input(IN_SELECT, 0, 0);   // PWR has only GERİ left
    EQ(g.set_page, PG_HUB); EQ(take_update(g), UREQ_NONE);
    g.set_row = HUB_CLOCK;                              // the same highlight as before
    CHECK(row_hash(g, HUB_UPDATE) != plain);            // the hub marks the waiting version
    g.set_battery(-1); CHECK(!g.update_allowed());      // unknown battery, no cable: no
    g.set_battery(50); CHECK(g.update_allowed());
    g.set_battery(10); g.set_plugged(true); g.set_plugged(true); CHECK(g.update_allowed());   // the cable: yes
    tap_row(g, HUB_UPDATE); EQ(g.update_view(), UST_AVAILABLE);   // a known newer version is offered at once
    tap_button(g, 1);                                   // YÜKLE
    EQ(take_update(g), UREQ_INSTALL); EQ(g.update_stage, UST_DOWNLOADING);
    g.update_status(UST_DOWNLOADING, 42, 0); EQ(g.update_ver, 4); EQ(g.update_pct, 42);
    shot(g, "update_download");
    g.input(IN_TOUCH, 90, (WBTN_Y0 + WBTN_Y1) / 2); g.input(IN_SELECT, 0, 0); g.input(IN_NEXT, 0, 0);
    EQ(g.set_page, PG_UPDATE); EQ(g.scene, SC_SETTINGS); EQ(take_update(g), UREQ_NONE);   // no way out mid-way
    g.tick(0);
    CHECK(!(g.tick(100) & F_REDRAW));                   // a still page while flash is written...
    g.update_status(UST_DOWNLOADING, 43, 0);
    CHECK(g.tick(0) & F_REDRAW);                        // ...redrawn when the bar moves
    run(g, SCREEN_OFF_MS + 2000); CHECK(g.screen_on);
    g.update_status(UST_VERIFYING, 100, 0); shot(g, "update_verify");
    g.update_status(UST_RESTARTING, 100, 0); shot(g, "update_restart");
    g.update_stage = UST_IDLE;
    const int errs[] = {UST_NO_WIFI, UST_FAILED, UST_BAD_SIG};
    for (int st : errs) {                               // each error offers TEKRAR: a fresh check
        g.update_status(st, 0, 0); EQ(g.update_view(), st);
        tap_button(g, 1); EQ(take_update(g), UREQ_CHECK); EQ(g.update_stage, UST_CHECKING);
    }
    shot(g, "update_checking2");
    g.update_status(UST_BAD_SIG, 0, 0); shot(g, "update_bad_sig");
    tap_button(g, 0); EQ(g.set_page, PG_HUB);           // GERİ; opened again, the stale error is gone
    tap_row(g, HUB_UPDATE); EQ(g.update_view(), UST_AVAILABLE);
    tap_button(g, 1); take_update(g);                   // a check under way when the grown-up leaves...
    g.update_stage = UST_CHECKING;
    tap_button(g, 0); tap_row(g, HUB_BACK); tap_set(g, 184, SET_EXIT);
    EQ(g.scene, SC_HOME);
    g.input(IN_PARENT, 0, 0); EQ(g.set_page, PG_UPDATE);   // ...the gesture goes straight back to it
    Game m = hatched();                                 // the main list marks its SAAT row too
    m.set_wall(at(2026, 11, 3, 11, 0)); m.fw_version = 1;
    m.input(IN_PARENT, 0, 0);
    u32 before = row_hash(m, SET_CLOCK);
    m.update_ver = 2;
    CHECK(row_hash(m, SET_CLOCK) != before);
    EQ(text_overflows(), 0);
}

static void copy_name_test(Game& g) {
    if (!g.wifi_ap[0]) g.wifi_session("Unicorn-A7C2", "kq7m4xtz9p2w");
    for (int i = 0; i < 20; i++) g.wifi_net[i] = "Ev WiFi 2.4 GHz Modem"[i];
}

static u32 region_hash(int x0, int y0, int x1, int y1) {
    u32 h = 2166136261u;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) h = (h ^ fb[y * SCREEN_W + x]) * 16777619u;
    return h;
}

static u32 row_hash(Game& g, int row) {   // the pixels of one settings row as it is drawn
    render(g, fb);
    int y = SET_Y0 + set_row_slot(g.set_rows(), row) * SET_ROW_H;
    return region_hash(16, y, SCREEN_W - 16, y + SET_ROW_H);
}

TEST(text_edge_cases) {
    Game a = hatched();                        // a 4-byte character after a longer name in the same slot
    a.wifi_list_clear(); a.wifi_list_add("ABCDEFGHIJKLMNOPQRSTUVWXYZ123456", false);
    a.wifi_list_clear(); a.wifi_list_add("\xF0\x9F\x8C\x88", false);   // a rainbow emoji
    Game b = hatched();
    b.wifi_list_clear(); b.wifi_list_add("\xF0\x9F\x8C\x88", false);
    for (Game* g : {&a, &b}) { g->input(IN_PARENT, 0, 0); g->set_page = PG_WLIST; g->set_row = 1; }
    render(a, fb); u32 ha = region_hash(16, SET_Y0, SCREEN_W - 16, SET_Y0 + SET_ROW_H);
    render(b, fb); u32 hb = region_hash(16, SET_Y0, SCREEN_W - 16, SET_Y0 + SET_ROW_H);
    EQ(ha, hb);                                // no stale letters after the emoji
    Game c = hatched();                        // long names that differ only at the end look different
    c.wifi_list_clear();
    c.wifi_list_add("ABCDEFGHIJKLMNOPQRSTU1", false); c.wifi_list_add("ABCDEFGHIJKLMNOPQRSTU2", false);
    c.input(IN_PARENT, 0, 0); c.set_page = PG_WLIST; c.set_row = 2;
    render(c, fb);
    CHECK(region_hash(16, SET_Y0, SCREEN_W - 16, SET_Y0 + SET_ROW_H) !=
          region_hash(16, SET_Y0 + SET_ROW_H, SCREEN_W - 16, SET_Y0 + 2 * SET_ROW_H));
    EQ(text_overflows(), 0);
    const char* broken[] = {"\xC3", "A\xE2\x82", "\xF0\x9F", "\xFF\xFE", "\xC3\xC3\xC3"};   // cut-off UTF-8
    for (const char* s : broken) { c.wifi_list_clear(); c.wifi_list_add(s, false); render(c, fb); }
}

// Every scene, page and overlay in every language: no text may run off the screen.
static int overflow_scan(int lang, char* where, int where_n) {
    int total = 0;
    auto check = [&](Game& g, const char* what) {
        render(g, fb);
        int n = text_overflows();
        if (n && std::getenv("OVERFLOW_DEBUG")) std::printf("      lang %d %s: %d\n", lang, what, n);
        if (n && !total) std::snprintf(where, where_n, "%s", what);
        total += n;
    };
    Game g = hatched();
    g.s.lang = (u8)lang;
    g.set_wall(at(2026, 11, 3, 11, 0));
    g.wifi_list_clear(); g.wifi_list_add("HomeNet", true); g.wifi_list_add("ABCDEFGHIJKLMNOPQRSTUVWXYZ123456", false);
    g.s.care_days = 250; g.s.milestones = 15; g.s.tricks_seen = TRICKS;
    const int scenes[] = {SC_EGG, SC_HATCH, SC_HOME, SC_EAT, SC_PLAY, SC_WASH, SC_SLEEP, SC_INFO, SC_REFUSE, SC_GAME,
                          SC_LEAVE, SC_AWAY, SC_RETURN, SC_FEED, SC_EGG_PICK, SC_TOYS, SC_MUSIC, SC_CLOUD, SC_DRESS,
                          SC_ALBUM, SC_LOUNGE};
    for (int sc : scenes) {
        for (int t = 0; t < 4; t++) { g.scene = (u8)sc; g.scene_ms = (u32)t * 1300; g.anim_ms = g.scene_ms; check(g, "scene"); }
    }
    g.scene = SC_HOME;
    for (int p = PARTY_REUNION; p <= PARTY_MILESTONE; p++)
        for (int a = 0; a < MILESTONES; a++) { g.party = (u8)p; g.party_arg = (u8)a; g.party_end = g.clock_ms + 1000; check(g, "party"); }
    g.party = 0; g.greeting = 1; g.greet_end = g.clock_ms + 1000; check(g, "greeting"); g.greeting = 0;
    g.set_wall(at(2026, 11, 3, 19, 45)); check(g, "drowsy");
    g.scene = SC_SLEEP; g.standing = 1; check(g, "standing"); g.standing = 0;
    g.scene = SC_SETTINGS;
    for (int pg = PG_MAIN; pg <= PG_UPDATE; pg++)
        for (int st = WST_OFF; st <= WST_TIMEOUT; st++) {
            g.set_page = (u8)pg; g.wifi_stage = (u8)st; g.wifi_arg = 1;
            copy_name_test(g);
            char what[32];
            std::snprintf(what, sizeof what, "settings page %d stage %d", pg, st);
            check(g, what);
        }
    g.set_page = PG_UPDATE; g.wifi_stage = WST_OFF;  // every update stage, battery rule both ways, a long version
    g.fw_version = 1234; g.update_ver = 9999;
    for (int st = UST_IDLE; st <= UST_BAD_SIG; st++)
        for (int ok = 0; ok < 2; ok++) {
            g.update_stage = (u8)st; g.update_pct = 57; g.plugged = (u8)ok;
            char what[32];
            std::snprintf(what, sizeof what, "update stage %d plugged %d", st, ok);
            check(g, what);
            g.set_page = PG_HUB; check(g, "hub with an update"); g.set_page = PG_MAIN; check(g, "main with an update");
            g.set_page = PG_UPDATE;
        }
    g.update_ver = 0; g.update_stage = UST_IDLE;
    for (int pg = PG_ROUTINE; pg <= PG_HABITS; pg++)   // the daily routine pages, with and without the pack
        for (int has = 0; has < 2; has++) {
            g.set_page = (u8)pg; g.has_routines = (u8)has; g.s.week[g.today().day % 7] = 0x7FF; g.s.r_day = g.today().day;
            check(g, "routine page");
        }
    g.scene = SC_ASK;                          // every question card, every stage, a yes and a no
    for (int q = 0; q < Q_COUNT; q++)
        for (int st = RS_ASK; st <= RS_REPLY; st++)
            for (int a = ANS_YES; a <= ANS_NO; a++) {
                g.r_q = (u8)q; g.r_stage = (u8)st; g.s.r_ans[q] = (u16)(a << 11);
                check(g, "question card");
            }
    g.scene = SC_SETTINGS;
    Game e = picked(7);                        // an egg: its own words on the clock page ("after hatching")
    e.s.lang = (u8)lang;
    e.input(IN_PARENT, 0, 0);
    for (int pg = PG_MAIN; pg <= PG_HABITS; pg++) { e.set_page = (u8)pg; e.wifi_stage = WST_WAIT; copy_name_test(e); check(e, "egg settings"); }
    return total;
}

TEST(every_language_fits_the_screen) {
    for (int l = 0; l < LANG_COUNT; l++) {
        char where[32] = "";
        int n = overflow_scan(l, where, sizeof where);
        if (n) std::printf("    language %d: %d overflows (first in %s)\n", l, n, where);
        EQ(n, 0);
    }
}

TEST(review_fixes_bug_hunt) {
    Game g = hatched();                        // a save with a nonsense volume is refused, not a crash later
    u8 buf[SAVE_SIZE]; g.s.volume = 200; g.save(buf);
    Game v; v.init(1); CHECK(!v.load(buf, SAVE_SIZE));
    Game e = picked(3);                        // WiFi setup opened on an egg: "play on" goes back to the egg
    e.input(IN_PARENT, 0, 0);
    e.set_page = PG_WSESSION; e.wifi_stage = WST_WAIT;
    e.input(IN_TOUCH, 280, (WBTN_Y0 + WBTN_Y1) / 2);
    EQ(e.scene, SC_EGG);
    Game w = hatched();                        // a timed-out setup lets the screen sleep
    w.input(IN_PARENT, 0, 0); w.set_page = PG_WSESSION; w.wifi_status(WST_TIMEOUT, 0);
    run(w, SCREEN_OFF_MS + 500); CHECK(!w.screen_on);
    Game n = hatched();                        // the cable at night leaves the room dark
    n.set_wall(at(2026, 11, 3, 20, 0)); n.s.opts |= OPT_NO_BEDTIME; n.set_plugged(false);
    run(n, SCREEN_OFF_MS + 500); CHECK(!n.screen_on);
    n.set_plugged(true); n.set_plugged(true);
    CHECK(!n.screen_on); EQ(n.scene, SC_LOUNGE);
    n.set_plugged(false); n.set_plugged(false);
    CHECK(!n.screen_on);
    Game l = hatched();                        // the goodbye is saved the moment it starts
    l.play_ms = l.play_limit_ms();
    u32 f = l.tick(100);
    EQ(l.scene, SC_LEAVE); CHECK(f & F_SAVE);
    while (l.scene == SC_LEAVE) f = l.tick(100);
    EQ(l.scene, SC_AWAY); CHECK(f & F_SAVE);   // and again once she is out
    Game p = hatched();                        // petting gains get saved (at most once a minute)
    p.tick(0);
    p.input(IN_TOUCH, 184 + p.pet_x, 200);
    CHECK(p.tick(0) & F_SAVE);
    run(p, PET_COOLDOWN_MS + 100); p.input(IN_TOUCH, 184 + p.pet_x, 200);
    EQ(p.tick(0) & F_SAVE, 0u);
    run(p, 60000); p.input(IN_SELECT, 0, 0); p.input(IN_TOUCH, 184 + p.pet_x, 200);
    CHECK(p.tick(0) & F_SAVE);
    Game b = hatched();                        // BOOT still works after 25+ days without a press
    boot_press(b, 80, 500); EQ(b.menu, 1);
    b.tick(0x80000000u);
    b.input(IN_TOUCH, 184, 200);               // wake the screen
    boot_press(b, 80, 500); EQ(b.menu, 2);
    Game c = hatched();                        // ...and so does the petting reward
    c.input(IN_TOUCH, 184 + c.pet_x, 200);
    c.tick(0x80000000u);
    c.input(IN_TOUCH, 184, 200);
    int h0 = c.stat(HAPPY);
    c.input(IN_TOUCH, 184 + c.pet_x, 200);
    CHECK(c.stat(HAPPY) > h0);
    Game a = hatched(), a2 = hatched();        // a huge tick is not lost to an overflow
    run(a, SCREEN_OFF_MS + 500); run(a2, SCREEN_OFF_MS + 500);
    a.acc_ms = 1; a2.acc_ms = 1;
    a.tick(0xffffffffu);
    a2.tick(0x7fffffffu); a2.tick(0x80000000u);
    EQ(a.s.age_s, a2.s.age_s); CHECK(a.s.age_s > 4000000);
    Game q;                                    // a hello does not use up play time
    q.init(5); u8 sv[SAVE_SIZE]; Game src = hatched(); src.save(sv); CHECK(q.load(sv, SAVE_SIZE));
    CHECK(q.greeting);
    u32 before = q.play_ms; q.tick(1000);
    EQ(q.play_ms, before);
    Game d = hatched();                        // three dreams a night, never a repeat
    d.set_stat(ENERGY, 1000);
    tap_menu(d, MENU_SLEEP);
    int seen[DREAMS + 1] = {0}, shown = 0;
    for (int t = 0; t < 60000; t += 100) {
        d.idle_ms = 0;                         // someone keeps looking
        d.tick(100);
        int now = d.dream();
        if (now && (!shown || seen[shown - 1] != now)) { CHECK(shown < DREAMS); if (shown < DREAMS + 1) seen[shown++] = now; }
    }
    EQ(shown, DREAMS);
}

TEST(review_fixes_third_pass) {
    Game g = hatched();                        // summer time: the local clock jumps an hour, the break does not
    g.s.opts |= OPT_NO_BEDTIME;                // (01:50 is past bedtime: she would sleep instead)
    g.set_wall(at(2027, 3, 28, 1, 50), true, at(2027, 3, 28, 0, 50));
    g.play_ms = g.play_limit_ms(); run(g, LEAVE_MS + 300);
    EQ(g.scene, SC_AWAY);
    g.set_wall(at(2027, 3, 28, 3, 0), true, at(2027, 3, 28, 1, 0));   // ten minutes later, local 03:00
    run(g, 200);
    CHECK(g.away_ms >= 19 * 60000);            // twenty minutes still to go
    g.set_wall(at(2027, 3, 28, 2, 55), true, at(2027, 3, 28, 0, 55));   // the clock goes back five minutes
    u32 left = g.away_ms;
    run(g, 200);
    CHECK(g.away_ms <= left);                  // ...and the break does not grow
    Game b = hatched();                        // time is up at bedtime: a touch puts her to sleep, not out
    b.set_wall(at(2026, 11, 3, 19, 40)); settle(b);
    b.play_ms = b.play_limit_ms();
    b.input(IN_TOUCH, 184, 60);
    CHECK(b.scene == SC_SLEEP);
    Game m = hatched();                        // the morning comes while the settings are open
    m.set_stat(ENERGY, 1000);
    m.set_wall(at(2026, 11, 3, 22, 0)); m.tick(100); run(m, 300);
    EQ(m.scene, SC_SLEEP);
    m.set_wall(at(2026, 11, 4, 6, 59)); run(m, 300);
    m.input(IN_SELECT, 0, 0); m.input(IN_PARENT, 0, 0); EQ(m.scene, SC_SETTINGS);
    m.set_wall(at(2026, 11, 4, 7, 5)); run(m, 300);
    tap_set(m, 184, SET_EXIT);
    run(m, 300);
    EQ(m.scene, SC_HOME);                      // up for the day, not stuck asleep
    Game r = hatched();                        // a long album title stays clear of the house button
    r.s.lang = LANG_RU; r.s.care_days = 13;
    tap_header(r); r.input(IN_TOUCH, 70, 30); EQ(r.scene, SC_ALBUM);
    r.book = 1; render(r, fb);
    EQ(text_overflows(), 0);
}

// ---- rendering --------------------------------------------------------------------------------

// Found by the monkey test (monkey.cpp): a grown-up who opens the settings and walks away left the toy
// in them for good, and the child woke it into the parent settings. They now close once the screen is
// dark (not while the radio is in use), and a sleep that ended meanwhile ends quietly in the dark.
TEST(settings_close_when_the_grown_up_walks_away) {
    Game g = hatched(61);
    g.set_plugged(false);                      // the first sample only says how things are
    g.input(IN_PARENT, 0, 0);
    EQ(g.scene, SC_SETTINGS);
    run(g, SCREEN_OFF_MS + 200);
    CHECK(!g.screen_on); EQ(g.scene, SC_HOME);
    g.input(IN_TOUCH, 300, SET_Y0 + 18);       // the child's first touch only wakes the screen: at home
    CHECK(g.screen_on); EQ(g.scene, SC_HOME); EQ(g.s.volume, VOL_NORMAL);
    g.wifi_session("Unicorn-AB12", "pw123456");   // a WiFi setup under way keeps them open (and lit)
    g.input(IN_PARENT, 0, 0);
    EQ(g.set_page, PG_WSESSION);
    run(g, SCREEN_OFF_MS + 5000);
    CHECK(g.screen_on); EQ(g.scene, SC_SETTINGS);
    g.wifi_status(WST_TIMEOUT, 0);             // the radio gave up (fw: SESSION_MS); nobody reads the result
    run(g, SCREEN_OFF_MS + 200);
    CHECK(!g.screen_on); EQ(g.scene, SC_HOME);
    EQ(g.wifi_stage, WST_OFF); EQ(g.wifi_req, WREQ_STOP);   // closed as if TAMAM was tapped
    g.wifi_req = WREQ_NONE;
    plug(g, true);                             // opened from the lounger and closed in the dark: back on it
    run(g, LOUNGE_IDLE_MS + 200);
    EQ(g.scene, SC_LOUNGE);
    g.input(IN_PARENT, 0, 0);
    run(g, SCREEN_OFF_MS + 200);
    EQ(g.scene, SC_LOUNGE);

    Game h = hatched(62);                      // opened from her night sleep; morning comes meanwhile
    h.set_wall(at(2026, 11, 3, 21, 0));
    run(h, 200);
    EQ(h.scene, SC_SLEEP);
    h.input(IN_PARENT, 0, 0);
    EQ(h.scene, SC_SETTINGS);
    h.set_wall(at(2026, 11, 4, 7, 5));
    h.take_sfx();
    run(h, SCREEN_OFF_MS + 200);
    EQ(h.scene, SC_HOME); CHECK(!h.screen_on);
    EQ(h.take_sfx(), SFX_NONE);                // no wake-up tune in a dark room
}

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


// ---- her daily routine ----------------------------------------------------------------------------
struct Asked { int q, minute, clip; };
static Game routine_game(u32 seed = 42) {
    Game g = hatched(seed);
    g.has_routines = 1;
    g.hint_pet = 0;
    return g;
}
// The platform's half: when she asks, it listens and reports `ans` (ANS_NONE: it never answers).
// Runs the toy second by second over [from, to) of the wall clock; returns the questions asked.
static int live(Game& g, u32 from, u32 to, int ans, Asked* log = 0, int max = 0, int* clips = 0) {
    int n = 0;
    for (u32 t = from; t < to; t += 60) {
        g.set_wall(t);
        for (int sec = 0; sec < 60; sec++) {
            u32 f = g.tick(1000);
            int line, clip;
            if (f & F_SOUND) { g.take_sfx(&line, &clip); if (clip >= 0 && clips) clips[clip]++; }
            if ((f & F_LISTEN) && g.listen_req == LREQ_LISTEN && g.listen_kind <= LK_OPEN) {
                g.listen_req = LREQ_NONE;
                if (log && n < max) log[n] = {g.r_q, (int)(t % 86400 / 60), 0};
                n++;
                if (ans) { g.listen_started(); g.listen_result(ans); }
            } else if (f & F_LISTEN) g.listen_req = LREQ_NONE;
        }
    }
    return n;
}
static int gaps_ok(const Asked* a, int n) {   // every question at least Q_GAP_MIN after the previous
    for (int i = 1; i < n; i++) if (a[i].minute - a[i - 1].minute < Q_GAP_MIN) return 0;
    return 1;
}

TEST(routine_school_morning) {
    Game g = routine_game();                   // Tuesday 3 November 2026, wake-up at 07:00
    Asked a[32];
    int clips[R_CLIPS] = {0};
    int n = live(g, at(2026, 11, 3, 6, 30), at(2026, 11, 3, 9, 0), ANS_YES, a, 32, clips);
    EQ(n, 6);                                  // teeth, dress, school clothes, hair, vitamin, breakfast
    u16 want = 1 << Q_TEETH_AM | 1 << Q_DRESS | 1 << Q_SCHOOL_CLOTHES | 1 << Q_HAIR | 1 << Q_VITAMIN | 1 << Q_BREAKFAST;
    EQ(g.s.r_asked, want);
    for (int i = 0; i < n; i++) CHECK(a[i].minute >= 7 * 60 + 3 && a[i].minute < 8 * 60);   // in her first hour
    CHECK(gaps_ok(a, n));
    EQ(g.stars_today(), 6);
    int greet = 0;
    for (int i = 0; i < R_VARIANTS; i++) greet += clips[R_GREET + i];
    EQ(greet, 1);                              // good morning, once
    for (int q = 0; q < Q_YESNO; q++) {
        int asks = 0, yes = 0;
        for (int i = 0; i < R_VARIANTS; i++) { asks += clips[Q_ASK[q] + i]; yes += clips[Q_YES[q] + i]; }
        bool expected = want >> q & 1;
        EQ(asks, expected ? 1 : 0); if (q != Q_TEETH_PM) EQ(yes, expected ? 1 : 0);
    }
    Game b = routine_game(43);                 // another day, another order and other moments
    Asked a2[32];
    live(b, at(2026, 11, 4, 6, 30), at(2026, 11, 4, 9, 0), ANS_YES, a2, 32);
    bool differs = false;
    for (int i = 0; i < 6; i++) differs |= a[i].q != a2[i].q || a[i].minute != a2[i].minute;
    CHECK(differs);
}

TEST(routine_weekend_and_holidays) {
    Game g = routine_game();                   // Saturday 7 November: she sleeps till 08:00
    Asked a[32];
    int n = live(g, at(2026, 11, 7, 6, 30), at(2026, 11, 7, 9, 30), ANS_YES, a, 32);
    EQ(n, 5);                                  // no school clothes
    CHECK(!(g.s.r_asked & 1 << Q_SCHOOL_CLOTHES));
    for (int i = 0; i < n; i++) CHECK(a[i].minute >= 8 * 60 + 3 && a[i].minute < 9 * 60);
    at_time(g, 7, 30); g.set_wall(at(2026, 11, 7, 7, 30)); EQ(g.bedtime(), BED_SLEEP);   // still her night at 07:30
    n = live(g, at(2026, 11, 7, 15, 0), at(2026, 11, 7, 16, 30), ANS_YES, a, 32);
    EQ(n, 1); EQ(a[0].q, Q_WATER); CHECK(a[0].minute >= 15 * 60 + 30 && a[0].minute < 16 * 60);
    Game h = routine_game();                   // holiday mode on a Wednesday: like a weekend day
    h.s.opts |= OPT_HOLIDAY;
    n = live(h, at(2026, 11, 4, 7, 30), at(2026, 11, 4, 16, 0), ANS_YES, a, 32);
    EQ(n, 6); CHECK(h.s.r_asked & 1 << Q_WATER); CHECK(!(h.s.r_asked & 1 << Q_SCHOOL_CLOTHES));
    Game r = routine_game();                   // a holiday range from the parent page
    r.s.hol[1][0] = (u16)(at(2026, 11, 2, 0, 0) / 86400); r.s.hol[1][1] = (u16)(at(2026, 11, 6, 0, 0) / 86400);
    r.set_wall(at(2026, 11, 4, 12, 0)); CHECK(r.holiday()); EQ(r.wake_min(), WAKE_WE_DEFAULT10 * 10);
    r.set_wall(at(2026, 11, 9, 12, 0)); CHECK(!r.holiday()); EQ(r.wake_min(), WAKE_DEFAULT10 * 10);
}

TEST(routine_evening) {
    Game g = routine_game();                   // bedtime 19:30: evening questions 18:00-19:00, then the happy one
    Asked a[32];
    int clips[R_CLIPS] = {0};
    int n = live(g, at(2026, 11, 3, 16, 0), at(2026, 11, 3, 19, 40), ANS_YES, a, 32, clips);
    EQ(n, 5);                                  // dinner, teeth, toys, toilet, what made you happy
    CHECK(gaps_ok(a, n));
    int toilet = -1, happy = -1, dinner = -1;
    for (int i = 0; i < n; i++) {
        if (a[i].q == Q_TOILET) toilet = i;
        if (a[i].q == Q_HAPPY) happy = i;
        if (a[i].q == Q_DINNER) dinner = i;
        if (a[i].q == Q_TEETH_PM || a[i].q == Q_TOYS || a[i].q == Q_TOILET) CHECK(a[i].minute >= 18 * 60 && a[i].minute < 19 * 60);
    }
    CHECK(dinner >= 0 && a[dinner].minute >= 18 * 60 && a[dinner].minute < 18 * 60 + 30);
    EQ(toilet, 3);                             // the last of the evening, after dinner, teeth and toys
    EQ(happy, 4); CHECK(a[happy].minute >= 19 * 60 && a[happy].minute < 19 * 60 + 25);
    int reply = 0;                             // "yes" to the open question is a happy reply, not a star
    for (int i = 0; i < R_VARIANTS; i++) reply += clips[R_HAPPY_REPLY + i];
    EQ(reply, 1); EQ(g.stars_today(), 4);
    Game s = routine_game();                   // silence: a kind good night, and no second try
    live(s, at(2026, 11, 3, 18, 55), at(2026, 11, 3, 19, 40), ANS_SILENT, a, 32);
    CHECK(s.s.r_done & 1 << Q_HAPPY); EQ(s.s.r_ans[Q_HAPPY] >> 11, ANS_SILENT);
}

TEST(routine_asks_again_once) {
    Game g = routine_game();
    g.s.q_off = (u16)~(1 << Q_DINNER) & ((1 << Q_COUNT) - 1);   // only dinner
    Asked a[8];
    int n = live(g, at(2026, 11, 3, 17, 50), at(2026, 11, 3, 19, 0), ANS_UNCLEAR, a, 8);
    EQ(n, 2);                                  // unclear: once more, ten minutes later, then never again
    CHECK(a[1].minute - a[0].minute >= Q_REASK_MIN);
    CHECK(g.s.r_done & 1 << Q_DINNER); EQ(g.stars_today(), 0);
    Game y = routine_game();
    y.s.q_off = g.s.q_off;
    n = live(y, at(2026, 11, 3, 17, 50), at(2026, 11, 3, 19, 0), ANS_NO, a, 8);
    EQ(n, 1);                                  // a clear no is an answer
    EQ(y.s.r_ans[Q_DINNER] >> 11, ANS_NO);
    Game p = routine_game();                   // a power cut right after the question: not asked again
    p.s.q_off = g.s.q_off;
    n = live(p, at(2026, 11, 3, 17, 50), at(2026, 11, 3, 18, 15), ANS_YES, a, 8);
    EQ(n, 1);
    u8 buf[SAVE_SIZE]; p.save(buf);
    Game q; q.init(9); q.set_wall(at(2026, 11, 3, 18, 15)); CHECK(q.load(buf, SAVE_SIZE)); q.has_routines = 1;
    EQ(live(q, at(2026, 11, 3, 18, 15), at(2026, 11, 3, 19, 0), ANS_YES, a, 8), 0);
    EQ(q.stars_today(), 1);
    EQ(live(q, at(2026, 11, 4, 17, 50), at(2026, 11, 4, 19, 0), ANS_YES, a, 8), 1);   // the next day: again
    EQ(q.stars_today(), 1);
}

TEST(routine_stays_quiet) {
    Asked a[32];
    u32 from = at(2026, 11, 3, 5, 0), to = at(2026, 11, 3, 22, 0);
    Game m = routine_game(); m.s.volume = VOL_OFF;                 // volume off
    EQ(live(m, from, to, ANS_YES, a, 32), 0);
    Game u = routine_game();                                       // an untrusted clock
    int n = 0;
    for (u32 t = from; t < to; t += 600) { u.set_wall(t, false); for (int k = 0; k < 600; k++) n += (u.tick(1000) & F_LISTEN) != 0; }
    EQ(n, 0); EQ(u.s.r_asked, 0);
    Game o = routine_game(); o.s.opts |= OPT_NO_ROUTINES;          // switched off
    EQ(live(o, from, to, ANS_YES, a, 32), 0);
    Game x = routine_game(); x.has_routines = 0;                   // no routines_tr.bin: the game as before
    EQ(live(x, from, to, ANS_YES, a, 32), 0);
    Game b = routine_game(); b.s.bed10 = 108;                      // bedtime 18:00: nothing at bedtime
    n = live(b, from, to, ANS_YES, a, 32);
    for (int i = 0; i < n; i++) CHECK(a[i].minute < 18 * 60 && a[i].minute >= 7 * 60);
    Game w = routine_game();                   // busy: she waits for the bath and the stars to end
    w.s.q_off = (u16)~(1 << Q_DINNER) & ((1 << Q_COUNT) - 1);
    w.set_wall(at(2026, 11, 3, 17, 59)); run(w, 1000);
    tap_menu(w, MENU_WASH);
    w.set_wall(at(2026, 11, 3, 18, 0)); run(w, 1000);
    EQ(w.scene, SC_WASH); EQ(w.s.r_asked, 0);
    run(w, WASH_MS + MEMORY_SHOW_MS + 500);    // (the day's first care puts a picture in the album first)
    EQ(w.scene, SC_ASK);                       // right after
    Game d = routine_game();                   // the screen was dark: lit for her question, dark again soon
    d.s.q_off = w.s.q_off;
    d.set_wall(at(2026, 11, 3, 17, 50)); run(d, SCREEN_OFF_MS + 1000); CHECK(!d.screen_on);
    d.set_wall(at(2026, 11, 3, 18, 0)); d.tick(1000);
    EQ(d.scene, SC_ASK); CHECK(d.screen_on);
    d.listen_started(); d.listen_result(ANS_YES); run(d, R_REPLY_MS + 200);
    EQ(d.scene, SC_HOME); run(d, R_DARK_LINGER_MS + 300); CHECK(!d.screen_on);
}

TEST(routine_question_flow) {
    Game g = routine_game();
    g.s.q_off = (u16)~(1 << Q_DINNER) & ((1 << Q_COUNT) - 1);
    g.set_wall(at(2026, 11, 3, 18, 0));
    u32 f = g.tick(100);
    EQ(g.scene, SC_ASK); EQ(g.r_q, Q_DINNER); EQ(g.r_stage, RS_ASK);
    CHECK(f & F_LISTEN); EQ(g.listen_req, LREQ_LISTEN); EQ(g.listen_kind, LK_YESNO); EQ(g.listen_q, Q_DINNER);
    int line, clip;
    g.take_sfx(&line, &clip);
    CHECK(clip >= Q_ASK[Q_DINNER] && clip < Q_ASK[Q_DINNER] + R_VARIANTS);
    EQ(g.link(), LINK_NOW);                    // the join starts while she is still asking
    g.listen_req = LREQ_NONE;
    g.listen_started(); EQ(g.r_stage, RS_LISTEN);
    shot(g, "29_question");
    g.input(IN_TOUCH, 90, 400);                // the buttons are not there yet: nothing
    EQ(g.r_stage, RS_LISTEN);
    g.listen_result(ANS_NO_LISTENER);          // nobody to understand her: the child answers
    EQ(g.r_stage, RS_BUTTONS);
    shot(g, "30_question_buttons");
    g.input(IN_NEXT, 0, 0); EQ(g.r_sel, 1); g.input(IN_NEXT, 0, 0);
    g.input(IN_TOUCH, 90, 400);                // the tick
    EQ(g.r_stage, RS_REPLY); EQ(g.s.r_ans[Q_DINNER] >> 11, ANS_TAP_YES); EQ(g.stars_today(), 1);
    g.take_sfx(&line, &clip);
    CHECK(clip >= Q_YES[Q_DINNER] && clip < Q_YES[Q_DINNER] + R_VARIANTS);
    shot(g, "31_question_star");
    run(g, R_REPLY_MS + 200); EQ(g.scene, SC_HOME);
    Game t = routine_game();                   // no answer at all: the buttons after a while, then "silent"
    t.s.q_off = g.s.q_off;
    t.set_wall(at(2026, 11, 3, 18, 0)); t.tick(100); t.listen_req = LREQ_NONE;
    run(t, R_LISTEN_MS + 200);
    EQ(t.r_stage, RS_BUTTONS); EQ(t.listen_req, LREQ_CANCEL);   // the platform stops listening
    run(t, R_BUTTONS_MS + 200);
    EQ(t.r_stage, RS_REPLY); EQ(t.s.r_ans[Q_DINNER] >> 11, ANS_SILENT);
    CHECK(!(t.s.r_done & 1 << Q_DINNER));      // asked once more later
    run(t, R_REPLY_MS + 200); EQ(t.scene, SC_HOME);
    Game z = routine_game();                   // a late answer after the reply is ignored
    z.s.q_off = g.s.q_off;
    z.set_wall(at(2026, 11, 3, 18, 0)); z.tick(100);
    z.listen_result(ANS_NO); z.listen_result(ANS_YES);
    EQ(z.s.r_ans[Q_DINNER] >> 11, ANS_NO); EQ(z.stars_today(), 0);
    z.listen_result(99);                       // and nonsense from the platform does nothing
    EQ(z.r_stage, RS_REPLY);
}

TEST(routine_variants_never_repeat_early) {
    Game g = routine_game();
    g.s.q_off = (u16)~(1 << Q_DINNER) & ((1 << Q_COUNT) - 1);
    int order[30], n = 0;
    for (int d = 0; d < 30; d++) {
        int clips[R_CLIPS] = {0};
        live(g, at(2026, 11, 1, 17, 59) + d * 86400u, at(2026, 11, 1, 18, 5) + d * 86400u, ANS_YES, 0, 0, clips);
        for (int i = 0; i < R_VARIANTS; i++) if (clips[Q_ASK[Q_DINNER] + i]) order[n++] = i;
    }
    EQ(n, 30);
    for (int r = 0; r < 3; r++) {              // every round of ten: all ten, each once
        int seen = 0;
        for (int i = 0; i < 10; i++) seen |= 1 << order[r * 10 + i];
        EQ(seen, 0x3FF);
    }
    for (int i = 1; i < n; i++) CHECK(order[i] != order[i - 1]);   // never twice in a row, across rounds too
}

TEST(save_v7_loads_as_v8) {
    Game g = hatched();
    g.s.care_days = 33; g.s.bed10 = 120; g.s.lang = LANG_SV;
    u8 buf[SAVE_SIZE];
    g.save(buf);
    buf[0] = 7;
    buf[51] = 0;                               // v7's spare byte
    u32 c = 2166136261u;
    for (int i = 0; i < SAVE_V7_SIZE - 4; i++) c = (c ^ buf[i]) * 16777619u;
    std::memcpy(buf + SAVE_V7_SIZE - 4, &c, 4);
    Game h; h.init(5);
    CHECK(h.load(buf, SAVE_V7_SIZE));
    EQ(h.s.version, 8); EQ(h.s.care_days, 33); EQ(h.s.bed10, 120); EQ(h.s.lang, LANG_SV);
    EQ(h.s.wake_we10, WAKE_WE_DEFAULT10); EQ(h.s.r_asked, 0); EQ(h.s.cfg_rev, 0u); EQ(h.s.q_off, 0);
    Game r = routine_game();                   // v8 keeps everything the routine writes
    r.s.q_off = (u16)~(1 << Q_DINNER) & ((1 << Q_COUNT) - 1);
    Asked a[4];
    live(r, at(2026, 11, 3, 17, 59), at(2026, 11, 3, 18, 10), ANS_YES, a, 4);
    r.s.hol[0][0] = 9800; r.s.hol[0][1] = 9810; r.s.cfg_rev = 77; r.s.cfg_dirty = 5;
    r.save(buf);
    Game k; k.init(6); CHECK(k.load(buf, SAVE_SIZE));
    EQ(k.s.r_ans[Q_DINNER], r.s.r_ans[Q_DINNER]); EQ(k.s.week[k.s.r_day % 7], 1 << Q_DINNER);
    EQ(k.s.hol[0][1], 9810); EQ(k.s.cfg_rev, 77u); EQ(k.s.cfg_dirty, 5);
    buf[96 + 2 * Q_DINNER + 1] = 0xF8;         // an answer code past the last one: refused
    c = 2166136261u;
    for (int i = 0; i < SAVE_SIZE - 4; i++) c = (c ^ buf[i]) * 16777619u;
    std::memcpy(buf + SAVE_SIZE - 4, &c, 4);
    CHECK(!k.load(buf, SAVE_SIZE));
    Game wk = routine_game();                  // the week moves along: an old star is gone after 7 days
    wk.s.week[0] = 0x7FF; wk.s.week[3] = 3; wk.s.r_day = (u16)(at(2026, 11, 3, 0, 0) / 86400);
    wk.set_wall(at(2026, 11, 5, 6, 0)); wk.tick(100);   // two days on: the two slots between are cleared
    EQ(wk.s.r_day, at(2026, 11, 5, 0, 0) / 86400);
    int kept = 0;
    for (int i = 0; i < 7; i++) kept += wk.s.week[i] != 0;
    EQ(kept, 1 + (wk.s.week[3] != 0));
    wk.set_wall(at(2026, 11, 30, 6, 0)); wk.tick(100);   // weeks later: nothing old survives
    for (int i = 0; i < 7; i++) EQ(wk.s.week[i], 0);
}

TEST(settings_sync_with_the_parent_page) {
    Game g = hatched();
    g.set_wall(at(2026, 11, 3, 12, 0));
    Settings mine = g.settings();
    EQ(mine.v[K_WAKE_WEEKDAY], 7 * 60); EQ(mine.v[K_WAKE_WEEKEND], 8 * 60); EQ(mine.v[K_BEDTIME], 19 * 60 + 30);
    EQ(mine.v[K_ROUTINES], 1); EQ(mine.v[K_QUESTIONS], (1 << Q_COUNT) - 1); EQ(mine.v[K_BRIGHTNESS], 180);
    EQ(mine.v[K_PLAY_MIN], 10); EQ(mine.v[K_BREAK_MIN], 30); EQ(mine.v[K_SHAKE], 1);
    Settings page = mine;
    page.v[K_LANG] = LANG_DE; page.v[K_VOLUME] = VOL_LOW; page.v[K_WAKE_WEEKEND] = 9 * 60; page.v[K_TV_TEST] = 1;
    page.v[K_QUESTIONS] = (u16)(mine.v[K_QUESTIONS] & ~(1 << Q_WATER));
    page.v[K_HOLIDAY_RANGES] = 1; page.hol[0][0] = 9800; page.hol[0][1] = 9805;
    g.apply_settings(5, page, (1 << K_COUNT) - 1);
    EQ(g.s.lang, LANG_DE); EQ(g.volume(), VOL_LOW); EQ(g.s.wake_we10, 54); CHECK(g.tv_on());
    EQ(g.s.tv_day, at(2026, 11, 3, 0, 0) / 86400); CHECK(g.s.q_off & 1 << Q_WATER); EQ(g.s.hol[0][1], 9805);
    EQ(g.s.cfg_rev, 5u); EQ(g.s.cfg_dirty, 0);
    page.v[K_LANG] = LANG_FR;
    g.apply_settings(5, page, (1 << K_COUNT) - 1);   // the same revision again: nothing new
    EQ(g.s.lang, LANG_DE);
    g.input(IN_PARENT, 0, 0);                  // on the toy: volume and bedtime, by the parent holding it
    tap_set(g, 300, SET_VOLUME); EQ(g.volume(), VOL_OFF);
    CHECK(g.s.cfg_dirty & 1 << K_VOLUME);
    page.v[K_VOLUME] = VOL_NORMAL; page.v[K_LANG] = LANG_FR;   // the page changed both meanwhile
    g.apply_settings(6, page, (1 << K_COUNT) - 1);
    EQ(g.volume(), VOL_OFF);                   // the toy's own change wins its key...
    EQ(g.s.lang, LANG_FR);                     // ...the page wins the others
    CHECK(g.s.cfg_dirty & 1 << K_VOLUME);      // and the toy's change is still to be told
    g.settings_synced(g.sync_mark()); EQ(g.s.cfg_dirty, 0);
    page.v[K_BRIGHTNESS] = 0; page.v[K_LANG] = 99; page.v[K_PLAY_MIN] = 500;   // out of range: ignored
    page.v[K_HOLIDAY_RANGES] = 2; page.hol[1][0] = 9900; page.hol[1][1] = 9890;    // backwards: ignored
    g.apply_settings(7, page, (1 << K_COUNT) - 1);
    EQ(g.brightness(), 180); EQ(g.s.lang, LANG_FR); EQ(g.play_limit_ms(), 10 * 60000u); EQ(g.s.hol[1][0], 0);
    Settings few = page;                       // only the keys a page sends are touched
    few.v[K_CALM] = 1; few.v[K_LANG] = LANG_IT;
    g.apply_settings(8, few, 1 << K_CALM);
    CHECK(g.calm()); EQ(g.s.lang, LANG_FR);
    Settings m = merge_settings(mine, 1 << K_LANG, few, 1 << K_LANG | 1 << K_CALM);   // the pure merge
    EQ(m.v[K_LANG], mine.v[K_LANG]); EQ(m.v[K_CALM], 1);
}

TEST(tv_test_mode_listens_only_to_lasting_sound) {
    Game g = hatched();
    g.set_wall(at(2026, 11, 3, 10, 0));
    g.input(IN_PARENT, 0, 0); g.set_page = PG_ROUTINE;
    tap_set(g, 300, RT_TV);
    CHECK(g.tv_on()); EQ(g.s.tv_day, at(2026, 11, 3, 0, 0) / 86400);
    tap_set(g, 300, RT_BACK); tap_set(g, 300, SET_EXIT); EQ(g.scene, SC_HOME);
    auto check_due = [&]() {                   // the next once-a-minute level check
        for (int i = 0; i < 700; i++) {
            u32 f = g.tick(100);
            if ((f & F_LISTEN) && g.listen_req == LREQ_LISTEN) { int k = g.listen_kind; g.listen_req = LREQ_NONE; return k; }
        }
        return -1;
    };
    int sampled = 0, minute = 0, when[200];
    auto level = [&](int db, int pct) {
        EQ(check_due(), LK_LEVEL);
        minute++;
        g.level_result(db, pct);
        if (g.listen_req == LREQ_LISTEN && g.listen_kind == LK_TV) { when[sampled++ % 200] = minute; g.listen_req = LREQ_NONE; }
    };
    level(-60, 0); level(-30, 90); EQ(sampled, 0);   // one loud minute: maybe a door
    level(-30, 20); level(-30, 20); EQ(sampled, 0);   // loud but not sustained: a slam, a shout
    level(-30, 90); EQ(sampled, 0);
    level(-30, 90); EQ(sampled, 1);            // two in a row: a sample
    for (int i = 0; i < 8; i++) level(-31, 90);
    EQ(sampled, 1);                            // the same sound going on: not again for ten minutes
    level(-15, 90); EQ(sampled, 2);            // it changed (much louder): a new sample
    level(-60, 0); level(-30, 90); level(-30, 90); EQ(sampled, 3);   // quiet, then sound again
    for (int i = 0; i < 40; i++) { level(-60, 0); level(-30 + (i % 2) * 20, 90); level(-30 + (i % 2) * 20, 90); }
    CHECK(sampled > TV_PER_HOUR);              // a changing sound keeps being sampled...
    for (int i = 0; i + TV_PER_HOUR < sampled && i + TV_PER_HOUR < 200; i++)
        CHECK(when[i + TV_PER_HOUR] - when[i] >= 60);   // ...but never more than six in any hour
    g.set_wall(at(2026, 11, 3, 21, 0));        // not at night
    for (int i = 0; i < 1200; i++) { u32 f = g.tick(100); CHECK(!(f & F_LISTEN)); }
    g.set_wall(at(2026, 11, 10, 10, 0)); g.tick(100);   // seven days on: it switched itself off
    CHECK(!g.tv_on()); CHECK(g.s.cfg_dirty & 1 << K_TV_TEST);
}

TEST(radio_link_policy) {
    Game g = routine_game();
    g.set_wall(at(2026, 11, 3, 12, 0)); g.tick(100);
    EQ(g.link(), LINK_OFF);                    // radio off by default
    EQ(g.link(true), LINK_NOW);                // a TV sample waiting to go out
    plug(g, true); EQ(g.link(), LINK_STAY);    // on USB power: kept up
    g.wifi_stage = WST_WAIT; EQ(g.link(), LINK_OFF);   // the WiFi setup owns the radio
    g.wifi_stage = WST_OFF; plug(g, false); EQ(g.link(), LINK_OFF);
    EQ(name_line(SFX_WAKE, V_AWAKE), NL_AWAKE); EQ(name_line(SFX_HELLO, V_HELLO), NL_BACK);
    EQ(name_line(SFX_HATCH, V_HELLO), NL_HELLO); EQ(name_line(SFX_EAT, V_YUM), -1);
}

// Codex review of the daily routine (hunt 13): sync acknowledgements, minute precision, clock trust, TV timing.
TEST(review_fixes_routine_pass) {
    {   // an edit made while a sync was out is not cleared by its acknowledgement
        Game g = hatched();
        g.set_wall(at(2026, 11, 3, 12, 0));
        g.input(IN_PARENT, 0, 0);
        tap_set(g, 300, SET_VOLUME);           // normal -> low, dirty
        Settings server = g.settings();        // what the sync sends (and the page then keeps)
        SyncMark sent = g.sync_mark();
        tap_set(g, 300, SET_VOLUME);           // low -> off while the request is out
        int mine = g.volume();
        g.settings_synced(sent);
        CHECK(g.s.cfg_dirty & 1 << K_VOLUME);  // still to be told
        server.v[K_CALM] = 1;                  // the page changes something else; its config has the old volume
        g.apply_settings(9, server, (1 << K_COUNT) - 1);
        EQ(g.volume(), mine); CHECK(g.calm());
        g.settings_synced(g.sync_mark()); EQ(g.s.cfg_dirty, 0);
    }
    {   // the page's 07:35 and 19:39 are kept to the minute: the window, the settings it reports, the save
        Game g = hatched();
        Settings page = g.settings();
        page.v[K_WAKE_WEEKDAY] = 7 * 60 + 35; page.v[K_BEDTIME] = 19 * 60 + 39; page.v[K_WAKE_WEEKEND] = 8 * 60 + 5;
        g.apply_settings(3, page, (1 << K_COUNT) - 1);
        Settings back = g.settings();
        EQ(back.v[K_WAKE_WEEKDAY], 7 * 60 + 35); EQ(back.v[K_BEDTIME], 19 * 60 + 39); EQ(back.v[K_WAKE_WEEKEND], 8 * 60 + 5);
        g.set_wall(at(2026, 11, 3, 19, 38)); EQ(g.bedtime(), BED_NONE);   // Tuesday
        g.set_wall(at(2026, 11, 3, 19, 39)); EQ(g.bedtime(), BED_DROWSY);
        g.set_wall(at(2026, 11, 4, 7, 34)); CHECK(g.bedtime() != BED_NONE);
        g.set_wall(at(2026, 11, 4, 7, 35)); EQ(g.bedtime(), BED_NONE);
        u8 buf[SAVE_SIZE]; g.save(buf);
        Game h; h.init(3); CHECK(h.load(buf, SAVE_SIZE));
        EQ(h.clock_min(T_BED), 19 * 60 + 39); EQ(h.clock_min(T_WAKE), 7 * 60 + 35); EQ(h.clock_min(T_WAKE_WE), 8 * 60 + 5);
        Save* raw = (Save*)buf;                // a minute past the ten that cannot be: refused
        raw->min_ones = (u16)(raw->min_ones | 10 << 4);
        u32 c = 2166136261u;
        for (u32 i = 0; i < SAVE_SIZE - 4; i++) c = (c ^ buf[i]) * 16777619u;
        raw->check = c;
        CHECK(!h.load(buf, SAVE_SIZE));
    }
    {   // the clock stops being trusted mid-question: she ends it silently, and a late answer says nothing
        Game g = routine_game();
        g.s.q_off = (u16)~(1 << Q_DINNER) & ((1 << Q_COUNT) - 1);
        g.set_wall(at(2026, 11, 3, 18, 0)); g.tick(100);
        EQ(g.scene, SC_ASK);
        g.listen_req = LREQ_NONE; g.listen_started();
        g.set_wall(at(2026, 11, 3, 18, 0), false);
        CHECK(g.scene != SC_ASK); EQ(g.listen_req, LREQ_CANCEL);
        int line, clip;
        g.take_sfx(&line, &clip); EQ(clip, -1);
        g.listen_result(ANS_YES);
        g.take_sfx(&line, &clip); EQ(clip, -1); EQ(g.stars_today(), 0);
    }
    {   // a level result that comes back after 17:00 (or with an untrusted clock) starts no recording
        for (int late = 0; late < 3; late++) {
            Game g = hatched();
            g.set_wall(at(2026, 11, 3, 16, 59));
            g.s.opts |= OPT_TV_TEST; g.s.tv_day = (u16)(at(2026, 11, 3, 0, 0) / 86400);
            g.listen_req = LREQ_NONE;
            g.level_result(-30, 90);
            if (late == 1) g.set_wall(at(2026, 11, 3, 17, 0));
            if (late == 2) g.set_wall(at(2026, 11, 3, 16, 59), false);
            g.level_result(-30, 90);
            bool sample = g.listen_req == LREQ_LISTEN && g.listen_kind == LK_TV;
            EQ(sample, late == 0);
        }
    }
    {   // TV test switched on on the page on 1 November counts from then, however late the toy hears of it
        Game g = hatched();
        u16 nov1 = (u16)(at(2026, 11, 1, 0, 0) / 86400);
        Settings page = g.settings();
        page.v[K_TV_TEST] = 1; page.tv_day = nov1;
        g.set_wall(at(2026, 11, 3, 10, 0));
        g.apply_settings(4, page, 1 << K_TV_TEST);
        CHECK(g.tv_on()); EQ(g.s.tv_day, nov1); EQ(g.settings().tv_day, nov1);
        Game late = hatched();
        late.set_wall(at(2026, 11, 8, 10, 0));
        late.apply_settings(4, page, 1 << K_TV_TEST);
        late.tick(100);
        CHECK(!late.tv_on());                  // its seven days were over before it arrived
        Game future = hatched();               // a date ahead of the toy's clock: from today
        future.set_wall(at(2026, 11, 3, 10, 0));
        page.tv_day = (u16)(nov1 + 400);
        future.apply_settings(4, page, 1 << K_TV_TEST);
        EQ(future.s.tv_day, at(2026, 11, 3, 0, 0) / 86400);
    }
    {   // toilet stays last: it waits for teeth's second try, even when that comes after its own window
        Game g = routine_game();
        g.s.q_off = (u16)~(1 << Q_TEETH_PM | 1 << Q_TOILET) & ((1 << Q_COUNT) - 1);
        g.s.volume = VOL_OFF;                  // busy (quiet) until 18:51
        Asked a[8];
        live(g, at(2026, 11, 3, 17, 50), at(2026, 11, 3, 18, 51), ANS_YES, a, 8);
        EQ(g.s.r_asked, 0);
        g.s.volume = VOL_NORMAL;
        EQ(live(g, at(2026, 11, 3, 18, 51), at(2026, 11, 3, 18, 52), ANS_UNCLEAR, a, 8), 1);
        EQ(a[0].q, Q_TEETH_PM);
        int n = live(g, at(2026, 11, 3, 18, 52), at(2026, 11, 3, 19, 40), ANS_YES, a, 8);
        EQ(n, 2); EQ(a[0].q, Q_TEETH_PM); EQ(a[1].q, Q_TOILET);
        CHECK(a[1].minute < 19 * 60 + 30 && a[1].minute - a[0].minute >= Q_GAP_MIN);
    }
}

// The second Codex pass (hunt 14): a question across midnight, routines switched off mid-question, the hello.
TEST(review_fixes_routine_pass2) {
    {   // asked at 23:5x, answered after midnight: the answer is the old day's, the new day starts clean after it
        Game g = routine_game();
        g.s.q_off = (u16)~(1 << Q_BREAKFAST) & ((1 << Q_COUNT) - 1);
        g.set_clock_min(T_WAKE, 23 * 60); g.set_clock_min(T_WAKE_WE, 23 * 60); g.set_clock_min(T_BED, 23 * 60);
        u32 t = at(2026, 11, 3, 23, 0);
        for (; g.scene != SC_ASK && t < at(2026, 11, 4, 0, 0); t += 60) {
            g.set_wall(t);
            for (int k = 0; k < 60 && g.scene != SC_ASK; k++) g.tick(1000);
        }
        EQ(g.scene, SC_ASK); EQ(g.r_q, Q_BREAKFAST);
        g.listen_req = LREQ_NONE; g.listen_started();
        g.set_wall(at(2026, 11, 4, 0, 0) + 10); g.tick(100);
        EQ(g.scene, SC_ASK);                   // still her question
        g.listen_result(ANS_YES);
        run(g, R_REPLY_MS + 500);
        EQ(g.s.r_day, at(2026, 11, 4, 0, 0) / 86400);
        EQ(g.s.r_done & 1 << Q_BREAKFAST, 0); EQ(g.stars_today(), 0);   // the new day's breakfast is still to come
        EQ(g.s.week[(g.s.r_day + 6) % 7], 1 << Q_BREAKFAST);            // the star went to the day she asked
    }
    {   // the page switches routines off while she listens: the question ends, a late answer says nothing
        Game g = routine_game();
        g.s.q_off = (u16)~(1 << Q_DINNER) & ((1 << Q_COUNT) - 1);
        g.set_wall(at(2026, 11, 3, 18, 0)); g.tick(100);
        EQ(g.scene, SC_ASK);
        g.listen_req = LREQ_NONE; g.listen_started();
        Settings page = g.settings();
        page.v[K_ROUTINES] = 0;
        g.apply_settings(12, page, 1 << K_ROUTINES);
        CHECK(g.scene != SC_ASK); EQ(g.listen_req, LREQ_CANCEL);
        int line, clip;
        g.take_sfx(&line, &clip); EQ(clip, -1);
        g.listen_result(ANS_YES);
        g.take_sfx(&line, &clip); EQ(clip, -1); EQ(g.stars_today(), 0);
    }
    {   // the morning hello waiting to be said when the clock stops being trusted: not said
        Game g = routine_game();
        g.set_wall(at(2026, 11, 3, 7, 0)); g.tick(100);
        CHECK(g.s.r_done & 1 << Q_COUNT);       // the hello is queued
        CHECK(g.rclip != 0);
        g.set_wall(at(2026, 11, 3, 7, 0), false);
        int line, clip;
        g.take_sfx(&line, &clip); EQ(clip, -1);
    }
}

// The third Codex pass (hunt 15): windows near midnight, a remote mute mid-question.
TEST(review_fixes_routine_pass3) {
    {   // wake-up and bedtime at 23:30: breakfast in the half hour before midnight, not never
        Game g = routine_game();
        g.s.q_off = (u16)~(1 << Q_BREAKFAST) & ((1 << Q_COUNT) - 1);
        g.set_clock_min(T_WAKE, 23 * 60 + 30); g.set_clock_min(T_WAKE_WE, 23 * 60 + 30); g.set_clock_min(T_BED, 23 * 60 + 30);
        Asked a[4];
        EQ(live(g, at(2026, 11, 3, 23, 0), at(2026, 11, 4, 0, 40), ANS_YES, a, 4), 1);
        EQ(a[0].q, Q_BREAKFAST); CHECK(a[0].minute >= 23 * 60 + 30);
    }
    {   // bedtime at 00:30: teeth in the evening before midnight, the happy question just after
        Game g = routine_game();
        g.s.q_off = (u16)~(1 << Q_TEETH_PM | 1 << Q_HAPPY) & ((1 << Q_COUNT) - 1);
        g.set_clock_min(T_BED, 30);
        Asked a[4];
        int n = live(g, at(2026, 11, 3, 21, 0), at(2026, 11, 4, 0, 40), ANS_YES, a, 4);
        EQ(n, 2);
        EQ(a[0].q, Q_TEETH_PM); CHECK(a[0].minute >= 23 * 60 && a[0].minute < 24 * 60);
        EQ(a[1].q, Q_HAPPY); CHECK(a[1].minute < 25);
    }
    {   // the page mutes the toy while she listens: the question ends, a late answer counts for nothing
        Game g = routine_game();
        g.s.q_off = (u16)~(1 << Q_DINNER) & ((1 << Q_COUNT) - 1);
        g.set_wall(at(2026, 11, 3, 18, 0)); g.tick(100);
        EQ(g.scene, SC_ASK);
        g.listen_req = LREQ_NONE; g.listen_started();
        Settings page = g.settings();
        page.v[K_VOLUME] = VOL_OFF;
        g.apply_settings(13, page, 1 << K_VOLUME);
        CHECK(g.scene != SC_ASK); EQ(g.listen_req, LREQ_CANCEL);
        g.listen_result(ANS_YES);
        EQ(g.stars_today(), 0); EQ(g.s.r_ans[Q_DINNER], 0);
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
        T(screen_turns_off_and_first_touch_only_wakes), T(menu_strip_takes_the_nearest_button),
        T(save_load_roundtrip_and_corruption), T(power_off_freezes_time), T(autosave_every_five_minutes),
        T(moods_follow_needs), T(sound_cue_for_every_event), T(the_child_cannot_mute_and_old_mutes_become_volume_off), T(event_shake),
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
        T(stars_hit_where_they_are_drawn), T(version_3_saves_still_load), T(parent_volume_off_stays_off), T(long_settings_lists_turn_pages),
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
        T(wifi_setup_from_the_toy), T(firmware_update_page), T(text_edge_cases), T(every_language_fits_the_screen),
        T(review_fixes_bug_hunt), T(review_fixes_third_pass), T(old_local_break_deadlines_are_not_read_as_utc), T(no_sound_at_bedtime), T(she_speaks_her_lines), T(her_voice_file_decodes), T(damaged_packs_never_read_outside), T(the_screen_dims_at_night), T(fifth_pass_fixes), T(album_opens_from_the_info_screen),
        T(settings_close_when_the_grown_up_walks_away), T(every_scene_renders_for_every_character),
        T(routine_school_morning), T(routine_weekend_and_holidays), T(routine_evening), T(routine_asks_again_once),
        T(routine_stays_quiet), T(routine_question_flow), T(routine_variants_never_repeat_early), T(save_v7_loads_as_v8),
        T(settings_sync_with_the_parent_page), T(tv_test_mode_listens_only_to_lasting_sound), T(radio_link_policy),
        T(review_fixes_routine_pass), T(review_fixes_routine_pass2), T(review_fixes_routine_pass3),
    };
    for (auto& t : tests) {
        int before = failures;
        t.fn();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
