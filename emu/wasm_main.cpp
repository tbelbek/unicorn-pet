// WebAssembly entry: the same core the firmware runs, plus a flat C API for emu.js.
#include "../core/game.cpp"
#include "../core/render.cpp"
#include "../core/sound.cpp"

using namespace up;

// clang may lower struct copies to these even under -nostdlib
extern "C" {
void* memcpy(void* d, const void* s, unsigned long n) {
    u8* dp = (u8*)d; const u8* sp = (const u8*)s;
    while (n--) *dp++ = *sp++;
    return d;
}
void* memset(void* d, int c, unsigned long n) {
    u8* dp = (u8*)d;
    while (n--) *dp++ = (u8)c;
    return d;
}
void* memmove(void* d, const void* s, unsigned long n) {
    u8* dp = (u8*)d; const u8* sp = (const u8*)s;
    if (dp < sp) while (n--) *dp++ = *sp++;
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}
}

static Game G;
static u16 FBUF[SCREEN_W * SCREEN_H];
static u8 SAVEBUF[64];   // >= SAVE_SIZE; also takes older, shorter saves

#define API(name) extern "C" __attribute__((export_name(#name)))

API(up_init) void up_init(u32 seed) { G.init(seed); render(G, FBUF); }
API(up_save_buf) u8* up_save_buf() { return SAVEBUF; }
API(up_save_size) u32 up_save_size() { return SAVE_SIZE; }
API(up_save) u32 up_save() { return G.save(SAVEBUF); }
API(up_load) int up_load(u32 len) { int ok = G.load(SAVEBUF, len); render(G, FBUF); return ok; }
API(up_fb) u16* up_fb() { return FBUF; }
API(up_input) void up_input(int type, int x, int y) { G.input(type, x, y); }
API(up_tick) u32 up_tick(u32 dt) {
    u32 f = G.tick(dt);
    if (f & F_REDRAW) render(G, FBUF);
    return f;
}
API(up_stat) int up_stat(int i) { return G.stat(i); }
API(up_set_stat) void up_set_stat(int i, int v) { G.set_stat(i, v); }
API(up_scene) int up_scene() { return G.scene; }
API(up_mood) int up_mood() { return G.mood(); }
API(up_menu) int up_menu() { return G.menu; }
API(up_screen_on) int up_screen_on() { return G.screen_on; }
API(up_character) int up_character() { return G.s.character; }
API(up_age_s) u32 up_age_s() { return G.s.age_s; }
API(up_hatched) int up_hatched() { return G.s.hatched; }
API(up_max_stat) int up_max_stat() { return MAX_STAT; }
API(up_take_sfx) int up_take_sfx() { return G.take_sfx(); }
API(up_melody) const Note* up_melody(int sfx) { int n; return melody(sfx, &n); }
API(up_melody_len) int up_melody_len(int sfx) { int n; melody(sfx, &n); return n; }
API(up_muted) int up_muted() { return G.s.muted; }
API(up_play_ms) u32 up_play_ms() { return G.play_ms; }
API(up_away_ms) u32 up_away_ms() { return G.away_ms; }
API(up_set_wall) void up_set_wall(u32 local_s, int calendar) { G.set_wall(local_s, calendar != 0); }
API(up_phase) int up_phase() { return G.phase(); }
API(up_wall) u32 up_wall() { return G.wall_s; }
API(up_stage) int up_stage() { return G.stage(); }
API(up_care_days) int up_care_days() { return G.s.care_days; }
API(up_set_care_days) void up_set_care_days(int d) { G.s.care_days = (u16)(d < 0 ? 0 : d > CARE_DAYS_MAX ? CARE_DAYS_MAX : d); }
API(up_greeting) int up_greeting() { return G.greeting; }
API(up_dream) int up_dream() { return G.dream(); }
API(up_set_play_ms) void up_set_play_ms(u32 ms) { G.play_ms = ms; }       // test button
API(up_volume) int up_volume() { return G.volume(); }
API(up_brightness) int up_brightness() { return G.brightness(); }
API(up_set_battery) void up_set_battery(int pct) { G.set_battery(pct); }
API(up_set_plugged) void up_set_plugged(int usb) { G.set_plugged(usb != 0); }
API(up_set_boot) void up_set_boot(int down) { G.set_boot(down != 0); }
API(up_plugged) int up_plugged() { return G.plugged; }
API(up_set_tilt) void up_set_tilt(int v) { G.set_tilt(v); }
API(up_accessory) int up_accessory() { return G.s.accessory; }
