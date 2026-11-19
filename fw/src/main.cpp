// Unicorn Pet firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.8.
// Hardware glue only: every game rule, pixel and melody comes from ../../core (see core.cpp).
//
// Pins, addresses and the expander sequence follow Waveshare's Arduino examples for this
// board (examples/arduino in waveshareteam/ESP32-S3-Touch-AMOLED-1.8). Lines marked VERIFY
// still need a look on real hardware.
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <soc/usb_serial_jtag_struct.h>
#include <driver/gpio.h>
#include <Arduino_GFX_Library.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <SensorQMI8658.hpp>

#include "../../core/unicorn.h"
#include "audio.h"
#include "rtc.h"
#include "clock.h"
#include "wifi_setup.h"
#include "ota.h"
#include "store.h"
#include "net.h"
#include "version.h"
#include "panel.h"

static const int LCD_CS = 12, LCD_SCLK = 11, LCD_D0 = 4, LCD_D1 = 5, LCD_D2 = 6, LCD_D3 = 7;
static const int I2C_SDA = 15, I2C_SCL = 14, TP_INT = 21, KEY_BOOT = 0;
static const uint8_t TCA9554_ADDR = 0x20, FT3168_ADDR = 0x38, CST820_ADDR = 0x15;
static const uint8_t EXIO_PWR = 4;                        // PWR key reads HIGH while held
static const bool TOUCH_SWAP_XY = false, TOUCH_FLIP_X = false, TOUCH_FLIP_Y = false;   // checked on a CO5300/CST820 board: corners read (1,1)..(360,447)
// The QMI8658's X is the screen's up-down here (upright it read x = 1 g), so a sideways tilt is Y; by the axes' handedness
// a right side going down reads Y < 0 (VERIFY on the board: flip if the cloud goes the wrong way)
static const bool TILT_FLIP = true;
static const uint8_t BRIGHTNESS = 180;                    // indoor toy: no need for full AMOLED
static const uint32_t SLEEP_POLL_US = 200000;             // light-sleep wake to poll PWR (no GPIO wake)
static const float SHAKE_G = 1.2f;                        // |a| - 1 g beyond this counts as a jolt
// Every chip on the shared I2C bus (AXP2101, TCA9554, FT3168, QMI8658, PCF85063, ES8311) does
// 400 kHz fast mode: touch, PWR and the accelerometer are read 4x faster than at the 100 kHz default.
static const uint32_t I2C_HZ = 400000;
static const uint32_t LCD_HZ = 40000000;                  // QSPI clock: 80 MHz measured only 44 -> 36 ms a full frame (the byte swap is the cost), not worth the risk

// shared: the library takes SPI2 only around its own writes, so panel.cpp can stream the pixels
static Arduino_DataBus* bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3, true);
// Two batches of this board: SH8601 panel + FT3168 touch (0x38), and newer CO5300 + CST820 (0x15),
// whose panel needs a 16-pixel column offset (Waveshare's own V2 example and ESP-IDF BSP). The touch
// chip that answers at boot tells them apart; -DPANEL_CO5300 / -DPANEL_SH8601 force one.
static Arduino_OLED* lcd;
static uint8_t touch_addr = FT3168_ADDR;
static XPowersPMU pmu;
static bool pmu_ok;
static SensorQMI8658 imu;
static bool imu_ok;
static Preferences prefs;
static up::Game game;
static bool panel_on = true;

// ---- TCA9554 IO expander -------------------------------------------------------------------

static void tca_write(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(reg);
    Wire.write(v);
    Wire.endTransmission();
}

static int tca_read(uint8_t reg) {
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(TCA9554_ADDR, (uint8_t)1) != 1) return -1;
    return Wire.read();
}

// As in Waveshare's example: EXIO1/EXIO2 are the panel and touch resets, pulsed low then high;
// EXIO4 (PWR key) and EXIO5 (PMU IRQ) stay inputs.
// (EXIO1 is the panel's DSI_PWR_EN. Driving it low while dark was tried: the panel then stays black after
// SLPOUT + DISPON and would need a full re-init, so it stays high; SLPIN already does most of that saving.)
static const uint8_t EXIO_LCD_RESET = 1 << 0, EXIO_TP_RESET = 1 << 2;
static uint8_t tca_out;                   // what the output register holds

static void tca_set(uint8_t v) { tca_out = v; tca_write(0x01, v); }

static void expander_init() {
    tca_write(0x03, 0xF8);        // config: 1 = input; EXIO0 (panel reset), EXIO1, EXIO2 outputs
    tca_set(0x00);
    delay(20);
    tca_set(0x07);
    delay(120);
}

// ---- panel ---------------------------------------------------------------------------------

static bool panel_stale = true;           // the panel's content is unknown: send the whole frame

// CST820 register 0xFE: 1 = never auto-sleep. Lit, it must answer every poll; dark, it may doze (a slow
// scan, ~mA less) and still pulls TP_INT low on a touch, which wakes the nap. Asleep it ignores I2C.
static bool touch_autosleep(bool allow) {
    if (touch_addr != CST820_ADDR) return true;
    Wire.beginTransmission(CST820_ADDR);
    Wire.write(0xFE);
    Wire.write(allow ? 0x00 : 0x01);
    return Wire.endTransmission() == 0;
}

// The touch chip dozes only while dark on the battery (the nap wakes on TP_INT). Dark on USB power nothing
// naps, and the chip is polled as before.
static bool touch_dozing, touch_booting;
static uint32_t touch_reset_at;

static void touch_doze(bool doze) {
    if (doze == touch_dozing || touch_addr != CST820_ADDR) return;
    if (doze) { touch_dozing = touch_autosleep(true); return; }
    touch_dozing = false;
    if (touch_autosleep(false)) return;   // awake: it was just touched
    // woken by PWR, BOOT, a shake, her routine or the cable, it still dozes and misses the write: reset it
    tca_set(tca_out & ~EXIO_TP_RESET); delay(10); tca_set(tca_out | EXIO_TP_RESET);
    touch_booting = true;
    touch_reset_at = millis();
}

static void touch_booted() {              // after that reset (each loop): "stay awake" once it answers again
    if (!touch_booting) return;
    if (touch_autosleep(false) || millis() - touch_reset_at > 500) touch_booting = false;
}

// CO5300 (datasheet V0.01): dark, it goes to deep standby (DSTBON 4Fh: 3 µA, against ~110-135 µA in plain
// sleep-in); out of it only by a reset pulse (RESX low > 3 ms, then 5 ms to reload its OTP) and its init again.
// SLPOUT needs 5 ms before the next command, and 120 ms must pass from SLPIN to SLPOUT. Arduino_CO5300's
// displayOn()/displayOff() waited 240 ms each way; this wakes in ~20 ms. The SH8601 batch keeps the library's.
static bool panel_co5300;
static uint32_t panel_slept_at;
static bool panel_dispon_due;              // after a reset its memory is noise: DISPON once the first frame is in

