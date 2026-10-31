// Game rules: needs decay over powered time, care actions, a star-popping mini-game, petting,
// an egg that hatches, and the cosmetic life around it (idle wandering, particles).
// The unicorn never dies: low needs only change her face.
#include "unicorn.h"

namespace up {

static int clamp(int v) { return v < 0 ? 0 : v > MAX_STAT ? MAX_STAT : v; }

static u32 fnv(const u8* p, u32 n) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void copy(u8* dst, const u8* src, u32 n) {
    for (u32 i = 0; i < n; i++) dst[i] = src[i];
}

static u32 xorshift(u32& x) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

u32 Game::random() { return xorshift(rng); }
u32 Game::crandom() { return xorshift(crng); }      // cosmetic only: never changes stats

void Game::init(u32 seed) {
    u8* raw = (u8*)this;
    for (u32 i = 0; i < sizeof(Game); i++) raw[i] = 0;
    rng = seed ? seed : 0x9E3779B9u;
    crng = (rng ^ 0xA5A5A5A5u) | 1;
    s.version = SAVE_VERSION;
    s.character = (u8)(random() % 4);
    for (int i = 0; i < NSTATS; i++) s.stat[i] = 8000;
    scene = SC_EGG;
    screen_on = 1;
    dirty = 1;
    facing = 1;
    idle_next = 3000;
}

bool Game::load(const u8* data, u32 len) {
    // Older saves are prefixes of the current layout: copy what they have, zero the rest.
    if (!data || len < 1) return false;
    u32 size = data[0] == 1 ? SAVE_V1_SIZE : data[0] == 2 ? SAVE_V2_SIZE : data[0] == 3 ? SAVE_V3_SIZE
             : data[0] == SAVE_VERSION ? SAVE_SIZE : 0;
    if (!size || len < size) return false;
    u32 check;
    copy((u8*)&check, data + size - 4, 4);
    if (fnv(data, size - 4) != check) return false;
    Save tmp;
    for (u32 i = 0; i < SAVE_SIZE; i++) ((u8*)&tmp)[i] = 0;
    copy((u8*)&tmp, data, size - 4);
    tmp.version = SAVE_VERSION;
    if (tmp.character > 3) return false;
    for (int i = 0; i < NSTATS; i++) if (tmp.stat[i] > MAX_STAT) return false;
    s = tmp;
    play_ms = (u32)s.play_s * 1000;
    away_ms = (u32)s.away_s * 1000;
    go(!s.hatched ? SC_EGG : (s.away_s || s.away_until) ? SC_AWAY : SC_HOME);   // no power-cut shortcut
    screen_on = 1;
    sync_away();                           // with an RTC, the time she was off counts too
    return true;
}

void Game::set_wall(u32 epoch_s) {
    wall_s = epoch_s;
    sync_away();
}

// While she is out and both a return time and a wall clock are known, the clock decides.
// A clock that went backwards (RTC lost power and restarted) is not trusted: fall back to
// counting powered time from what was left.
void Game::sync_away() {
    if (scene != SC_AWAY || !s.away_until || !wall_s) return;
    if (wall_s + break_ms() / 1000 < s.away_until) { s.away_until = 0; return; }
    u32 left = wall_s >= s.away_until ? 0 : (s.away_until - wall_s) * 1000;
    if (left != away_ms) { away_ms = left; dirty = 1; }
}

u32 Game::save(u8* out) const {
    Save tmp = s;
    bool out_now = scene == SC_LEAVE || scene == SC_AWAY;
    tmp.away_s = (u16)(scene == SC_LEAVE ? break_ms() / 1000 : out_now ? (away_ms + 999) / 1000 : 0);
    tmp.play_s = (u16)(play_ms / 1000 > 65535 ? 65535 : play_ms / 1000);
    tmp.away_until = !out_now ? 0 : scene == SC_LEAVE ? (wall_s ? wall_s + break_ms() / 1000 : 0) : s.away_until;
    tmp.check = fnv((const u8*)&tmp, SAVE_SIZE - 4);
    copy(out, (const u8*)&tmp, SAVE_SIZE);
    return SAVE_SIZE;
}

void Game::set_stat(int i, int v) {
    if (i >= 0 && i < NSTATS) { s.stat[i] = (u16)clamp(v); dirty = 1; }
}

int Game::take_sfx() {
    int id = s.muted ? (int)SFX_NONE : (int)sfx;
    sfx = SFX_NONE;
    return id;
}

void Game::go(u8 sc) {
    scene = sc;
    scene_ms = anim_ms = 0;
    frame = 0;
    dirty = 1;
    idle_act = IDLE_NONE;
}

void Game::hatch() {
    screen_on = 1;                         // never hatch in the dark: this is the big moment
    idle_ms = 0;
    s.hatched = 1;
    want_save = 1;
    go(SC_HATCH);
    cue(SFX_HATCH);
    burst(P_CONFETTI, SCREEN_W / 2, 230, 10);
    burst(P_STAR, SCREEN_W / 2, 230, 4);
    hint_pet = 1;                          // a hand shows how to pet her until she is petted
}

void Game::start_leave() {                 // a friend comes by; she says goodbye and goes out
    go(SC_LEAVE);
    cue(SFX_BYE);
    facing = 1;
}

void Game::press(int item) {               // a menu button dips and pops back (render)
    press_item = (u8)item;
    press_end = clock_ms + PRESS_MS;
}

int Game::recommended() const {
    const u16* v = s.stat;
    if (v[HUNGER] < NEED_LOW) return MENU_FEED;
    if (v[ENERGY] < NEED_LOW) return MENU_SLEEP;
    if (v[CLEAN] < NEED_LOW) return MENU_WASH;
    if (v[HAPPY] < NEED_LOW) return MENU_PLAY;
    return -1;
}

void Game::come_back() {                   // she walks in from the left
    s.away_until = 0;
    go(SC_RETURN);
    pet_x = -300;
    facing = 1;
    cue(SFX_HELLO);
}

void Game::wake() {
    go(SC_HOME);
    cue(SFX_WAKE);
}

Mood Game::mood() const {
    const u16* v = s.stat;
    if (v[HUNGER] < NEED_LOW) return M_HUNGRY;
    if (v[ENERGY] < NEED_LOW) return M_SLEEPY;
    if (v[CLEAN] < NEED_LOW || v[HAPPY] < NEED_LOW) return M_SAD;
    if (v[HUNGER] >= 7000 && v[HAPPY] >= 7000 && v[ENERGY] >= 7000 && v[CLEAN] >= 7000) return M_HAPPY;
    return M_IDLE;
}

int Game::messes() const {
    if (!s.hatched) return 0;
    int c = s.stat[CLEAN];
    return c < 1000 ? 3 : c < 1800 ? 2 : c < NEED_LOW ? 1 : 0;
}

int Game::particles() const {
    int n = 0;
    for (int i = 0; i < NPART; i++) n += part[i].kind != P_NONE;
    return n;
}

// ---- particles -----------------------------------------------------------------------------

void Game::burst(int kind, int x, int y, int n) {
    static const u8 LIFE[] = {0, 14, 6, 20, 8, 10, 16, 6};
    if (calm()) n = (n + 1) / 2;
    for (int k = 0; k < n; k++) {
        int slot = 0;                      // free slot, else the one closest to expiring
        for (int i = 0; i < NPART; i++) {
            if (part[i].kind == P_NONE) { slot = i; break; }
            if (part[i].life < part[slot].life) slot = i;
        }
        Part& p = part[slot];
        p.kind = (u8)kind;
        int spread = kind == P_HEART ? 28 : 10;           // hearts fan out so each one reads
        p.x = (i16)(x + (int)(crandom() % (2 * spread + 1)) - spread);
        p.y = (i16)(y + (int)(crandom() % 11) - 5);
        p.vx = (i8)((int)(crandom() % 7) - 3);
        switch (kind) {
        case P_HEART:    p.vy = (i8)-(2 + (int)(crandom() % 2)); p.vx = (i8)(p.vx / 2); break;
        case P_BUBBLE:   p.vy = (i8)-(1 + (int)(crandom() % 2)); break;
        case P_CRUMB:    p.vy = (i8)-(1 + (int)(crandom() % 3)); break;
        case P_CONFETTI: p.vy = (i8)-(3 + (int)(crandom() % 4)); break;
        case P_RING:     p.vx = 0; p.vy = 0; p.x = (i16)x; p.y = (i16)y; break;
        default:         p.vy = (i8)((int)(crandom() % 7) - 4); break;
        }
        p.life = LIFE[kind];
        p.col = (u8)(crandom() % 7);
    }
    dirty = 1;
}

// ---- mini-game -----------------------------------------------------------------------------

void Game::spawn_target(int i, int y) {
    int x = 62 + i * 122 + (int)(crandom() % 41) - 20;
    tgt[i].x = (i16)(x < 44 ? 44 : x > SCREEN_W - 44 ? SCREEN_W - 44 : x);
    tgt[i].y = (i16)y;
    tgt[i].vy = (i8)-(4 + (int)(crandom() % 3));
    tgt[i].alive = 1;
}

bool Game::visible(int i) const { return tgt[i].alive && tgt[i].y < SCREEN_H - 20; }

int Game::highest_visible() const {
    int best = -1;
    for (int i = 0; i < GAME_TARGETS; i++)
        if (visible(i) && (best < 0 || tgt[i].y < tgt[best].y)) best = i;
    return best;
}

void Game::pop(int i) {
    if (i < 0 || i >= GAME_TARGETS || !tgt[i].alive) return;
    if (score < 99) score++;
    burst(P_RING, tgt[i].x, tgt[i].y, 1);
    burst(P_SPARKLE, tgt[i].x, tgt[i].y, 2);
    burst(P_CONFETTI, tgt[i].x, tgt[i].y, 3);
    cue(SFX_POP);
    spawn_target(i, SCREEN_H - 10 + (int)(crandom() % 30));   // the next one peeks in at the bottom
}

// ---- care ----------------------------------------------------------------------------------

void Game::pet() {
    pet_react_end = clock_ms + PET_REACT_MS;
    petting = 1;
    hint_pet = 0;
    idle_act = IDLE_NONE;
    burst(P_HEART, SCREEN_W / 2 + pet_x + facing * 42, 112, 3);   // just above her head
    cue(SFX_PET);
    if (!pet_cooling || due(pet_gain_at)) { // hearts every time, happiness rationed
        s.stat[HAPPY] = (u16)clamp(s.stat[HAPPY] + PET_HAPPY);
        pet_gain_at = clock_ms + PET_COOLDOWN_MS;
        pet_cooling = 1;
    }
    dirty = 1;
}

void Game::minute() {
    u16* v = s.stat;
    if (scene == SC_LEAVE || scene == SC_AWAY || scene == SC_RETURN) return;   // out with friends
    if (scene == SC_SLEEP) {               // sleeping: energy refills, other needs slow down
        v[ENERGY] = (u16)clamp(v[ENERGY] + SLEEP_ENERGY_GAIN);
        v[HUNGER] = (u16)clamp(v[HUNGER] - DECAY_HUNGER / 2);
        v[CLEAN] = (u16)clamp(v[CLEAN] - DECAY_CLEAN / 2);
    } else {
        v[HUNGER] = (u16)clamp(v[HUNGER] - DECAY_HUNGER);
        v[HAPPY] = (u16)clamp(v[HAPPY] - DECAY_HAPPY);
        v[ENERGY] = (u16)clamp(v[ENERGY] - DECAY_ENERGY);
        v[CLEAN] = (u16)clamp(v[CLEAN] - DECAY_CLEAN);
    }
    dirty = 1;
}

void Game::eat(int t) {                    // the chosen snack: all three nourish the same
    u16* v = s.stat;
    treat = (u8)t;
    v[HUNGER] = (u16)clamp(v[HUNGER] + FEED_GAIN);
    v[HAPPY] = (u16)clamp(v[HAPPY] + TREAT_HAPPY);
    go(SC_EAT);
    cue(SFX_EAT);
    want_save = 1;
}

// Parent settings: BOOT (or tap) picks a row, PWR (or tap) changes it.
void Game::settings_input(int type, int x, int y) {
    (void)x;
    int row = set_row;
    if (type == IN_TOUCH) {
        if (y < 70) return;
        row = (y - 70) / 44;
        if (row >= SET_ROWS) return;
        set_row = (u8)row;
    } else if (type == IN_NEXT) {
        set_row = (u8)((set_row + 1) % SET_ROWS);
        dirty = 1;
        return;
    } else if (type != IN_SELECT) {
        return;
    }
    static const u8 LIMITS[] = {5, 10, 15, 20}, BREAKS[] = {15, 30, 45, 60}, BRIGHT[] = {60, 120, 180, 255};
    switch (row) {
    case SET_VOLUME: s.volume = (u8)((s.volume + 1) % 3); s.muted = 0; break;
    case SET_LIMIT: {
        int cur = s.limit_min ? s.limit_min : 10, i = 0;
        while (i < 4 && LIMITS[i] != cur) i++;
        s.limit_min = LIMITS[(i + 1) % 4];
        break;
    }
    case SET_BREAK: {
        int cur = s.break_min ? s.break_min : 30, i = 0;
        while (i < 4 && BREAKS[i] != cur) i++;
        s.break_min = BREAKS[(i + 1) % 4];
        break;
    }
    case SET_BRIGHT: {
        int cur = brightness(), i = 0;
        while (i < 4 && BRIGHT[i] != cur) i++;
        s.bright = BRIGHT[(i + 1) % 4];
        break;
    }
    case SET_CALM:  s.opts ^= OPT_CALM; break;
    case SET_SHAKE: s.opts ^= OPT_NO_SHAKE; break;
    case SET_EXIT:  go(s.hatched ? SC_HOME : SC_EGG); break;
    }
    want_save = 1;
    dirty = 1;
    cue(SFX_TAP);
}

void Game::act(int item) {
    u16* v = s.stat;
    pet_x = 0;                             // actions are staged centre-front
    facing = 1;
    switch (item) {
    case MENU_FEED:                        // she gets to choose: three big pictures
        sel = treat;
        go(SC_FEED);
        cue(SFX_TAP);
        return;
    case MENU_PLAY:
        if (v[ENERGY] < PLAY_MIN_ENERGY) { go(SC_REFUSE); cue(SFX_REFUSE); return; }   // too tired
        v[HAPPY] = (u16)clamp(v[HAPPY] + PLAY_HAPPY);      // guaranteed, even with no stars
        v[ENERGY] = (u16)clamp(v[ENERGY] - PLAY_COST);
        v[HUNGER] = (u16)clamp(v[HUNGER] - PLAY_HUNGER);
        v[CLEAN] = (u16)clamp(v[CLEAN] - PLAY_DIRT);
        score = 0;
        sel = 0;
        for (int i = 0; i < GAME_TARGETS; i++) spawn_target(i, 250 + i * 70);   // some in view at once
        go(SC_GAME);
        cue(SFX_PLAY);
        break;
    case MENU_SLEEP:
        sleep_ms = 0;
        napped = 0;
        go(SC_SLEEP);
        cue(SFX_SLEEP);
        break;
    case MENU_WASH:
        wash_messes = (u8)messes();        // shown dissolving during the bath
        v[CLEAN] = MAX_STAT;
        v[HAPPY] = (u16)clamp(v[HAPPY] + TREAT_HAPPY);
        celebrated = 0;
        go(SC_WASH);
        cue(SFX_WASH);
        break;
    }
    want_save = 1;
}

// ---- input ---------------------------------------------------------------------------------

void Game::input(int type, int x, int y) {
    if (type == IN_PARENT) {               // a grown-up: call her back early, or open settings
        screen_on = 1;
        idle_ms = 0;
        if (scene == SC_AWAY || scene == SC_LEAVE) { away_ms = 0; come_back(); }
        else if (scene == SC_HOME || scene == SC_EGG || scene == SC_INFO) { set_row = 0; go(SC_SETTINGS); }
        return;
    }
    if (type == IN_SHAKE && (s.opts & OPT_NO_SHAKE)) return;
    if (!screen_on) {                      // first touch only wakes the screen
        screen_on = 1;
        idle_ms = 0;
        dirty = 1;
        if (scene == SC_AWAY && away_ms == 0) { come_back(); }   // she's back!
        return;
    }
    if (scene != SC_AWAY) idle_ms = 0;     // knocking does not keep the door screen lit
    switch (scene) {
    case SC_AWAY:
        if (away_ms == 0) { come_back(); break; }
        wobble_end = clock_ms + WOBBLE_MS;  // the sign sways a little; nothing to win by knocking
        wobbling = 1;
        dirty = 1;
        break;
    case SC_SETTINGS:
        settings_input(type, x, y);
        break;
    case SC_FEED:
        if (type == IN_TOUCH) {
            if (y >= 110 && y <= 300) {    // the three cards
                int t = x / (SCREEN_W / TREAT_COUNT);
                t = t < TREAT_COUNT ? t : TREAT_COUNT - 1;
                press(t);
                eat(t);
            }
        } else if (type == IN_NEXT) {
            sel = (u8)((sel + 1) % TREAT_COUNT);
            dirty = 1;
            cue(SFX_TAP);
        } else if (type == IN_SELECT) {
            press(sel);
            eat(sel);
        }
        break;
    case SC_EGG:                           // taps and shakes both nudge the egg
        dirty = 1;
        wobble_end = clock_ms + WOBBLE_MS;
        wobbling = 1;
        if (++taps >= EGG_TAPS) hatch();
        else cue(SFX_TAP);
        break;
    case SC_HOME:
        if (play_ms >= play_limit_ms()) { start_leave(); break; }   // time's up: no new activities
        if (type == IN_NEXT) {
            menu = (u8)((menu + 1) % MENU_COUNT);
            dirty = 1;
            cue(SFX_TAP);
        } else if (type == IN_SHAKE) {     // shaking the toy = let's play
            menu = MENU_PLAY;
            act(MENU_PLAY);
        } else if (type == IN_SELECT) {
            press(menu);
            act(menu);
        } else if (type == IN_TOUCH) {
            if (y >= MENU_Y0 && y <= MENU_Y1) {
                int item = (x - MENU_X0) / MENU_STEP;
                // only the drawn buttons count, not the gutters between and around them
                if (x < MENU_X0 || item >= MENU_COUNT || x > MENU_X0 + item * MENU_STEP + MENU_BTN_W) break;
                menu = (u8)item;
                press(item);
                act(item);
            } else if (y < HEADER_Y1) {
                go(SC_INFO);
                cue(SFX_INFO);
            } else if (x >= PET_X0 + pet_x && x <= PET_X1 + pet_x && y >= PET_Y0 && y <= PET_Y1) {
                pet();
            }
        }
        break;
    case SC_GAME:
        if (type == IN_TOUCH) {
            int best = -1, bestd = 1 << 30;
            for (int i = 0; i < GAME_TARGETS; i++) {   // where the star is drawn right now
                int ty = tgt[i].y + tgt[i].vy * (int)acc_ms / STEP_MS;
                int dx = iabs(x - tgt[i].x), dy = iabs(y - ty);
                if (tgt[i].alive && dx <= TARGET_HIT && dy <= TARGET_HIT && dx + dy < bestd) {
                    best = i;
                    bestd = dx + dy;
                }
            }
            pop(best);
        } else if (type == IN_NEXT) {
            sel = (u8)((sel + 1) % GAME_TARGETS);
            dirty = 1;
            cue(SFX_TAP);
        } else if (type == IN_SELECT) {    // the selected star, or else the highest one in view
            pop(visible(sel) ? sel : highest_visible());
        } else if (type == IN_SHAKE) {     // shake pops the highest star in view
            pop(highest_visible());
        }
        break;
    case SC_INFO:
        if (type == IN_TOUCH && x >= SOUND_X0 && y <= SOUND_Y1) {   // sound on/off
            if (volume() == VOL_OFF) {     // on again, even if a grown-up had set "off"
                s.muted = 0;
                if (s.volume == VOL_OFF) s.volume = VOL_NORMAL;
            } else {
                s.muted = 1;
            }
            want_save = 1;
            dirty = 1;
            scene_ms = 0;                  // keep the info screen up a little longer
            cue(SFX_TAP);
            break;
        }
        go(SC_HOME);
        break;
    case SC_SLEEP:                         // waking her up early is allowed
        wake();
        break;
    case SC_REFUSE:                        // the big pillow: let her sleep right away
        if (type == IN_SELECT || (type == IN_TOUCH && x >= 40 && x <= 328 && y >= 352)) {   // pillow button
            menu = MENU_SLEEP;
            act(MENU_SLEEP);
        } else {
            go(SC_HOME);
        }
        break;
    default:                               // hatching and care animations play out in full
        break;
    }
}

// ---- cosmetic simulation, one STEP_MS at a time ------------------------------------------

void Game::step() {
    bool changed = false;
    for (int i = 0; i < NPART; i++) {
        Part& p = part[i];
        if (p.kind == P_NONE) continue;
        changed = true;
        p.x = (i16)(p.x + p.vx * 2);
        p.y = (i16)(p.y + p.vy * 2);
        if (p.kind == P_CRUMB || p.kind == P_CONFETTI) { if (p.vy < 6) p.vy++; }       // gravity
        if (p.kind == P_BUBBLE) p.x = (i16)(p.x + (((p.life >> 1) & 1) ? 1 : -1));      // wobble
        if (--p.life == 0 || p.y < -24 || p.y > SCREEN_H + 24) p.kind = P_NONE;
    }

    vel_x = 0;
    switch (scene) {
    case SC_EGG: case SC_AWAY:
        if (wobbling) { changed = true; if (due(wobble_end)) wobbling = 0; }
        break;
    case SC_LEAVE:                         // a word first, then off to the right
        facing = 1;
        if (scene_ms > 1200 + FRIEND_MS) { vel_x = 12; pet_x = (i16)(pet_x + vel_x); }
        if (scene_ms == 1200 + FRIEND_MS - 400) burst(P_HEART, SCREEN_W / 2 + 90, 150, 3);   // hug
        changed = true;
        break;
    case SC_RETURN:                        // in from the left, then a happy hello
        facing = 1;
        if (pet_x < 0) {
            vel_x = (i8)(pet_x + 15 > 0 ? -pet_x : 15);
            pet_x = (i16)(pet_x + vel_x);
            if (pet_x == 0) burst(P_HEART, SCREEN_W / 2 + 42, 112, 4);
        }
        changed = true;
        break;
    case SC_HOME: {
        if (petting && due(pet_react_end)) { petting = 0; changed = true; }
        if (petting) break;
        Mood m = mood();
        bool lively = m == M_HAPPY || m == M_IDLE;        // needy unicorns stay put and blink
        if (!lively && (idle_act == IDLE_WALK || idle_act == IDLE_HOP)) { idle_act = IDLE_NONE; changed = true; }
        if (idle_act == IDLE_NONE && due(idle_next)) {
            u32 r = crandom() % 100;
            if (r < 40 || !lively) {
                idle_act = IDLE_BLINK;
                idle_end = clock_ms + 160;
            } else if (r < 80) {
                int t = (int)(crandom() % (2 * WALK_RANGE / WALK_SPEED + 1)) * WALK_SPEED - WALK_RANGE;
                if (iabs(t - pet_x) < 12) t = pet_x > 0 ? -WALK_RANGE : WALK_RANGE;
                walk_to = (i16)t;
                facing = (i8)(t > pet_x ? 1 : -1);
                idle_act = IDLE_WALK;
            } else {
                idle_act = IDLE_HOP;
                idle_end = clock_ms + 600;
            }
            changed = true;
        }
        if (idle_act == IDLE_WALK) {
            int d = walk_to - pet_x;
            vel_x = (i8)(d > 0 ? (d < WALK_SPEED ? d : WALK_SPEED) : (d < -WALK_SPEED ? -WALK_SPEED : d));
            pet_x = (i16)(pet_x + vel_x);
            if (pet_x == walk_to) idle_act = IDLE_NONE;
            changed = true;
        } else if (idle_act != IDLE_NONE && due(idle_end)) {
            idle_act = IDLE_NONE;
            changed = true;
        }
        if (idle_act == IDLE_NONE && changed && due(idle_next))
            idle_next = clock_ms + 3000 + crandom() % 4000;
        break;
    }
    case SC_EAT:                           // crumbs fly on every bite
        if ((scene_ms / STEP_MS) % 3 == 0) burst(P_CRUMB, 278, 326, 2);
        break;
    case SC_WASH:
        if ((scene_ms / STEP_MS) % 2 == 0) burst(P_BUBBLE, 110 + (int)(crandom() % 150), 300, 1);
        if (!celebrated && scene_ms + 700 >= ACTION_MS) {   // squeaky clean
            burst(P_SPARKLE, SCREEN_W / 2, 220, 5);
            celebrated = 1;
        }
        changed = true;
        break;
    case SC_GAME:
        for (int i = 0; i < GAME_TARGETS; i++) {
            tgt[i].y = (i16)(tgt[i].y + tgt[i].vy);
            if (tgt[i].y < 70) spawn_target(i, SCREEN_H + 20 + (int)(crandom() % 40));
        }
        changed = true;
        break;
    }
    if (changed) dirty = 1;
}

// Life time: whatever is left after the egg hatches counts as the unicorn's life.
bool Game::counts_as_play() const {
    return s.hatched && scene != SC_EGG && scene != SC_HATCH && scene != SC_LEAVE && scene != SC_AWAY &&
           scene != SC_RETURN && scene != SC_SETTINGS;
}

void Game::advance(u32 ms) {
    clock_ms += ms;
    scene_ms += ms;
    anim_ms += ms;
    idle_ms += ms;
    save_ms += ms;
    if (screen_on) {                       // screen time, reset by a long enough break
        dark_ms = 0;
        if (counts_as_play()) play_ms += ms;
    } else {
        dark_ms += ms;
        if (dark_ms >= REST_RESET_MS) play_ms = 0;
    }
    if (scene == SC_AWAY && away_ms && !(s.away_until && wall_s))   // no clock: count powered time
        away_ms = away_ms > ms ? away_ms - ms : 0;
    u32 grow = 0;
    if (!s.hatched) {
        s.egg_ms += ms;
        if (s.egg_ms >= EGG_HATCH_MS && scene == SC_EGG) { grow = s.egg_ms - EGG_HATCH_MS; hatch(); }
    } else {
        grow = ms;
    }
    minute_ms += grow;
    while (minute_ms >= 60000) {
        minute_ms -= 60000;
        minute();
        if (scene == SC_SLEEP && s.stat[ENERGY] >= MAX_STAT) {   // slept enough: wakes up rested
            s.stat[HAPPY] = (u16)clamp(s.stat[HAPPY] + TREAT_HAPPY);
            want_save = 1;
            wake();
        }
    }
    sec_ms += grow;
    while (sec_ms >= 1000) { sec_ms -= 1000; s.age_s++; }
    if (scene == SC_SLEEP) {               // a short nap already helps
        sleep_ms += ms;
        if (!napped && sleep_ms >= NAP_MS) {
            napped = 1;
            s.stat[ENERGY] = (u16)clamp(s.stat[ENERGY] + NAP_BONUS);
            dirty = 1;
            if (s.stat[ENERGY] >= MAX_STAT) { want_save = 1; wake(); }
        }
    }
}

void Game::scene_timers() {
    switch (scene) {
    case SC_EAT: case SC_WASH:
        if (scene_ms >= ACTION_MS) go(SC_HOME);
        break;
    case SC_GAME:
        if (scene_ms >= GAME_MS) {         // joy does not depend on the score: stars are just counted
            want_save = 1;
            go(SC_PLAY);                   // celebration: jump after the ball, star count shown
            cue(SFX_CHEER);
            burst(P_CONFETTI, SCREEN_W / 2, 180, 8);
        }
        break;
    case SC_HOME:                          // enough screen time: she has things to do
        if (play_ms >= play_limit_ms()) start_leave();
        break;
    case SC_LEAVE:
        if (scene_ms >= LEAVE_MS) {
            away_ms = break_ms();
            s.away_until = wall_s ? wall_s + break_ms() / 1000 : 0;
            want_save = 1;                 // saved right away, so a power cut is no shortcut
            idle_ms = 0;
            pet_x = 0;
            go(SC_AWAY);
        }
        break;
    case SC_AWAY:                          // back as soon as someone is looking
        if (away_ms == 0 && screen_on) { come_back(); }
        break;
    case SC_RETURN:
        if (scene_ms >= RETURN_MS) {
            play_ms = 0;
            want_save = 1;
            go(SC_HOME);
            idle_next = clock_ms + 3000;   // settle in before wandering off again
        }
        break;
    case SC_PLAY:   if (scene_ms >= CELEBRATE_MS) go(SC_HOME); break;
    case SC_FEED:   if (scene_ms >= FEED_PICK_MS) eat(sel); break;
    case SC_REFUSE: if (scene_ms >= REFUSE_MS) go(SC_HOME); break;
    case SC_HATCH:  if (scene_ms >= HATCH_MS) go(SC_HOME); break;
    case SC_INFO:   if (scene_ms >= INFO_MS) go(SC_HOME); break;
    case SC_SLEEP:                          // energy topped up some other way (debug slider)
        if (s.stat[ENERGY] >= MAX_STAT) { want_save = 1; wake(); }
        break;
    }
}

// Fixed-step simulation: the world only moves in whole STEP_MS steps, so one big tick and any
// mix of small ones end in exactly the same state. While the screen is dark at home or asleep
// nothing visible can happen, so that time is fast-forwarded in one go (hours of light-sleep).
u32 Game::tick(u32 dt) {
    acc_ms += dt;
    while (acc_ms >= STEP_MS) {
        if (!screen_on && s.hatched && (scene == SC_HOME || scene == SC_SLEEP || scene == SC_AWAY)) {
            u32 n = acc_ms - acc_ms % STEP_MS;
            if (scene == SC_SLEEP && !napped && sleep_ms < NAP_MS && n > NAP_MS - sleep_ms)
                n = NAP_MS - sleep_ms;     // stop exactly at the nap, like small steps would
            advance(n);
            acc_ms -= n;
            continue;
        }
        advance(STEP_MS);
        acc_ms -= STEP_MS;
        scene_timers();
        if (screen_on) step();             // cosmetics only run while someone can see them
        bool busy = scene == SC_EAT || scene == SC_PLAY || scene == SC_WASH || scene == SC_HATCH ||
                    scene == SC_GAME || scene == SC_LEAVE || scene == SC_RETURN;
        u32 off = scene == SC_AWAY ? AWAY_SCREEN_OFF_MS : SCREEN_OFF_MS;
        if (screen_on && !busy && idle_ms >= off) screen_on = 0;
    }

    u32 period = 900;                      // breathing
    switch (scene) {
    case SC_EAT: case SC_WASH: case SC_PLAY: case SC_GAME: case SC_HATCH: period = 300; break;
    case SC_EGG: period = 500; break;
    case SC_HOME: if (idle_act == IDLE_WALK) period = 250; else if (idle_act == IDLE_HOP) period = 300; break;
    }
    u8 f = (u8)((anim_ms / period) & 1);
    if (f != frame) { frame = f; dirty = 1; }

    u32 flags = 0;
    if (save_ms >= AUTOSAVE_MS) { save_ms = 0; want_save = 1; }
    if (want_save) { flags |= F_SAVE; want_save = 0; }
    if (sfx && s.muted) sfx = SFX_NONE;
    if (sfx) flags |= F_SOUND;
    if (screen_on) {
        flags |= F_SCREEN_ON;
        // render() interpolates between steps (acc_ms), so anything that moves wants a fresh
        // frame on every call; the info screen and the door are still pictures
        bool still = scene == SC_INFO || (scene == SC_AWAY && !wobbling);
        if (dirty || !still) { flags |= F_REDRAW; dirty = 0; }
    }
    return flags;
}

}  // namespace up
