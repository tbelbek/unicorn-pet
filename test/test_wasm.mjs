// Drives emu/unicorn.wasm through every event, the way the browser emulator does.
//   node test/test_wasm.mjs
import { readFileSync } from "node:fs";
import assert from "node:assert/strict";

const bytes = readFileSync(new URL("../emu/unicorn.wasm", import.meta.url));
const mod = new WebAssembly.Module(bytes);
assert.equal(WebAssembly.Module.imports(mod).length, 0, "core must not import anything");
const w = new WebAssembly.Instance(mod, {}).exports;

const W = 368, H = 448;
const SC = { EGG: 0, HATCH: 1, HOME: 2, EAT: 3, PLAY: 4, WASH: 5, SLEEP: 6, INFO: 7, REFUSE: 8, GAME: 9,
             FEED: 13, SETTINGS: 14, EGG_PICK: 15, TOYS: 16, MUSIC: 17, CLOUD: 18, DRESS: 19 };
const IN = { NEXT: 1, SELECT: 2, TOUCH: 3 };
const ST = { HUNGER: 0, HAPPY: 1, ENERGY: 2, CLEAN: 3 };
const fb = () => new Uint16Array(w.memory.buffer, w.up_fb(), W * H);
const lit = () => fb().reduce((n, c) => n + (c !== 0), 0);
const run = (ms) => { let f = 0; for (let t = 0; t < ms; t += 100) f |= w.up_tick(100); return f; };
const tapMenu = (i) => w.up_input(IN.TOUCH, 46 + i * 92, 400);
const hash = () => { let h = 0; for (const c of fb()) h = (h * 31 + c) >>> 0; return h; };

let passed = 0;
function test(name, fn) {
  w.up_init(42);
  fn();
  passed++;
  console.log("ok  ", name);
}

test("egg renders and hatches with taps", () => {
  assert.equal(w.up_scene(), SC.EGG_PICK, "a new game starts by choosing an egg");
  w.up_input(IN.TOUCH, 268, 310); w.up_input(IN.TOUCH, 268, 310);   // highlight, then choose
  assert.equal(w.up_scene(), SC.EGG);
  assert.equal(w.up_character(), 3);
  assert.ok(lit() > 5000);
  for (let i = 0; i < 5; i++) w.up_input(IN.TOUCH, 184, 200);
  assert.equal(w.up_scene(), SC.HATCH);
  run(3100);
  assert.equal(w.up_scene(), SC.HOME);
});

const hatch = () => { for (let i = 0; i < 6; i++) w.up_input(IN.SELECT, 0, 0); run(3100); };   // choose + 5 taps

test("feed", () => {
  hatch();
  w.up_set_stat(ST.HUNGER, 2000);
  tapMenu(0);
  assert.equal(w.up_scene(), SC.FEED, "she picks the snack first");
  w.up_input(IN.TOUCH, 61 + 2 * 122, 200);            // the carrot
  assert.equal(w.up_scene(), SC.EAT);
  assert.equal(w.up_stat(ST.HUNGER), 5500);
  const f = w.up_tick(100);
  assert.ok(f & 4, "save requested after an action");
  const a = hash(); run(300); assert.notEqual(hash(), a, "eat animates");
  run(4000);
  assert.equal(w.up_scene(), SC.HOME);
});

test("play and refuse", () => {
  hatch();
  w.up_set_stat(ST.ENERGY, 6000);
  w.up_input(IN.NEXT, 0, 0);
  assert.equal(w.up_menu(), 1);
  w.up_input(IN.SELECT, 0, 0);
  assert.equal(w.up_scene(), SC.TOYS, "play opens the toy box");
  w.up_input(IN.SELECT, 0, 0);
  assert.equal(w.up_scene(), SC.GAME, "first card: the star game");
  w.up_input(IN.SELECT, 0, 0);                         // PWR pops the selected star
  run(15100);
  assert.equal(w.up_scene(), SC.PLAY, "celebration");
  run(3100);
  w.up_set_stat(ST.ENERGY, 1000);
  tapMenu(1);
  w.up_input(IN.TOUCH, 116, 148);
  assert.equal(w.up_scene(), SC.REFUSE);
});

test("sleep at night and wake rested", () => {
  hatch();
  w.up_set_stat(ST.ENERGY, 2000);
  tapMenu(2);
  w.up_tick(100);
  assert.equal(w.up_scene(), SC.SLEEP);
  // moon pixels in the sky
  const p = fb(); let moon = 0;
  for (let y = 100; y < 150; y++) for (let x = 40; x < 90; x++) moon += p[y * W + x] !== 0;
  assert.ok(moon > 100, "moon drawn");
  let minutes = 0;
  while (w.up_scene() === SC.SLEEP && minutes < 200) { w.up_tick(60000); minutes++; }
  assert.equal(w.up_scene(), SC.HOME);
  assert.ok(minutes < 120);
});