static void co5300_w(uint8_t c, uint8_t d) { bus->beginWrite(); bus->writeC8D8(c, d); bus->endWrite(); }

static void co5300_wake() {
    uint32_t since = millis() - panel_slept_at;
    if (since < 120) delay(120 - since);
    tca_set(tca_out & ~EXIO_LCD_RESET); delay(4); tca_set(tca_out | EXIO_LCD_RESET); delay(6);
    bus->sendCommand(0x11);               // SLPOUT
    delay(5);
    co5300_w(0xFE, 0x00);                 // the library's init after that (Arduino_CO5300.h co5300_init_operations)
    co5300_w(0xC4, 0x80);                 // SPI mode
    co5300_w(0x3A, 0x55);                 // 16 bits a pixel
    co5300_w(0x53, 0x20);
    co5300_w(0x63, 0xFF);                 // HBM brightness
    co5300_w(0x58, 0x00);                 // high-contrast mode off
    bus->sendCommand(0x20);               // inversion off (invertDisplay(false))
    lcd->setRotation(lcd->getRotation()); // MADCTL, as begin() set it
    lcd->startWrite(); lcd->writeAddrWindow(0, 0, 2, 2); lcd->endWrite();   // the window cache: CASET/PASET reset
    panel_dispon_due = true;              // push_frame() sends DISPON after the whole first frame
}

static void panel_power(bool on) {
    if (on == panel_on) return;
    if (on) {
        touch_doze(false);                // (a reset of it is waited for in the loop, not here)
        if (panel_co5300) co5300_wake(); else lcd->displayOn();
        lcd->setBrightness(game.screen_brightness()); panel_stale = true;
    } else {
        if (panel_co5300) {
            bus->sendCommand(0x28);       // DISPOFF
            bus->sendCommand(0x10);       // SLPIN: the booster winds down over ~120 ms
            delay(120);
            co5300_w(0x4F, 0x01);         // DSTBON
            panel_slept_at = millis();
        } else lcd->displayOff();         // AMOLED: every pixel off, nothing to leak
        if (pmu_ok) pmu.clearIrqStatus();   // a press made while lit is not a new one in the dark
    }
    panel_on = on;
}

// Only the rows that changed go over the QSPI bus (a full frame is 330 KB, ~17 ms at 40 MHz).
// The core redraws on every step while anything moves, but most of the screen stays the same
// and many frames are identical (breathing moves a pixel every ~200 ms): those send nothing.
// A checksum per row decides; a full frame every 64 sends heals any (2^-32) collision.
static uint32_t row_sum[up::SCREEN_H];

static uint32_t diag_render_max, diag_push_max;   // bench: µs, the slowest render and panel send
static uint32_t touch_at, diag_lat_max, diag_lat_n, diag_lat_sum;   // touch (or bench tap) -> its frame on the panel, ms

// The whole frame in PSRAM: only for the bench's "shot" and when the fast stream is missing (push_frame draws
// band by band in internal RAM). Sending from a task on the other core was tried before the bands and measured
// slower: both cores on PSRAM at once made a frame 3-4x longer.
static uint16_t* fb;

static void send_rows(const uint16_t* buf, int y0, int y1) {
    uint32_t t1 = micros();
    if (panel_fast_ok()) {
        lcd->startWrite();
        lcd->writeAddrWindow(0, y0, up::SCREEN_W, y1 - y0 + 1);   // the window, then RAMWR
        lcd->endWrite();
        panel_fast_send(buf + y0 * up::SCREEN_W, (uint32_t)up::SCREEN_W * (y1 - y0 + 1));
    } else {
        lcd->draw16bitRGBBitmap(0, y0, (uint16_t*)buf + y0 * up::SCREEN_W, up::SCREEN_W, y1 - y0 + 1);
    }
    if (micros() - t1 > diag_push_max) diag_push_max = micros() - t1;
}

static uint32_t diag_frames;                  // bench: push_frame calls (frames rendered), for the fps under a naptest

static uint32_t row_check(const uint16_t* row) {   // FNV-1a over a row, two pixels at a time
    const uint32_t* w = (const uint32_t*)row;
    uint32_t s = 2166136261u;
    for (int i = 0; i < up::SCREEN_W / 2; i++) s = (s ^ *w++) * 16777619u;
    return s;
}

static void note_latency() {
    if (touch_at) { uint32_t l = millis() - touch_at; touch_at = 0; diag_lat_n++; diag_lat_sum += l; if (l > diag_lat_max) diag_lat_max = l; }
}

// Without the fast stream (no DMA buffers): the whole frame in PSRAM, as before the bands.
static void push_frame_psram(bool full) {
    uint32_t t0 = micros();
    up::render(game, fb);
    if (micros() - t0 > diag_render_max) diag_render_max = micros() - t0;
    int y0 = up::SCREEN_H, y1 = -1;
    for (int y = 0; y < up::SCREEN_H; y++) {
        uint32_t s = row_check(fb + y * up::SCREEN_W);
        if (s == row_sum[y] && !full) continue;
        row_sum[y] = s;
        if (y < y0) y0 = y;
        y1 = y;
    }
    if (y1 >= 0) {
        y0 &= ~1;                          // SH8601/CO5300 windows start on an even row...
        y1 |= 1;                           // ...and span an even number of rows
        send_rows(fb, y0, y1);
        note_latency();
    }
    if (panel_dispon_due) { bus->sendCommand(0x29); panel_dispon_due = false; }
}

// The frame band by band in internal RAM (render_band), each band's changed rows swapped to the panel's byte
// order and sent while the next band is drawn: no 330 KB PSRAM framebuffer written, read back and read again
// (measured 15.4 ms render + 5.5 ms checksum there, vs ~5.5 ms for all bands here).
static const int BAND_ROWS = PANEL_BAND_PX / up::SCREEN_W & ~1;   // 22: even, so windows stay on even rows

