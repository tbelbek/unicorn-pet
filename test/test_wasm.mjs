// Drives emu/unicorn.wasm through every event, the way the browser emulator does.
//   node test/test_wasm.mjs
import { readFileSync } from "node:fs";
import assert from "node:assert/strict";

const bytes = readFileSync(new URL("../emu/unicorn.wasm", import.meta.url));
const mod = new WebAssembly.Module(bytes);
assert.equal(WebAssembly.Module.imports(mod).length, 0, "core must not import anything");
const w = new WebAssembly.Instance(mod, {}).exports;

const W = 368, H = 448;
const SC = { EGG: 0, HATCH: 1, HOME: 2, EAT: 3, PLAY: 4, WASH: 5, SLEEP: 6, INFO: 7, REFUSE: 8 };
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
  assert.equal(w.up_scene(), SC.EGG);
  assert.ok(lit() > 5000);
  for (let i = 0; i < 5; i++) w.up_input(IN.TOUCH, 184, 200);
  assert.equal(w.up_scene(), SC.HATCH);
  run(3100);
  assert.equal(w.up_scene(), SC.HOME);
});

const hatch = () => { for (let i = 0; i < 5; i++) w.up_input(IN.SELECT, 0, 0); run(3100); };

test("feed", () => {
  hatch();
  w.up_set_stat(ST.HUNGER, 2000);
  tapMenu(0);
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
  assert.equal(w.up_scene(), SC.PLAY);
  run(4100);
  w.up_set_stat(ST.ENERGY, 1000);
  tapMenu(1);
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
  for (let y = 100; y < 150; y++) for (let x = 278; x < 328; x++) moon += p[y * W + x] !== 0;
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

test("info face on tapping the unicorn", () => {
  hatch();
  w.up_input(IN.TOUCH, 184, 220);
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
  assert.equal(w.up_scene(), SC.PLAY);
  run(4100);
  tapMenu(2);
  w.up_input(4, 0, 0);
  assert.equal(w.up_scene(), SC.HOME);
});

test("mute toggle on the info screen", () => {
  hatch();
  w.up_input(IN.TOUCH, 184, 220);
  w.up_input(IN.TOUCH, 300, 26);
  assert.equal(w.up_muted(), 1);
  w.up_input(IN.TOUCH, 184, 300);
  tapMenu(0);
  assert.equal(w.up_tick(0) & 8, 0, "no sound while muted");
});

console.log(`\n${passed} wasm tests passed`);