test("wash", () => {
  hatch();
  w.up_set_stat(ST.CLEAN, 500);
  tapMenu(3);
  assert.equal(w.up_scene(), SC.WASH);
  assert.equal(w.up_stat(ST.CLEAN), w.up_max_stat());
});

test("tapping the unicorn pets her; the header opens the info face", () => {
  hatch();
  run(2100);
  const before = w.up_stat(ST.HAPPY);
  w.up_input(IN.TOUCH, 184, 230);
  assert.equal(w.up_scene(), SC.HOME);
  assert.ok(w.up_stat(ST.HAPPY) > before, "petting cheers her up");
  w.up_input(IN.TOUCH, 184, 60);
  assert.equal(w.up_scene(), SC.INFO);
});

test("screen off, then first touch only wakes", () => {
  hatch();
  w.up_input(IN.NEXT, 0, 0);
  run(21000);
  assert.equal(w.up_screen_on(), 0);
  const m = w.up_menu();
  w.up_input(IN.NEXT, 0, 0);
  assert.equal(w.up_screen_on(), 1);
  assert.equal(w.up_menu(), m);
});

test("save/load through the shared buffer", () => {
  hatch();
  w.up_set_stat(ST.HAPPY, 1234);
  const n = w.up_save();
  assert.equal(n, w.up_save_size());
  const blob = new Uint8Array(w.memory.buffer, w.up_save_buf(), n).slice();
  w.up_init(7);
  new Uint8Array(w.memory.buffer, w.up_save_buf(), n).set(blob);
  assert.equal(w.up_load(n), 1);
  assert.equal(w.up_stat(ST.HAPPY), 1234);
  assert.equal(w.up_scene(), SC.HOME);
  blob[8] ^= 1;
  new Uint8Array(w.memory.buffer, w.up_save_buf(), n).set(blob);
  assert.equal(w.up_load(n), 0, "corrupt save rejected");
});

test("sound cues and melodies cross the wasm boundary", () => {
  hatch();
  w.up_take_sfx();
  tapMenu(0);
  w.up_input(IN.TOUCH, 61, 200);
  const f = w.up_tick(0);
  assert.ok(f & 8, "F_SOUND after feeding");
  const id = w.up_take_sfx();
  assert.equal(id, 3, "SFX_EAT");
  const n = w.up_melody_len(id);
  const notes = new Uint16Array(w.memory.buffer, w.up_melody(id), n * 2);
  assert.ok(n > 0 && notes[0] > 0 && notes[1] > 0, "first note has pitch and length");
  assert.equal(w.up_take_sfx(), 0, "delivered once");
});

test("shake plays, and wakes a sleeping unicorn", () => {
  hatch();
  w.up_set_stat(ST.ENERGY, 6000);
  w.up_input(4, 0, 0);
  assert.equal(w.up_scene(), SC.GAME);
  run(18200);
  tapMenu(2);
  w.up_input(4, 0, 0);                             // the first shake tucks her in
  assert.equal(w.up_scene(), SC.SLEEP);
  w.up_input(4, 0, 0);
  assert.equal(w.up_scene(), SC.HOME);
});

test("the info screen has no mute switch: sound is the parent's volume", () => {
  hatch();
  w.up_input(IN.TOUCH, 184, 60);
  w.up_input(IN.TOUCH, 300, 26);                     // where the old switch was: just closes the info screen
  assert.equal(w.up_scene(), SC.HOME);
  assert.equal(w.up_muted(), 0);
  assert.equal(w.up_volume(), 0);
});

test("RTC: the break also passes while powered off", () => {
  hatch();
  w.up_set_wall(1000000);
  w.up_set_play_ms(600000);
  run(100);
  for (let i = 0; i < 60 && w.up_scene() !== 11; i++) run(100);
  assert.equal(w.up_scene(), 11, "out with friends, door shown");
  const n = w.up_save();
  const blob = new Uint8Array(w.memory.buffer, w.up_save_buf(), n).slice();
  w.up_init(5);                                      // ...power off for 40 minutes...
  w.up_set_wall(1000000 + 2400);
  new Uint8Array(w.memory.buffer, w.up_save_buf(), n).set(blob);
  assert.equal(w.up_load(n), 1);
  assert.equal(w.up_away_ms(), 0, "break already over");
  w.up_input(IN.TOUCH, 184, 200);
  assert.equal(w.up_scene(), 12, "walks back in");
});