static void push_frame() {
    static uint8_t sends;
    diag_frames++;
    ota_frame_pushed(millis());            // a new firmware on trial is kept once it has drawn for a while
    bool full = panel_stale || ++sends % 64 == 0;
    panel_stale = false;
    if (!panel_fast_ok()) { push_frame_psram(full); return; }
    uint32_t t0 = micros(), drawing = 0;
    int on_wire = -1;                      // which band buffer the DMA is reading
    bool sent = false;
    for (int k = 0, y = 0; y < up::SCREEN_H; k++, y += BAND_ROWS) {
        int y1 = y + BAND_ROWS < up::SCREEN_H ? y + BAND_ROWS : up::SCREEN_H;
        if (on_wire == (k & 1)) { panel_band_wait(); on_wire = -1; }   // (the band before it had nothing to send)
        uint16_t* band = panel_band(k);
        uint32_t td = micros();
        up::render_band(game, band, y, y1);
        drawing += micros() - td;
        int r0 = -1, r1 = -1;
        for (int r = y; r < y1; r++) {
            uint32_t s = row_check(band + (r - y) * up::SCREEN_W);
            if (s == row_sum[r] && !full) continue;
            row_sum[r] = s;
            if (r0 < 0) r0 = r;
            r1 = r;
        }
        if (r0 < 0) continue;
        r0 &= ~1;                          // even rows, an even count (y and y1 are even)
        r1 |= 1;
        uint32_t* p = (uint32_t*)(band + (r0 - y) * up::SCREEN_W);
        for (int i = 0, n = (r1 - r0 + 1) * up::SCREEN_W / 2; i < n; i++) {
            uint32_t v = p[i];
            p[i] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);   // the panel is big-endian
        }
        panel_band_wait();                 // the band before is out: the bus is free for the window
        lcd->startWrite();
        lcd->writeAddrWindow(0, r0, up::SCREEN_W, r1 - r0 + 1);   // the window, then RAMWR
        lcd->endWrite();
        panel_band_send((uint16_t*)p, (uint32_t)(r1 - r0 + 1) * up::SCREEN_W);
        on_wire = k & 1;
        sent = true;
    }
    panel_band_wait();
    if (panel_dispon_due) { bus->sendCommand(0x29); panel_dispon_due = false; }   // DISPON: the frame is in
    if (drawing > diag_render_max) diag_render_max = drawing;
    uint32_t rest = micros() - t0 - drawing;
    if (rest > diag_push_max) diag_push_max = rest;
    if (sent) note_latency();
}

// ---- inputs --------------------------------------------------------------------------------

static bool edge(bool down, bool* last, uint32_t* t, uint32_t debounce) {
    if (down != *last && millis() - *t > debounce) { *t = millis(); *last = down; return down; }
    return false;
}

// PWR is also the AXP2101's power key. While the screen is dark we only wake every SLEEP_POLL_US,
// so a child's quick press (~100 ms) would often fall between two looks at EXIO4: the PMU latches
// a short press as an interrupt flag, which is read (and cleared) instead.
// One register read per dark wake (INTSTS2, bit 3 = PKEY short press), cleared only when set: the
// library's getIrqStatus() + clearIrqStatus() are six I2C transactions, every 200 ms all night.
static bool pmu_short_press() {
    if (!pmu_ok) return false;
    Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);
    Wire.write(0x49);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)AXP2101_SLAVE_ADDRESS, (uint8_t)1) != 1) return false;
    if (!(Wire.read() & 0x08)) return false;
    Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);
    Wire.write(0x49);
    Wire.write(0x08);                         // write-1-to-clear
    Wire.endTransmission();
    return true;
}

static bool pwr_pressed() {
    static bool last; static uint32_t t, polled;
    if (!panel_on) return pmu_short_press();
    if (millis() - polled < 20) return false;   // a button, not a sensor: 50 Hz is plenty
    polled = millis();
    int v = tca_read(0x00);
    return edge(v >= 0 && ((v >> EXIO_PWR) & 1), &last, &t, 60);
}

static bool touch_read(int* x, int* y) {
    static bool was_down = false;
    uint8_t r[5];
    Wire.beginTransmission(touch_addr);
    Wire.write(0x02);             // finger count, then X1 hi/lo, Y1 hi/lo
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(touch_addr, (uint8_t)5) != 5) return false;
    for (int i = 0; i < 5; i++) r[i] = Wire.read();
    bool down = (r[0] & 0x0F) > 0;
    bool press = down && !was_down;          // only the moment the finger lands counts
    was_down = down;
    if (!press) return false;
    int tx = ((r[1] & 0x0F) << 8) | r[2], ty = ((r[3] & 0x0F) << 8) | r[4];
    if (TOUCH_SWAP_XY) { int t = tx; tx = ty; ty = t; }
    if (TOUCH_FLIP_X) tx = up::SCREEN_W - 1 - tx;
    if (TOUCH_FLIP_Y) ty = up::SCREEN_H - 1 - ty;
    *x = tx; *y = ty;
    return true;
}

// Shake = two jolts within 500 ms while awake; any single jolt while the screen is off
// (we only sample every SLEEP_POLL_US then). 1.5 s cool-down so one shake is one event.
static float acc_x, acc_y, acc_z;
static bool acc_fresh;

static void read_accel() {                // shake and tilt both use it; at most once per sample
    static uint32_t polled;
    acc_fresh = false;
    if (!imu_ok || (panel_on && millis() - polled < 8)) return;   // the IMU samples at ~128 Hz
    polled = millis();
    acc_fresh = imu.getDataReady() && imu.getAccelerometer(acc_x, acc_y, acc_z);
}

static bool shaken() {
    static uint32_t first_jolt, last_shake;
    static bool shook;
    if (!acc_fresh || (shook && millis() - last_shake < 1500)) return false;   // wrap-safe cool-down
    float x = acc_x, y = acc_y, z = acc_z;
    float g = sqrtf(x * x + y * y + z * z);
    if (fabsf(g - 1.0f) < SHAKE_G) return false;
    uint32_t now = millis();
    if (!panel_on || (first_jolt && now - first_jolt < 500)) {
        first_jolt = 0;
        last_shake = now;
        shook = true;
        return true;
    }
    first_jolt = now;
    return false;
}

// ---- battery log ---------------------------------------------------------------------------
// The AXP2101 has no current meter, so what the dark costs is measured the slow way: every 15 min on the
// battery the level is written to NVS (it survives the 3.3 V cut-off); unplugging starts a new log.
// Bench "batlog" prints it. 96 records = 24 h; 96 small NVS writes a day are nothing for the flash.
struct BatRec { uint16_t min; uint16_t mv; uint8_t pct; uint8_t dark_pct; };
static const int BATLOG_N = 96;
static const uint32_t BATLOG_MS = 15u * 60000u;
static BatRec batlog[BATLOG_N];
static int batlog_n;
static uint32_t batlog_t0, batlog_last, batlog_dark_ms, batlog_dark_from;
static bool vbus_now = true;              // the cable, read once a second (true until then: no log at boot)

static void batlog_poll(bool plugged, uint32_t now) {
    // A log starts only at an unplug seen since boot: a reset on the battery (a crash, an update's restart)
    // keeps last night's records instead of starting over on top of them.
    static bool was_plugged = false, logging = false;
    if (!pmu_ok || now < 5000) return;     // (the plugged state settles over ~2 s after boot: no false start)
    if (plugged) { was_plugged = true; logging = false; return; }
    if (was_plugged) {                     // unplugged: a new log, its first record now
        logging = true;
        was_plugged = false;
        batlog_n = 0; batlog_t0 = batlog_last = now; batlog_dark_ms = 0; batlog_dark_from = now;
    } else if (!logging || now - batlog_last < BATLOG_MS || batlog_n >= BATLOG_N) return;
    else batlog_last = now;
    uint32_t span = now - batlog_dark_from;
    batlog[batlog_n++] = {(uint16_t)((now - batlog_t0) / 60000), (uint16_t)pmu.getBattVoltage(),
                          (uint8_t)pmu.getBatteryPercent(), (uint8_t)(span ? (uint64_t)batlog_dark_ms * 100 / span : 0)};
    batlog_dark_ms = 0; batlog_dark_from = now;
    prefs.putBytes("batlog", batlog, batlog_n * sizeof(BatRec));
}

