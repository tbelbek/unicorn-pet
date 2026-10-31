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
    if (!data || len < SAVE_SIZE) return false;
    Save tmp;
    copy((u8*)&tmp, data, SAVE_SIZE);
    if (tmp.version != SAVE_VERSION || tmp.character > 3) return false;
    if (fnv((const u8*)&tmp, SAVE_SIZE - 4) != tmp.check) return false;
    for (int i = 0; i < NSTATS; i++) if (tmp.stat[i] > MAX_STAT) return false;
    s = tmp;
    go(s.hatched ? SC_HOME : SC_EGG);
    screen_on = 1;
    return true;
}

u32 Game::save(u8* out) const {
    Save tmp = s;
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
    static const u8 LIFE[] = {0, 14, 6, 20, 8, 10, 16};
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
    burst(P_SPARKLE, tgt[i].x, tgt[i].y, 4);
    burst(P_CONFETTI, tgt[i].x, tgt[i].y, 3);
    cue(SFX_POP);
    spawn_target(i, SCREEN_H - 10 + (int)(crandom() % 30));   // the next one peeks in at the bottom
}

// ---- care ----------------------------------------------------------------------------------

void Game::pet() {
    pet_react_end = clock_ms + PET_REACT_MS;
    petting = 1;
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

void Game::act(int item) {
    u16* v = s.stat;
    pet_x = 0;                             // actions are staged centre-front
    facing = 1;
    switch (item) {
    case MENU_FEED:
        v[HUNGER] = (u16)clamp(v[HUNGER] + FEED_GAIN);
        v[HAPPY] = (u16)clamp(v[HAPPY] + TREAT_HAPPY);
        treat = (u8)((treat + 1) % TREAT_COUNT);     // apple, strawberry, carrot in turn
        go(SC_EAT);
        cue(SFX_EAT);
        break;
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
    idle_ms = 0;
    if (!screen_on) {                      // first touch only wakes the screen
        screen_on = 1;
        dirty = 1;
        return;
    }
    switch (scene) {
    case SC_EGG:                           // taps and shakes both nudge the egg
        dirty = 1;
        wobble_end = clock_ms + WOBBLE_MS;
        wobbling = 1;
        if (++taps >= EGG_TAPS) hatch();
        else cue(SFX_TAP);
        break;
    case SC_HOME:
        if (type == IN_NEXT) {
            menu = (u8)((menu + 1) % MENU_COUNT);
            dirty = 1;
            cue(SFX_TAP);
        } else if (type == IN_SHAKE) {     // shaking the toy = let's play
            menu = MENU_PLAY;
            act(MENU_PLAY);
        } else if (type == IN_SELECT) {
            act(menu);
        } else if (type == IN_TOUCH) {
            if (y >= MENU_Y0 && y <= MENU_Y1) {
                int item = (x - MENU_X0) / MENU_STEP;
                // only the drawn buttons count, not the gutters between and around them
                if (x < MENU_X0 || item >= MENU_COUNT || x > MENU_X0 + item * MENU_STEP + MENU_BTN_W) break;
                menu = (u8)item;
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
            for (int i = 0; i < GAME_TARGETS; i++) {
                int dx = iabs(x - tgt[i].x), dy = iabs(y - tgt[i].y);
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
            s.muted = !s.muted;
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
    case SC_REFUSE:
        go(SC_HOME);
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

    switch (scene) {
    case SC_EGG:
        if (wobbling) { changed = true; if (due(wobble_end)) wobbling = 0; }
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
            pet_x = (i16)(pet_x + (d > 0 ? (d < WALK_SPEED ? d : WALK_SPEED) : (d < -WALK_SPEED ? -WALK_SPEED : d)));
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
void Game::advance(u32 ms) {
    clock_ms += ms;
    scene_ms += ms;
    anim_ms += ms;
    idle_ms += ms;
    save_ms += ms;
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
}

void Game::scene_timers() {
    switch (scene) {
    case SC_EAT: case SC_WASH:
        if (scene_ms >= ACTION_MS) go(SC_HOME);
        break;
    case SC_GAME:
        if (scene_ms >= GAME_MS) {
            int bonus = score * GAME_BONUS;
            s.stat[HAPPY] = (u16)clamp(s.stat[HAPPY] + (bonus > GAME_BONUS_MAX ? GAME_BONUS_MAX : bonus));
            want_save = 1;
            go(SC_PLAY);                   // celebration: jump after the ball, star count shown
            cue(SFX_CHEER);
            burst(P_CONFETTI, SCREEN_W / 2, 180, 8);
        }
        break;
    case SC_PLAY:   if (scene_ms >= CELEBRATE_MS) go(SC_HOME); break;
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
        if (!screen_on && s.hatched && (scene == SC_HOME || scene == SC_SLEEP)) {
            u32 n = acc_ms - acc_ms % STEP_MS;
            advance(n);
            acc_ms -= n;
            continue;
        }
        advance(STEP_MS);
        acc_ms -= STEP_MS;
        scene_timers();
        if (screen_on) step();             // cosmetics only run while someone can see them
        bool busy = scene == SC_EAT || scene == SC_PLAY || scene == SC_WASH || scene == SC_HATCH ||
                    scene == SC_GAME;
        if (screen_on && !busy && idle_ms >= SCREEN_OFF_MS) screen_on = 0;
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
        if (dirty) { flags |= F_REDRAW; dirty = 0; }
    }
    return flags;
}

}  // namespace up
