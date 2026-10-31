# Unicorn Pet

A Tamagotchi-style pixel-art unicorn for the **Waveshare ESP32-S3-Touch-AMOLED-1.8**
(368×448 AMOLED, touch, speaker, IMU, battery in the case).

One game core runs in three places: the firmware, a browser emulator (WebAssembly) and host
tests, so what you play in the browser is what runs on the board.

![gameplay](design/gameplay.gif)

![showcase](design/showcase.png)

```
tools/   Python asset generator (pixel art) -> core/assets.gen.h
core/    rules (game.cpp), renderer (render.cpp), melodies (sound.cpp)
         freestanding C++: no libc, no heap, no STL
emu/     browser emulator: the core compiled to wasm + index.html
fw/      PlatformIO firmware: display, touch, buttons, IMU, audio, NVS, light sleep
test/    host tests (ASan/UBSan) + the same events through the wasm build
design/  character, pose and screen sheets
```

## Play it in the browser

```sh
emu/run.sh                 # builds emu/unicorn.wasm, serves http://localhost:8732/emu/
```

- Click the screen = touch. `→` / `Space` = BOOT (next), `Enter` = PWR (select), `S` = shake.
- Side panel: time speed (1×–600×), sliders to make her hungry/tired/dirty, shake, power cut,
  new egg. On a phone, a real shake works too.
- Sound plays the same melodies as the board (Web Audio, square wave).
- Progress is kept in `localStorage`, like NVS on the board.

Needs Homebrew `llvm` and `lld` (`brew install llvm lld`); no emscripten.

## The game

| Event | What happens |
|---|---|
| Egg | every tap wobbles it and a crack spreads; after five taps (or shakes), or one minute, the shell splits, confetti flies and her face grows in |
| Besle (feed) | apple, strawberry and carrot in turn, eaten from a bowl bite by bite with flying crumbs: hunger +35%, happiness +3% |
| Oyna (play), or shake the toy | **Yıldız yakala**: 15 s of popping big rising stars (touch, PWR on the selected star, or a shake), then she jumps after a ball and the stars are counted. Happiness +30% guaranteed, +1% per star (max +15%); energy −10%, a bit hungrier and dirtier; refuses below 15% energy |
| Uyut (sleep) | lies down, the screen turns to night colours; energy refills in ~2 h, wakes by herself, on touch or on a shake |
| Yıka (wash) | bubbles rise, the rainbow messes on the floor dissolve, a sparkle at the end: clean 100%, happiness +3% |
| Tap the unicorn | petting: hearts float up and she beams; happiness +2% at most every 2 s |
| Tap the name/status bar | front-face info screen with all needs, and the sound on/off switch |

When nobody is playing she lives on her own: blinks, wanders left and right (mirrored), hops when
she is happy. A hungry, tired or sad unicorn stays put. Low cleanliness leaves little rainbow messes
on the floor. Needs drain over ~8–17 h of powered time. She never dies: low needs only change her face.

The screen goes dark after 20 s; the first touch only wakes it. Tunables are the enum at the top
of `core/unicorn.h`.

The game design was worked out together with Codex (an independent design scan and a review of
the implementation).

## Tests

```sh
make -C test               # host: every event, mini-game, petting, idle life, particles, sound,
                           # save/load, screen-off, tick-size independence, a week of neglect;
                           # dumps a screenshot per event to test/out/*.png
make -C test wasm          # the same events through emu/unicorn.wasm in Node
make -C test record        # scripted play-through from the real core -> design/gameplay.gif
```

## Firmware

```sh
pio run -d fw              # build (Arduino-ESP32 3.x via pioarduino; needs Python <= 3.14)
pio run -d fw -t upload    # flash over USB-C
```

- **Inputs:** touch first; BOOT moves the menu cursor; PWR (short press) selects; shaking plays.
- **Power:** a long PWR press is a hardware power cut, so the game saves after every action and
  every 5 minutes. While powered off time stands still (she just sleeps).
- **Screen off:** the panel is switched off and the ESP32 light-sleeps, woken by touch or BOOT
  at once and every 200 ms to poll PWR (the key sits on the IO expander, which has no wake line).
- **Sound:** ES8311 codec over I2S; the speaker amp is only enabled while a tune plays.

Pins, addresses and the IO-expander reset sequence come from Waveshare's own Arduino examples.
Still to check on a real board (marked `VERIFY` in `fw/src/main.cpp`): touch axis orientation.
If the screen stays black, the panel may be the newer CO5300: build with `-DPANEL_CO5300`.

## Changing the art

```sh
uv run --with pillow python tools/export_c.py      # regenerate core/assets.gen.h
uv run --with pillow python tools/showcase.py      # regenerate design sheets
```

## Third-party

- Press Start 2P font, SIL Open Font License 1.1 (`assets/fonts/OFL.txt`); glyphs are baked into
  `core/assets.gen.h`.
- ES8311 codec driver by Espressif Systems, Apache-2.0 (`fw/lib/es8311`).
- Libraries fetched by PlatformIO: GFX Library for Arduino, XPowersLib, SensorLib.