// ---- save ----------------------------------------------------------------------------------

static bool keep_old_save;                    // an unreadable save could not be backed up: leave it alone

static void save_game() {
    if (keep_old_save) return;
    uint8_t buf[up::SAVE_SIZE];
    game.save(buf);
    prefs.putBytes("save", buf, sizeof buf);
}

// A save an older firmware could not read waits in "save_bad" (an update rolled back, then the older one
// started a new egg and saved that). This firmware reads it back when it holds the longer life.
static bool restore_bad_save(bool have_save) {
    size_t m = prefs.isKey("save_bad") ? prefs.getBytesLength("save_bad") : 0;
    if (!m) return false;
    uint8_t* bad = (uint8_t*)malloc(m);
    up::Game* t = bad ? new up::Game() : nullptr;
    bool ok = t && prefs.getBytes("save_bad", bad, m) == m && (t->init(1), t->load(bad, m));
    bool better = ok && (!have_save || t->s.age_s > game.s.age_s);
    if (better && game.load(bad, m)) {
        Serial.println("save restored from save_bad (written by a newer firmware)");
        save_game();
        prefs.remove("save_bad");
    } else if (ok) prefs.remove("save_bad");   // readable but an older life: nothing to bring back
    delete t;
    free(bad);
    return better;
}

static void load_game() {
    size_t len = prefs.getBytesLength("save");
    if (!len) { if (!restore_bad_save(false)) Serial.println("no save: new egg"); return; }
    uint8_t* buf = (uint8_t*)malloc(len);     // any size: a newer firmware's save may be longer
    size_t n = buf ? prefs.getBytes("save", buf, len) : 0;
    if (n && game.load(buf, n)) { Serial.println("save loaded"); free(buf); restore_bad_save(true); return; }
    Serial.println("unreadable save: new egg");
    // Keep it (a newer firmware's, or a bad write) instead of overwriting it with the new egg: a
    // later firmware can still read it back from "save_bad". If it cannot be kept, never overwrite.
    bool kept = false;
    if (n && prefs.isKey("save_bad")) {       // already a backup: is it this very save?
        size_t m = prefs.getBytesLength("save_bad");
        uint8_t* old = m == n ? (uint8_t*)malloc(m) : nullptr;
        kept = old && prefs.getBytes("save_bad", old, m) == m && !memcmp(old, buf, n);
        free(old);
    } else if (n) {
        kept = prefs.putBytes("save_bad", buf, n) == n;
    }
    if (!kept) keep_old_save = true;          // a second unreadable save, or no room: never overwrite it
    free(buf);
}

// ---- power ---------------------------------------------------------------------------------

// Screen off and nothing playing: light-sleep until a touch, BOOT, or the next poll for PWR.
// millis() keeps counting across light sleep, so the game clock stays right.
static uint32_t touch_listen_until;           // after a pin wake in the dark: stay up and read the touch chip a moment

// bench: what the naps really did (counted always, read with "naps")
static uint32_t nap_n, nap_err, nap_timer, nap_gpio, nap_other, nap_tp_low, nap_boot_low;
static uint64_t nap_slept_us;
static uint64_t nap_awake_us;                 // from one nap's wake to the next nap: the CPU's time awake in the dark
static int64_t nap_woke_at;
static uint64_t sect_us[8];                   // ... and where it goes, loop section by section (dark only)
static int64_t sect_t;
#define SECT(i) do { int64_t t_ = esp_timer_get_time(); if (!panel_on && nap_woke_at) sect_us[i] += t_ - sect_t; sect_t = t_; } while (0)   // (only loops right after a nap)
static uint32_t nap_since;                    // millis() when the counters were last cleared

static uint32_t lit_n;                       // lit naps: the gaps between frames (on the battery)
static uint64_t lit_slept_us;

static void nap(uint32_t us = SLEEP_POLL_US, bool lit = false) {
    gpio_wakeup_enable((gpio_num_t)KEY_BOOT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)TP_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(us);
    if (lit) {                            // counted apart: the dark figures stay the dark's
        int64_t t0 = esp_timer_get_time();
        esp_light_sleep_start();
        lit_slept_us += esp_timer_get_time() - t0;
        lit_n++;
        return;
    }
    if (digitalRead(TP_INT) == LOW) nap_tp_low++;      // a low level here wakes the nap at once
    if (digitalRead(KEY_BOOT) == LOW) nap_boot_low++;
    int64_t t0 = esp_timer_get_time();
    if (nap_woke_at) nap_awake_us += t0 - nap_woke_at;
    esp_err_t e = esp_light_sleep_start();
    nap_woke_at = esp_timer_get_time();
    nap_slept_us += nap_woke_at - t0;
    nap_n++;
    if (e != ESP_OK) nap_err++;
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: nap_timer++; break;
    case ESP_SLEEP_WAKEUP_GPIO: nap_gpio++; touch_listen_until = millis() + 300; break;   // a finger (or BOOT)
    default: nap_other++; break;
    }
}

// After light sleep the host has given up on the USB-Serial/JTAG port and does not ask again: drop D+'s
// pull-up for a moment so it sees a fresh plug-in (bench "naptest"; a real plug-in is a real one).
static void usb_reconnect() {
    USB_SERIAL_JTAG.conf0.pad_pull_override = 1;
    USB_SERIAL_JTAG.conf0.dp_pullup = 0;
    delay(100);
    USB_SERIAL_JTAG.conf0.dp_pullup = 1;
    USB_SERIAL_JTAG.conf0.pad_pull_override = 0;
}

