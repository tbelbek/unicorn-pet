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

test("mute toggle on the info screen", () => {
  hatch();
  w.up_input(IN.TOUCH, 184, 60);
  w.up_input(IN.TOUCH, 300, 26);
  assert.equal(w.up_muted(), 1);
  w.up_input(IN.TOUCH, 184, 300);
  tapMenu(0);
  w.up_input(IN.TOUCH, 61, 200);
  assert.equal(w.up_tick(0) & 8, 0, "no sound while muted");
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

test("parent settings from the BOOT long-press", () => {
  hatch();
  w.up_input(IN.PARENT ?? 5, 0, 0);
  assert.equal(w.up_scene(), SC.SETTINGS);
  w.up_input(IN.TOUCH, 184, 64 + 0 * 38 + 18);        // volume: normal -> low
  assert.equal(w.up_volume(), 1);
  w.up_input(IN.TOUCH, 184, 64 + 3 * 38 + 18);        // brightness steps
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
  for (let i = 0; i < 10; i++) { w.up_set_tilt(100); w.up_tick(100); }
  w.up_input(IN.TOUCH, 320, 40);
  tapMenu(1);
  w.up_input(IN.TOUCH, 252, 264);
  assert.equal(w.up_scene(), SC.DRESS);
  w.up_input(IN.TOUCH, 24 + 3 * 86 + 39, 390);
  assert.equal(w.up_accessory(), 3, "wearing the tiara");
});

console.log(`\n${passed} wasm tests passed`);