test("parent settings from the BOOT gesture (three presses, the last held)", () => {
  hatch();
  const press = (ms, gap) => {
    for (let t = 0; t < ms; t += 10) { w.up_set_boot(1); w.up_tick(10); }
    for (let t = 0; t < gap; t += 10) { w.up_set_boot(0); w.up_tick(10); }
  };
  press(80, 150); press(80, 150);
  for (let t = 0; t < 3100; t += 10) { w.up_set_boot(1); w.up_tick(10); }
  w.up_set_boot(0);
  assert.equal(w.up_scene(), SC.SETTINGS, "the gesture opens the settings");
  const row = (slot) => 56 + slot * 48 + 24;          // SET_Y0, SET_ROW_H
  w.up_input(IN.TOUCH, 184, row(0));                  // volume: normal -> low
  assert.equal(w.up_volume(), 1);
  w.up_input(IN.TOUCH, 184, row(5));                  // the "more" arrow: the second half of the list
  w.up_input(IN.TOUCH, 184, row(2));                  // brightness steps
  assert.notEqual(w.up_brightness(), 180);
});

test("toy box: music, tilt-a-cloud and the mirror", () => {
  hatch();
  tapMenu(1);
  assert.equal(w.up_scene(), SC.TOYS);
  w.up_input(IN.TOUCH, 252, 148);
  assert.equal(w.up_scene(), SC.MUSIC);
  w.up_input(IN.TOUCH, 64, 270);
  assert.ok(w.up_tick(0) & 8, "a flower sings");
  w.up_input(IN.TOUCH, 320, 40);
  tapMenu(1);
  w.up_input(IN.TOUCH, 116, 264);
  assert.equal(w.up_scene(), SC.CLOUD);
  const x0 = w.up_cloud_x();
  for (let i = 0; i < 10; i++) { w.up_set_tilt(100); w.up_tick(100); }
  const right = w.up_cloud_x();
  for (let i = 0; i < 20; i++) { w.up_set_tilt(-100); w.up_tick(100); }
  assert.ok(right > x0 && w.up_cloud_x() < right, "tilting steers the cloud both ways");
  w.up_input(IN.TOUCH, 320, 40);
  tapMenu(1);
  w.up_input(IN.TOUCH, 252, 264);
  assert.equal(w.up_scene(), SC.DRESS);
  w.up_input(IN.TOUCH, 24 + 3 * 86 + 39, 390);
  assert.equal(w.up_accessory(), 3, "wearing the tiara");
});

test("firmware update: check, the battery rule, install, restart keeping the save (the emulator's mock)", () => {
  const UST = { IDLE: 0, CHECKING: 1, AVAILABLE: 3, DOWNLOADING: 4, RESTARTING: 6 }, F_UPDATE = 64;
  const row = (slot) => 56 + slot * 48 + 24, button = (right) => w.up_input(IN.TOUCH, right ? 280 : 90, 412);
  const take = () => { const f = w.up_tick(0); const r = f & F_UPDATE ? w.up_update_req() : 0; w.up_update_take(); return r; };
  hatch();
  w.up_set_fw_version(1);
  w.up_set_stat(ST.HAPPY, 4321);
  w.up_input(5, 0, 0);                                // the grown-up gesture
  w.up_input(IN.TOUCH, 184, row(4));                  // SAAT: the clock, WiFi and update hub
  w.up_input(IN.TOUCH, 184, row(2));                  // GÜNCELLEME
  assert.equal(w.up_update_view(), UST.IDLE);
  button(1);                                          // KONTROL
  assert.equal(take(), 1, "a check request");
  assert.equal(w.up_update_stage(), UST.CHECKING);
  w.up_update_status(2, 0, 2);                        // the mock manifest: version 2 ("yeni sürüm var")
  assert.equal(w.up_update_view(), UST.AVAILABLE);
  w.up_set_battery(20); w.up_set_plugged(0); w.up_set_plugged(0);
  button(1);
  assert.equal(take(), 0, "20 % and no cable: no install");
  w.up_set_plugged(1); w.up_set_plugged(1);
  button(1);                                          // YÜKLE
  assert.equal(take(), 2, "an install request");
  w.up_update_status(UST.DOWNLOADING, 55, 0);
  assert.equal(w.up_update_stage(), UST.DOWNLOADING);
  w.up_update_status(UST.RESTARTING, 100, 0);
  const n = w.up_save();                              // the restart: saved, then the new firmware boots
  const blob = new Uint8Array(w.memory.buffer, w.up_save_buf(), n).slice();
  w.up_init(9);
  w.up_set_fw_version(2);
  new Uint8Array(w.memory.buffer, w.up_save_buf(), n).set(blob);
  assert.equal(w.up_load(n), 1, "the save survives the update");
  assert.equal(w.up_stat(ST.HAPPY), 4321);
  assert.equal(w.up_scene(), SC.HOME);
  assert.equal(w.up_update_view(), UST.IDLE, "nothing waiting on the new version");
});