// ---- main ----------------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    // Never wait on the USB port. Plugged into a computer that is not reading it, every print waited up
    // to 20 x 100 ms for room (HWCDC::write): a log line per touch froze the toy for seconds. Logs are
    // dropped instead; the bench's dumps wait on purpose (dump_begin/dump_end).
    Serial.setTxTimeoutMs(0);
    Serial.printf("reset reason %d\n", (int)esp_reset_reason());   // 1 power-on, 3 software, 4 panic, 5-7 watchdogs, 9 brownout
    pinMode(KEY_BOOT, INPUT_PULLUP);
    pinMode(TP_INT, INPUT_PULLUP);
    Wire.begin(I2C_SDA, I2C_SCL);

    pmu_ok = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    if (!pmu_ok) Serial.println("AXP2101 not found");
    else {
        pmu.setChargeTargetVoltage(3);        // 4.2 V, as Waveshare's example sets it
        pmu.enableBattDetection();
        pmu.enableBattVoltageMeasure();       // needed for getBatteryPercent()
        // stop before the cell is flat: below ~3.3 V the 3.3 V rail sags and the ESP32/PSRAM misbehave
        // (resets that light the screen again and again), and a Li-ion cell should not go to 2.6 V
        pmu.setSysPowerDownVoltage(3300);
        // Rails nothing on this board uses (schematic: DC2-4 have no input, ALDO2-4 end at test pads,
        // BLDO1/2, CPUSLDO and DLDO1/2 at nothing), all on by default. DC1 is VCC3V3, ALDO1 the codec's AVDD + mic.
        pmu.disableDC2(); pmu.disableDC3(); pmu.disableDC4();
        pmu.disableALDO2(); pmu.disableALDO3(); pmu.disableALDO4();
        pmu.disableBLDO1(); pmu.disableBLDO2(); pmu.disableCPUSLDO(); pmu.disableDLDO1(); pmu.disableDLDO2();
        // ADC channels nobody reads (isVbusIn() is a status bit; the battery's voltage stays on for the %)
        pmu.disableVbusVoltageMeasure(); pmu.disableSystemVoltageMeasure(); pmu.disableTemperatureMeasure();
        pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
        pmu.clearIrqStatus();
    }
    expander_init();

    // which batch: the CST820 answers at 0x15 right after its reset pulse (then naps, so ask now)
    Wire.beginTransmission(CST820_ADDR);
    bool v2 = Wire.endTransmission() == 0;
#if defined(PANEL_CO5300)
    v2 = true;
#elif defined(PANEL_SH8601)
    v2 = false;
#endif
    if (v2) {
        touch_addr = CST820_ADDR;
        Wire.beginTransmission(CST820_ADDR);  // no auto-sleep: we poll it, it would stop answering
        Wire.write(0xFE);
        Wire.write(0x01);
        Wire.endTransmission();
        lcd = new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, up::SCREEN_W, up::SCREEN_H, 16, 0, 0, 0);
        panel_co5300 = true;
    } else {
        lcd = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, up::SCREEN_W, up::SCREEN_H);
    }
    Serial.printf("board: %s panel, touch at 0x%02X\n", v2 ? "CO5300" : "SH8601", touch_addr);
    if (!lcd->begin(LCD_HZ)) Serial.println("display init failed");
    lcd->setBrightness(BRIGHTNESS);
    if (!panel_fast_begin(LCD_CS, LCD_HZ)) Serial.println("panel: library path (no fast stream)");

    imu_ok = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    if (imu_ok) {
        // accelerometer only, in its low-power 128 Hz mode (allowed with the gyro off): a shake
        // and a slow tilt need no more, and the loop never reads faster than that anyway
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_LOWPOWER_128Hz,
                                SensorQMI8658::LPF_MODE_0);
        imu.enableAccelerometer();
    } else {
        Serial.println("QMI8658 not found: shake disabled");
    }
    if (!audio_begin()) Serial.println("ES8311 init failed: sound disabled");
    else if (pmu_ok) audio_set_rail([](bool on) { if (on) pmu.enableALDO1(); else pmu.disableALDO1(); });
    if (!rtc_begin()) Serial.println("PCF85063 not found: breaks count powered time only");

    Wire.setClock(I2C_HZ);                    // after every driver's begin(), which may reset it
    fb = (uint16_t*)ps_malloc(up::SCREEN_W * up::SCREEN_H * sizeof(uint16_t));
    if (!fb) { Serial.println("no PSRAM for framebuffer"); for (;;) delay(1000); }

    store_begin();                            // the card or LittleFS: private packs, the listener's address
    prefs.begin("unicorn", false);
    game.init(esp_random());
    clock_begin(prefs);
    wifi_setup_begin(game);                   // saved networks, shown in the settings
    ota_begin(game);                          // the running version (settings > SAAT > GÜNCELLEME)
    net_begin();                              // the home listener: answers, journal, TV samples, settings sync
    { uint32_t utc = rtc_now();             // one read: local and UTC agree
      game.set_wall(clock_local(utc), clock_trusted(), utc); }   // before the save, so time spent off counts
    load_game();
    push_frame();
    store_load_packs();                       // after the first frame: ~1.7 MB into PSRAM, in a task
}

// ---- bench console (USB serial) ------------------------------------------------------------
// Lines in, one reply each: how the board is tested on the desk without looking at it.
//   info | i2c | tap X Y | sel | shake | shot (the framebuffer, base64 RGB565) | mic KIND (0 yes/no, 1 open,
//   2 level, 3 five raw seconds: the recording back as base64 WAV) | play SFX [LINE [CLIP]] | utc SECONDS_SINCE_2000 |
//   boot MS (BOOT held that long; "boot 80" is a tap) | awake 0/1 (no screen timeout while testing) |
//   ask Q (her routine question Q now, as if due) | gain 0-7 | slot 0/1 | wipe (a new egg, restarts) |
//   naptest S (S seconds as if on the battery: USB drops until it ends) | naps (what the naps did: slept %, wake
//   causes, what kept it awake, µs per loop section) | rails (AXP2101 outputs, expander) | codec (ES8311 registers
//   that differ from boot) | batlog (the battery log: minutes, mV, %, % of it dark)
// Physical access only: nothing secret is printed (no token, no WiFi password).
static int diag_mic = -1;                     // a bench recording is under way: its result comes here
static uint32_t diag_boot_until;              // "boot MS": BOOT held down from the bench
static bool diag_awake;
static uint32_t diag_nap_until;               // "naptest S": as if on the battery for S seconds (light sleep drops USB)
static uint32_t diag_skip[6];                 // loops kept awake while dark, by reason: panel audio clock wifi ota net
static uint32_t diag_loop_max, diag_loop_slow;   // the longest loop since the last "info", and loops over 50 ms

// A dump is asked for by someone reading the port: wait for room then, or its bytes are lost.
static void dump_begin() { Serial.setTxTimeoutMs(200); }
static void dump_end() { Serial.flush(); Serial.setTxTimeoutMs(0); }

static void b64_dump(const uint8_t* p, uint32_t n) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[77];
    int k = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        line[k++] = T[v >> 18 & 63]; line[k++] = T[v >> 12 & 63];
        line[k++] = i + 1 < n ? T[v >> 6 & 63] : '=';
        line[k++] = i + 2 < n ? T[v & 63] : '=';
        if (k == 76) { line[k] = 0; Serial.println(line); k = 0; }
    }
    if (k) { line[k] = 0; Serial.println(line); }
}

