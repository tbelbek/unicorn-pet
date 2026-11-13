// WebAssembly entry: the same core the firmware runs, plus a flat C API for emu.js.
#include "../core/game.cpp"
#include "../core/render.cpp"
#include "../core/sound.cpp"
#include "../core/qr.cpp"

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
static u8 SAVEBUF[192];  // >= SAVE_SIZE; also takes older, shorter saves
static_assert(sizeof SAVEBUF >= SAVE_SIZE, "the save fits");

#define API(name) extern "C" __attribute__((export_name(#name)))

API(up_init) void up_init(u32 seed) { G.init(seed); render(G, FBUF); }
API(up_save_buf) u8* up_save_buf() { return SAVEBUF; }
API(up_save_buf_len) int up_save_buf_len() { return (int)sizeof SAVEBUF; }
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
static int taken_line, taken_clip;
API(up_take_sfx) int up_take_sfx() { return G.take_sfx(&taken_line, &taken_clip); }
API(up_taken_line) int up_taken_line() { return taken_line; }   // what she says with that cue (V_NONE: nothing)
API(up_taken_clip) int up_taken_clip() { return taken_clip; }   // a routines_tr.bin clip instead (-1: none)
API(up_name_line) int up_name_line(int sfx, int line) { return name_line(sfx, line); }
API(up_lang) int up_lang() { return G.s.lang; }
API(up_melody) const Note* up_melody(int sfx) { int n; return melody(sfx, &n); }
API(up_melody_len) int up_melody_len(int sfx) { int n; melody(sfx, &n); return n; }
API(up_muted) int up_muted() { return G.volume() == VOL_OFF; }   // sound off (old saves' mute is folded in)
API(up_play_ms) u32 up_play_ms() { return G.play_ms; }
API(up_away_ms) u32 up_away_ms() { return G.away_ms; }
API(up_set_wall) void up_set_wall(u32 local_s, int calendar, u32 utc_s) { G.set_wall(local_s, calendar != 0, utc_s); }
API(up_phase) int up_phase() { return G.phase(); }
API(up_wall) u32 up_wall() { return G.wall_s; }
API(up_stage) int up_stage() { return G.stage(); }
API(up_care_days) int up_care_days() { return G.s.care_days; }
API(up_set_care_days) void up_set_care_days(int d) { G.s.care_days = (u16)(d < 0 ? 0 : d > CARE_DAYS_MAX ? CARE_DAYS_MAX : d); }
API(up_greeting) int up_greeting() { return G.greeting; }
API(up_dream) int up_dream() { return G.dream(); }
API(up_set_play_ms) void up_set_play_ms(u32 ms) { G.play_ms = ms; }       // test button
API(up_volume) int up_volume() { return G.volume(); }
API(up_brightness) int up_brightness() { return G.screen_brightness(); }
API(up_set_battery) void up_set_battery(int pct) { G.set_battery(pct); }
API(up_set_plugged) void up_set_plugged(int usb) { G.set_plugged(usb != 0); }
API(up_set_boot) void up_set_boot(int down) { G.set_boot(down != 0); }
// WiFi setup: strings travel through up_text_buf() (NUL-separated)
static char TEXT[256];
API(up_text_buf) char* up_text_buf() { return TEXT; }
API(up_wifi_req) int up_wifi_req() { return G.wifi_req; }
API(up_cloud_x) int up_cloud_x() { return G.cloud_x; }
API(up_wifi_arg) int up_wifi_arg() { return G.wifi_arg; }
API(up_wifi_take) void up_wifi_take() { G.wifi_req = WREQ_NONE; }
API(up_wifi_stage) int up_wifi_stage() { return G.wifi_stage; }
API(up_wifi_session) void up_wifi_session() { int n = 0; while (TEXT[n]) n++; G.wifi_session(TEXT, TEXT + n + 1); }
API(up_wifi_status) void up_wifi_status(int stage) { G.wifi_status(stage, TEXT[0] ? TEXT : 0); }
API(up_wifi_list_clear) void up_wifi_list_clear() { G.wifi_list_clear(); }
API(up_wifi_list_add) void up_wifi_list_add(int fixed) { G.wifi_list_add(TEXT, fixed != 0); }
API(up_plugged) int up_plugged() { return G.plugged; }
API(up_set_tilt) void up_set_tilt(int v) { G.set_tilt(v); }
API(up_accessory) int up_accessory() { return G.s.accessory; }
// firmware update: the page's mock plays the board's ota.cpp (manifest, download, restart)
API(up_set_fw_version) void up_set_fw_version(int v) { G.fw_version = (u16)v; }
API(up_update_req) int up_update_req() { return G.update_req; }
API(up_update_take) void up_update_take() { G.update_req = UREQ_NONE; }
API(up_update_stage) int up_update_stage() { return G.update_stage; }
API(up_update_view) int up_update_view() { return G.update_view(); }
API(up_update_status) void up_update_status(int stage, int pct, int ver) { G.update_status(stage, pct, ver); }
// her daily routine: the page plays the board's audio.cpp (record) and net.cpp (the listener), or the buttons
API(up_set_packs) void up_set_packs(int routines, int names) { G.has_routines = (u8)routines; G.has_names = (u8)names; }
API(up_listen_req) int up_listen_req() { return G.listen_req; }
API(up_listen_kind) int up_listen_kind() { return G.listen_kind; }
API(up_listen_q) int up_listen_q() { return G.listen_q; }
API(up_listen_take) void up_listen_take() { G.listen_req = LREQ_NONE; }
API(up_listen_started) void up_listen_started() { G.listen_started(); }
API(up_listen_result) void up_listen_result(int a) { G.listen_result(a); }
API(up_level_result) void up_level_result(int db, int pct) { G.level_result(db, pct); }
API(up_q_key) const char* up_q_key(int q) { return q >= 0 && q < Q_COUNT ? Q_KEY[q] : ""; }
API(up_q_count) int up_q_count() { return Q_COUNT; }
API(up_r_stage) int up_r_stage() { return G.r_stage; }
API(up_r_q) int up_r_q() { return G.r_q; }
API(up_r_ans) int up_r_ans(int q) { return q >= 0 && q < Q_COUNT ? G.s.r_ans[q] : 0; }
API(up_stars_today) int up_stars_today() { return G.stars_today(); }
API(up_link) int up_link(int sample_waiting) { return G.link(sample_waiting != 0); }
API(up_tv_day) int up_tv_day() { return G.s.tv_day; }
// settings sync with the parent page (server/API.md): a Settings struct the page reads and writes
static Settings SET;
API(up_settings_buf) u16* up_settings_buf() { return SET.v; }   // v[K_COUNT], hol[HOL_MAX][2], then tv_day
API(up_settings_count) int up_settings_count() { return K_COUNT; }
API(up_get_settings) void up_get_settings() { SET = G.settings(); }
API(up_apply_settings) void up_apply_settings(u32 rev, int present) { G.apply_settings(rev, SET, (u16)present); }
API(up_cfg_rev) u32 up_cfg_rev() { return G.s.cfg_rev; }
API(up_cfg_dirty) int up_cfg_dirty() { return G.s.cfg_dirty; }
static SyncMark MARK;                    // what the sync in flight carries (one at a time)
API(up_sync_mark) int up_sync_mark() { MARK = G.sync_mark(); return MARK.dirty; }
API(up_settings_synced) void up_settings_synced() { G.settings_synced(MARK); }
