// Game rules: needs decay over powered time, four actions, an egg that hatches.
// The unicorn never dies: low needs only change its face.
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

u32 Game::random() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

void Game::init(u32 seed) {
    u8* raw = (u8*)this;
    for (u32 i = 0; i < sizeof(Game); i++) raw[i] = 0;
    rng = seed ? seed : 0x9E3779B9u;
    s.version = SAVE_VERSION;
    s.character = (u8)(random() % 4);
    for (int i = 0; i < NSTATS; i++) s.stat[i] = 8000;
    scene = SC_EGG;
    screen_on = 1;
    dirty = 1;
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

void Game::go(u8 sc) {
    scene = sc;
    scene_ms = anim_ms = 0;
    frame = 0;
    dirty = 1;
}

void Game::hatch() {
    s.hatched = 1;
    want_save = 1;
    go(SC_HATCH);
    cue(SFX_HATCH);
}

int Game::take_sfx() {
    int id = s.muted ? (int)SFX_NONE : (int)sfx;
    sfx = SFX_NONE;
    return id;
}

Mood Game::mood() const {
    const u16* v = s.stat;
    if (v[HUNGER] < NEED_LOW) return M_HUNGRY;
    if (v[ENERGY] < NEED_LOW) return M_SLEEPY;
    if (v[CLEAN] < NEED_LOW || v[HAPPY] < NEED_LOW) return M_SAD;
    if (v[HUNGER] >= 7000 && v[HAPPY] >= 7000 && v[ENERGY] >= 7000 && v[CLEAN] >= 7000) return M_HAPPY;
    return M_IDLE;
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
    switch (item) {
    case MENU_FEED:
        v[HUNGER] = (u16)clamp(v[HUNGER] + FEED_GAIN);
        v[HAPPY] = (u16)clamp(v[HAPPY] + TREAT_HAPPY);
        go(SC_EAT);
        cue(SFX_EAT);
        break;
    case MENU_PLAY:
        if (v[ENERGY] < PLAY_MIN_ENERGY) { go(SC_REFUSE); cue(SFX_REFUSE); return; }   // too tired
        v[HAPPY] = (u16)clamp(v[HAPPY] + PLAY_HAPPY);
        v[ENERGY] = (u16)clamp(v[ENERGY] - PLAY_COST);
        v[HUNGER] = (u16)clamp(v[HUNGER] - PLAY_HUNGER);
        v[CLEAN] = (u16)clamp(v[CLEAN] - PLAY_DIRT);
        go(SC_PLAY);
        cue(SFX_PLAY);
        break;
    case MENU_SLEEP:
        go(SC_SLEEP);
        cue(SFX_SLEEP);
        break;
    case MENU_WASH:
        v[CLEAN] = MAX_STAT;
        v[HAPPY] = (u16)clamp(v[HAPPY] + TREAT_HAPPY);
        go(SC_WASH);
        cue(SFX_WASH);
        break;
    }
    want_save = 1;
}

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
                item = item < 0 ? 0 : item >= MENU_COUNT ? MENU_COUNT - 1 : item;
                menu = (u8)item;
                act(item);
            } else if (x >= PET_X0 && x <= PET_X1 && y >= PET_Y0 && y <= PET_Y1) {
                go(SC_INFO);
                cue(SFX_INFO);
            }
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
        go(SC_HOME);
        cue(SFX_WAKE);
        break;
    case SC_REFUSE:
    case SC_HATCH:
        go(SC_HOME);
        break;
    default:                               // let eat/play/wash animations finish
        break;
    }
}

u32 Game::tick(u32 dt) {
    scene_ms += dt;
    anim_ms += dt;
    idle_ms += dt;
    save_ms += dt;

    if (!s.hatched) {
        s.egg_ms += dt;
        if (s.egg_ms >= EGG_HATCH_MS && scene == SC_EGG) hatch();
    } else {
        minute_ms += dt;
        while (minute_ms >= 60000) { minute_ms -= 60000; minute(); }
        sec_ms += dt;
        while (sec_ms >= 1000) { sec_ms -= 1000; s.age_s++; }
    }

    switch (scene) {
    case SC_EAT: case SC_PLAY: case SC_WASH:
        if (scene_ms >= ACTION_MS) go(SC_HOME);
        break;
    case SC_REFUSE: if (scene_ms >= REFUSE_MS) go(SC_HOME); break;
    case SC_HATCH:  if (scene_ms >= HATCH_MS) go(SC_HOME); break;
    case SC_INFO:   if (scene_ms >= INFO_MS) go(SC_HOME); break;
    case SC_SLEEP:
        if (s.stat[ENERGY] >= MAX_STAT) {   // slept enough: wakes up rested
            s.stat[HAPPY] = (u16)clamp(s.stat[HAPPY] + TREAT_HAPPY);
            want_save = 1;
            go(SC_HOME);
            cue(SFX_WAKE);
        }
        break;
    }

    u32 period = 600;
    if (scene == SC_EAT || scene == SC_PLAY || scene == SC_WASH) period = 300;
    else if (scene == SC_SLEEP) period = 900;
    else if (scene == SC_EGG) period = 500;
    u8 f = (u8)((anim_ms / period) & 1);
    if (f != frame) { frame = f; dirty = 1; }

    bool busy = scene == SC_EAT || scene == SC_PLAY || scene == SC_WASH || scene == SC_HATCH;
    if (screen_on && !busy && idle_ms >= SCREEN_OFF_MS) screen_on = 0;

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