static void diag_poll() {
    static char cmd[64];
    static int n;
    while (Serial.available()) {
        int c = Serial.read();
        if (c != '\n' && c != '\r') { if (n < (int)sizeof cmd - 1) cmd[n++] = c; continue; }
        if (!n) continue;
        cmd[n] = 0; n = 0;
        int a = 0, b = 0, d = -1;
        unsigned long u = 0;
        if (!strcmp(cmd, "info")) {
            float x = 0, y = 0, z = 0;
            if (imu_ok) imu.getAccelerometer(x, y, z);
            Serial.printf("INFO fw=%d up=%lus heap=%u psram=%u scene=%d stage=%d screen=%d bright=%d vol=%d lang=%d "
                          "wall=%lu utc=%lu plugged=%d vbus=%d chg=%d batt=%d%% vbat=%dmV "
                          "acc=%.2f,%.2f,%.2f store=%s routines=%d names=%d listening=%d rst=%d loopmax=%lums slow=%lu render=%luus push=%luus lat=%lu/%lums n=%lu ifree=%u ilarge=%u\n",
                          FW_VERSION, (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getFreePsram(), game.scene, game.stage(), game.screen_on,
                          game.screen_brightness(), game.volume(), game.s.lang, (unsigned long)game.wall_s,
                          (unsigned long)rtc_now(), game.plugged, pmu_ok && pmu.isVbusIn(),
                          pmu_ok && pmu.isCharging(), pmu_ok ? pmu.getBatteryPercent() : -1,
                          pmu_ok ? pmu.getBattVoltage() : -1,
                          x, y, z, store_where(), game.has_routines, game.has_names, audio_listening(), (int)esp_reset_reason(),
                          (unsigned long)diag_loop_max, (unsigned long)diag_loop_slow,
                          (unsigned long)diag_render_max, (unsigned long)diag_push_max,
                          (unsigned long)(diag_lat_n ? diag_lat_sum / diag_lat_n : 0), (unsigned long)diag_lat_max, (unsigned long)diag_lat_n,
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            diag_loop_max = diag_loop_slow = diag_render_max = diag_push_max = diag_lat_max = diag_lat_n = diag_lat_sum = 0;
        } else if (!strcmp(cmd, "i2c")) {
            Serial.print("I2C");
            for (uint8_t ad = 1; ad < 127; ad++) {
                Wire.beginTransmission(ad);
                if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", ad);
            }
            Serial.println();
        } else if (sscanf(cmd, "tap %d %d", &a, &b) == 2) {
            touch_at = millis();
            game.input(up::IN_TOUCH, a, b);
            Serial.printf("OK tap %d %d scene=%d\n", a, b, game.scene);
        } else if (!strcmp(cmd, "sel")) {
            game.input(up::IN_SELECT, 0, 0); Serial.printf("OK scene=%d\n", game.scene);
        } else if (!strcmp(cmd, "shake")) {
            game.input(up::IN_SHAKE, 0, 0); Serial.printf("OK scene=%d\n", game.scene);
        } else if (!strcmp(cmd, "shot")) {
            up::render(game, fb);
            panel_stale = true;                   // the next push sends it all again
            dump_begin();
            Serial.printf("SHOT %d %d\n", up::SCREEN_W, up::SCREEN_H);
            b64_dump((const uint8_t*)fb, up::SCREEN_W * up::SCREEN_H * 2);
            Serial.println("END");
            dump_end();
        } else if (sscanf(cmd, "mic %d", &a) == 1 && a >= up::LK_YESNO && a <= up::LK_TV) {
            diag_mic = a;
            audio_mic(true);
            audio_listen(a);
            Serial.printf("OK mic %d: speak now\n", a);
        } else if (sscanf(cmd, "play %d %d %d", &a, &b, &d) >= 1) {
            audio_play(a, b, game.s.lang, d);
            Serial.printf("OK play %d %d %d\n", a, b, d);
        } else if (!strcmp(cmd, "wipe")) {     // a new egg: the save goes (bench only), then a restart
            prefs.remove("save");
            keep_old_save = true;                 // nothing writes it back before the restart
            Serial.println("OK wipe");
            Serial.flush();
            delay(100);
            ESP.restart();
        } else if (sscanf(cmd, "ask %d", &a) == 1) {
            game.bench_ask(a);
            Serial.printf("OK ask %d scene=%d\n", a, game.scene);
        } else if (sscanf(cmd, "gain %d", &a) == 1 && a >= 0 && a <= 7) {
            audio_mic_gain(a);
            Serial.printf("OK gain %d (%d dB)\n", a, a * 6);
        } else if (sscanf(cmd, "slot %d", &a) == 1 && (a == 0 || a == 1)) {
            audio_mic_slot = a;
            Serial.printf("OK slot %d\n", a);
        } else if (sscanf(cmd, "awake %d", &a) == 1) {
            diag_awake = a;
            Serial.printf("OK awake %d\n", a);
        } else if (sscanf(cmd, "naptest %d", &a) == 1 && a > 0 && a <= 600) {
            nap_n = nap_err = nap_timer = nap_gpio = nap_other = nap_tp_low = nap_boot_low = 0;
            nap_slept_us = nap_awake_us = lit_slept_us = 0; nap_woke_at = 0; lit_n = 0; diag_frames = 0; nap_since = millis();
            memset(sect_us, 0, sizeof sect_us);
            memset(diag_skip, 0, sizeof diag_skip);
            Serial.printf("OK naptest %d: USB goes away until it ends\n", a);
            Serial.flush();
            diag_nap_until = millis() + a * 1000u;
        } else if (!strcmp(cmd, "naps")) {
            uint32_t wall = millis() - nap_since;
            Serial.printf("NAPS n=%lu slept=%lums wall=%lums (%lu%%) timer=%lu gpio=%lu other=%lu err=%lu tp_low=%lu boot_low=%lu "
                          "skip panel=%lu audio=%lu clock=%lu wifi=%lu ota=%lu net=%lu tp=%d boot=%d ",
                          (unsigned long)nap_n, (unsigned long)(nap_slept_us / 1000), (unsigned long)wall,
                          (unsigned long)(wall ? nap_slept_us / 10 / wall : 0), (unsigned long)nap_timer,
                          (unsigned long)nap_gpio, (unsigned long)nap_other, (unsigned long)nap_err,
                          (unsigned long)nap_tp_low, (unsigned long)nap_boot_low,
                          (unsigned long)diag_skip[0], (unsigned long)diag_skip[1], (unsigned long)diag_skip[2],
                          (unsigned long)diag_skip[3], (unsigned long)diag_skip[4], (unsigned long)diag_skip[5],
                          digitalRead(TP_INT), digitalRead(KEY_BOOT));
            Serial.printf("frames=%lu lit_naps=%lu lit_slept=%lums awake=%lums per_wake=%luus sect_us=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
                          (unsigned long)diag_frames, (unsigned long)lit_n, (unsigned long)(lit_slept_us / 1000),
                          (unsigned long)(nap_awake_us / 1000), (unsigned long)(nap_n > 1 ? nap_awake_us / (nap_n - 1) : 0),
                          (unsigned long)sect_us[0], (unsigned long)sect_us[1], (unsigned long)sect_us[2], (unsigned long)sect_us[3],
                          (unsigned long)sect_us[4], (unsigned long)sect_us[5], (unsigned long)sect_us[6], (unsigned long)sect_us[7]);
        } else if (!strcmp(cmd, "batlog")) {
            BatRec r[BATLOG_N];
            size_t n = prefs.getBytes("batlog", r, sizeof r) / sizeof(BatRec);
            Serial.printf("BATLOG n=%u", (unsigned)n);
            for (size_t i = 0; i < n; i++) Serial.printf(" %u:%umV/%u%%/dark%u", r[i].min, r[i].mv, r[i].pct, r[i].dark_pct);
            Serial.println();
        } else if (!strcmp(cmd, "rprof")) {   // where a frame's time goes: render, a plain PSRAM clear, the row checksum
            const int N = 10, W = up::SCREEN_W, H = up::SCREEN_H;
            uint32_t t0 = micros();
            for (int i = 0; i < N; i++) up::render(game, fb);
            uint32_t t_render = (micros() - t0) / N;
            t0 = micros();
            for (int i = 0; i < N; i++) memset(fb, 0, W * H * 2);
            uint32_t t_clear = (micros() - t0) / N;
            volatile uint32_t sink = 0;
            t0 = micros();
            for (int i = 0; i < N; i++) { const uint32_t* w = (const uint32_t*)fb; uint32_t x = 0; for (int k = 0; k < W * H / 2; k++) x ^= w[k]; sink = x; }
            uint32_t t_read = (micros() - t0) / N;
            uint16_t* band = (uint16_t*)heap_caps_malloc(W * 32 * 2, MALLOC_CAP_INTERNAL);
            uint32_t t_band = 0;
            if (band) { t0 = micros(); for (int i = 0; i < N; i++) for (int b = 0; b < H / 32; b++) memset(band, b, W * 32 * 2); t_band = (micros() - t0) / N; free(band); }
            uint32_t t_empty = 0, t_b22 = 0, t_b56 = 0;
            uint16_t* bb = (uint16_t*)heap_caps_malloc(W * 56 * 2, MALLOC_CAP_INTERNAL);
            if (bb) {
                t0 = micros(); for (int i = 0; i < N; i++) up::render_band(game, bb, 0, 0); t_empty = (micros() - t0) / N;
                t0 = micros(); for (int i = 0; i < N; i++) for (int y = 0; y < H; y += 22) up::render_band(game, bb, y, y + 22); t_b22 = (micros() - t0) / N;
                t0 = micros(); for (int i = 0; i < N; i++) for (int y = 0; y < H; y += 56) up::render_band(game, bb, y, y + 56); t_b56 = (micros() - t0) / N;
                free(bb);
            }
            up::render(game, fb);
            panel_stale = true;
            Serial.printf("OK rprof render=%luus psram_clear=%luus psram_read=%luus internal_clear_x14=%luus empty_pass=%luus bands22=%luus bands56=%luus scene=%d\n",
                          (unsigned long)t_render, (unsigned long)t_clear, (unsigned long)t_read, (unsigned long)t_band,
                          (unsigned long)t_empty, (unsigned long)t_b22, (unsigned long)t_b56, game.scene);
        } else if (!strcmp(cmd, "codec")) {
            audio_codec_check();
        } else if (!strcmp(cmd, "rails")) {
            if (!pmu_ok) { Serial.println("RAILS none"); continue; }
            Serial.printf("RAILS dc1=%d dc2=%d dc3=%d dc4=%d dc5=%d aldo1=%d aldo2=%d aldo3=%d aldo4=%d bldo1=%d bldo2=%d cpus=%d dldo1=%d dldo2=%d exio=0x%02X cfg=0x%02X out=0x%02X\n",
                          pmu.isEnableDC1(), pmu.isEnableDC2(), pmu.isEnableDC3(), pmu.isEnableDC4(), pmu.isEnableDC5(),
                          pmu.isEnableALDO1(), pmu.isEnableALDO2(), pmu.isEnableALDO3(), pmu.isEnableALDO4(),
                          pmu.isEnableBLDO1(), pmu.isEnableBLDO2(), pmu.isEnableCPUSLDO(), pmu.isEnableDLDO1(),
                          pmu.isEnableDLDO2(), tca_read(0x00), tca_read(0x03), tca_read(0x01));
        } else if (sscanf(cmd, "boot %d", &a) == 1) {
            diag_boot_until = millis() + a;
            Serial.printf("OK boot %d\n", a);
        } else if (sscanf(cmd, "utc %lu", &u) == 1) {
            bool ok = clock_set_utc(u);
            Serial.printf("%s utc %lu\n", ok ? "OK" : "ERR", u);
        } else {
            Serial.printf("ERR %s\n", cmd);
        }
    }
}

// Her daily routine and TV test mode: the core asks (F_LISTEN), the audio task records, net.cpp asks the listener.
static void listening(uint32_t f) {
    static bool packs_known;
    if (!packs_known) {                       // the packs finish loading a moment after boot
        Pack p;
        game.has_routines = store_pack(PACK_ROUTINES, &p);
        game.has_names = store_pack(PACK_NAMES, &p);
        packs_known = game.has_routines && game.has_names;
        static uint32_t gave_up = millis() + 15000;
        if ((int32_t)(millis() - gave_up) > 0) packs_known = true;   // (absent: the game as before)
    }
    if (f & up::F_LISTEN) {
        int req = game.listen_req, kind = game.listen_kind;
        game.listen_req = up::LREQ_NONE;
        if (req == up::LREQ_CANCEL) { audio_listen_cancel(); audio_routine_stop(); audio_mic(false); }
        else if (req == up::LREQ_LISTEN) { audio_mic(true); audio_listen(kind); }   // after her question
    }
    if (audio_listen_started()) game.listen_started();
    Heard h;
    if (audio_heard(&h)) {
        if (!audio_listening()) audio_mic(false);   // off again between moments (not with another capture waiting)
        if (diag_mic == h.kind) {             // a bench recording: to the serial port, not the game
            diag_mic = -1;
            Serial.printf("HEARD kind=%d speech=%d db=%d loud=%d%% len=%lu\n", h.kind, h.speech, h.db,
                          h.loud_pct, (unsigned long)h.len);
            if (h.wav && h.len) { dump_begin(); Serial.println("WAV"); b64_dump(h.wav, h.len); Serial.println("END"); dump_end(); }
            return;
        }
        if (h.kind == up::LK_LEVEL) game.level_result(h.db, h.loud_pct);
        else if (h.kind == up::LK_TV) net_tv_sample(h.wav, h.len);
        else if (!h.speech) game.listen_result(up::ANS_SILENT);   // nothing said: nothing to send
        else net_answer(game.listen_q, h.kind, h.wav, h.len);
    }
}

void loop() {
    sect_t = esp_timer_get_time();
    static uint32_t last = millis();
    uint32_t now = millis();
    uint32_t dt = now - last;                 // unsigned: survives the 49-day millis() wrap
    if (dt > diag_loop_max) diag_loop_max = dt;
    if (dt > 50) diag_loop_slow++;
    if (!panel_on) batlog_dark_ms += dt;
    last = now;

    int x, y;
    game.set_boot(digitalRead(KEY_BOOT) == LOW || (diag_boot_until && (int32_t)(millis() - diag_boot_until) < 0));   // taps, and the grown-up gesture (3 presses, hold the last)
    if (pwr_pressed()) { touch_at = millis(); Serial.println("KEY PWR"); game.input(up::IN_SELECT, 0, 0); }
    { static bool was; bool b = digitalRead(KEY_BOOT) == LOW; if (b && !was) Serial.println("KEY BOOT"); was = b; }
    bool touch_window = touch_listen_until && (int32_t)(millis() - touch_listen_until) < 0;
    if ((!touch_dozing || touch_window) && touch_read(&x, &y)) { touch_at = millis(); Serial.printf("TOUCH %d %d\n", x, y); game.input(up::IN_TOUCH, x, y); }
    diag_poll();
    read_accel();
    if (shaken()) game.input(up::IN_SHAKE, 0, 0);
    // tilt-a-cloud: sideways tilt at ~20 Hz, only while that game is open
    static uint32_t last_tilt;
    if (game.scene == up::SC_CLOUD && acc_fresh && now - last_tilt >= 50) {
        last_tilt = now;
        int t = (int)(acc_y * 280.0f);        // ~0.35 g (about 20 degrees) of tilt = full deflection
        game.set_tilt(TILT_FLIP ? -t : t);
    }
    SECT(0);

    static uint32_t last_rtc, last_batt;
    if (now - last_rtc >= 1000) {
        last_rtc = now;
        { uint32_t utc = rtc_now();             // one read: local and UTC agree
      game.set_wall(clock_local(utc), clock_trusted(), utc); }   // local for the day, UTC for breaks
        bool test = diag_nap_until && (int32_t)(millis() - diag_nap_until) < 0;   // bench: as if unplugged
        if (diag_nap_until && !test) { diag_nap_until = 0; usb_reconnect(); }
        vbus_now = pmu_ok && pmu.isVbusIn();
        if (pmu_ok) game.set_plugged(vbus_now && !test);   // USB power, not isCharging(): that ends at 100%
    }
    if (now - last_batt >= 30000 || !last_batt) {        // battery % for the indicator
        last_batt = now ? now : 1;
        game.set_battery(pmu_ok && pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1);   // -1 on a PMU error too
    }
    batlog_poll(vbus_now, now);                           // the cable itself: a naptest never starts a log
    static int bright = -1;                              // parent settings: brightness, volume
    if (panel_on && game.screen_brightness() != bright) { bright = game.screen_brightness(); lcd->setBrightness(bright); }   // dims at night
    audio_level(game.volume());
    SECT(1);

    if (diag_awake) { game.idle_ms = 0; game.play_ms = 0; }   // bench: no screen timeout, no play-time break
    uint32_t f = game.tick(dt);
    if (f & up::F_SAVE) save_game();
    if (f & up::F_CLOCK) clock_set_local(game.wall_s);   // a parent set the date and time
    SECT(2);
    clock_poll(now);                          // WiFi time (and zone) sync, about twice a day
    SECT(3);
    wifi_setup_poll(game, now, f);            // a parent adding a network from the toy
    SECT(4);
    // the update's battery rule on fresh readings: the game's plugged state is debounced over ~2 s
    bool power_ok = !(f & up::F_UPDATE) || (pmu_ok && (pmu.isVbusIn() ||
                    (pmu.isBatteryConnect() && pmu.getBatteryPercent() >= 50)));
    if (ota_poll(game, now, f, power_ok)) {             // a verified update is in the other slot: save her, restart
        save_game();
        push_frame();                         // "restarting"
        delay(1500);
        ESP.restart();
    }
    if (f & up::F_SOUND) {
        int line, clip, sfx = game.take_sfx(&line, &clip);
        audio_play(sfx, line, game.s.lang, clip);   // a routine clip, her voice, else the tune
    }
    listening(f);                             // (after the sound: she asks first, then listens)
    SECT(5);
    net_poll(game, now);
    SECT(6);
    panel_power(f & up::F_SCREEN_ON);
    touch_doze(!panel_on && !game.plugged);
    touch_booted();
    // the core asks for a frame on every call while something moves; cap pushes at ~30 fps
    static bool pending;
    static uint32_t last_push;
    pending |= (f & up::F_REDRAW) != 0 || panel_stale;   // (a woken panel gets its frame even in a still scene)
    // ~30 fps for animation, but a frame answering a touch goes out at once (it waited up to 33 ms before)
    if (pending && panel_on && (millis() - last_push >= 33 || touch_at)) {
        uint32_t start = millis();         // ~30 fps counts from frame start to frame start (it used to count
        push_frame();                      // from the end: the frame's own time + 33 ms, ~17 fps measured)
        last_push = start;
        pending = false;
    }

    // light sleep would drop the WiFi association: while the link is up only modem sleep (USB power, mostly)
    // On USB power light sleep saves nothing and takes the USB port down with it (no console, no
    // flashing without the BOOT button): only on the battery.
    SECT(7);
    audio_sleep();                            // the codec off between sounds (returns at once while one plays)
    bool busy[6] = {panel_on || touch_window, audio_busy(), clock_busy(), wifi_setup_busy(), ota_busy(), net_busy()};
    bool awake = false;
    for (int i = 0; i < 6; i++) if (busy[i]) { awake = true; diag_skip[i]++; break; }
    if (awake || game.plugged) nap_woke_at = 0;   // (only back-to-back naps count as awake-between-naps)
    bool others = busy[1] || busy[2] || busy[3] || busy[4] || busy[5];
    if (game.plugged) delay(awake ? 5 : 20);
    else if (!awake) nap();
    else if (panel_on && !others) {
        // Lit on the battery, nothing playing or on the air: the gap until the next frame (or the next 20 ms
        // look at PWR) is slept instead of idled at 240 MHz. A touch or BOOT ends it at once (GPIO wake);
        // waking costs <1 ms, taken off the end so the frame is not late.
        int32_t gap = pending ? 33 - (int32_t)(millis() - last_push) : 20;
        if (gap > 20) gap = 20;
        if (gap >= 4) nap((uint32_t)gap * 1000 - 1000, true); else delay(5);
    } else delay(5);
}