// local seconds since 2000 (what the board's RTC gives the core)
const at = (y, mo, d, h, mi) => (Date.UTC(y, mo - 1, d, h, mi) - Date.UTC(2000, 0, 1)) / 1000;
const F_LISTEN = 128, ANS = { YES: 1, NO: 2, TAP_YES: 6, NO_LISTENER: 8 }, LK = { YESNO: 0, LEVEL: 2, TV: 3 };
const ASK = 22;

test("daily routine: she asks, nobody can understand her, the child taps the tick, a star", () => {
  hatch();
  w.up_set_packs(1, 0);                               // as if routines_tr.bin were on the card
  w.up_set_wall(at(2026, 11, 3, 17, 59), 1, 0); run(1000);
  w.up_set_wall(at(2026, 11, 3, 18, 0), 1, 0);        // dinner time on a Tuesday
  const f = w.up_tick(100);
  assert.equal(w.up_scene(), ASK, "her question");
  assert.ok(f & F_LISTEN, "the platform is asked to listen");
  assert.equal(w.up_listen_req(), 1); assert.equal(w.up_listen_kind(), LK.YESNO);
  const key = new TextDecoder().decode(new Uint8Array(w.memory.buffer, w.up_q_key(w.up_listen_q()), 6));
  assert.equal(key, "DINNER");
  w.up_take_sfx(); assert.ok(w.up_taken_clip() >= 0, "a clip from routines_tr.bin");
  assert.equal(w.up_link(0), 1, "the radio comes up while she asks");
  w.up_listen_take(); w.up_listen_started(); w.up_listen_result(ANS.NO_LISTENER);
  assert.equal(w.up_r_stage(), 2, "the buttons");
  w.up_input(IN.TOUCH, 90, 400);                      // the big tick
  assert.equal(w.up_stars_today(), 1);
  assert.equal(w.up_r_ans(8) >> 11, ANS.TAP_YES);
  run(5200);
  assert.equal(w.up_scene(), SC.HOME, "back home");
});

test("settings sync: a page config applies, the toy's own change wins its key", () => {
  hatch();
  const K = w.up_settings_count(), buf = () => new Uint16Array(w.memory.buffer, w.up_settings_buf(), K + 8);
  w.up_get_settings();
  assert.equal(buf()[2], 7 * 60, "weekday wake-up 07:00");
  buf()[8] = 4; buf()[5] = 1;                          // the page: German, holiday mode
  w.up_apply_settings(3, 1 << 8 | 1 << 5);
  assert.equal(w.up_lang(), 4); assert.equal(w.up_cfg_rev(), 3);
  w.up_input(5, 0, 0);                                // on the toy: the volume
  w.up_input(IN.TOUCH, 184, 56 + 24);
  assert.ok(w.up_cfg_dirty() & 1 << 9, "marked for the next sync");
  w.up_get_settings(); buf()[9] = 2; buf()[8] = 3;    // the page changed the volume and the language too
  w.up_apply_settings(4, 1 << 9 | 1 << 8);
  assert.equal(w.up_volume(), 1, "the toy's change wins"); assert.equal(w.up_lang(), 3, "the page's other change applies");
  w.up_sync_mark(); w.up_input(IN.TOUCH, 184, 56 + 24);   // a sync goes out; the volume changes again meanwhile
  w.up_settings_synced(); assert.ok(w.up_cfg_dirty() & 1 << 9, "an edit made while it was out stays to be told");
  w.up_sync_mark(); w.up_settings_synced(); assert.equal(w.up_cfg_dirty(), 0);
});

test("TV test mode: a level check a minute, a sample only for lasting sound", () => {
  hatch();
  w.up_set_wall(at(2026, 11, 3, 10, 0), 1, 0);
  w.up_input(5, 0, 0);
  const row = (slot) => 56 + slot * 48 + 24;
  w.up_input(IN.TOUCH, 184, row(5)); w.up_input(IN.TOUCH, 184, row(5));   // second half: GÜNLÜK RUTİN
  w.up_input(IN.TOUCH, 184, row(2));                  // TV TESTİ on
  assert.ok(w.up_tv_day() > 0);
  w.up_input(IN.TOUCH, 184, row(4)); w.up_input(IN.TOUCH, 184, row(6));   // GERİ, ÇIKIŞ
  let levels = 0, samples = 0;
  for (let i = 0; i < 6 * 600; i++) {                 // six minutes, the TV on
    if (!(w.up_tick(100) & F_LISTEN)) continue;
    const kind = w.up_listen_kind(); w.up_listen_take();
    if (kind === LK.LEVEL) { levels++; w.up_level_result(-30, 90); } else if (kind === LK.TV) samples++;
  }
  assert.ok(levels >= 5 && levels <= 7, `a check a minute (${levels})`);
  assert.equal(samples, 1, "one sample: the same sound going on is not sampled again");
});

console.log(`\n${passed} wasm tests passed`);
