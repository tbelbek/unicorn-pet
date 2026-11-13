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

static const int LCD_CS = 12, LCD_SCLK = 11, LCD_D0 = 4, LCD_D1 = 5, LCD_D2 = 6, LCD_D3 = 7;
static const int I2C_SDA = 15, I2C_SCL = 14, TP_INT = 21, KEY_BOOT = 0;
static const uint8_t TCA9554_ADDR = 0x20, FT3168_ADDR = 0x38, CST820_ADDR = 0x15;
static const uint8_t EXIO_PWR = 4;                        // PWR key reads HIGH while held
static const bool TOUCH_SWAP_XY = false, TOUCH_FLIP_X = false, TOUCH_FLIP_Y = false;   // checked on a CO5300/CST820 board: corners read (1,1)..(360,447)
static const bool TILT_FLIP = false;                      // VERIFY: tilting right moves the cloud right
static const uint8_t BRIGHTNESS = 180;                    // indoor toy: no need for full AMOLED
static const uint32_t SLEEP_POLL_US = 200000;             // light-sleep wake to poll PWR (no GPIO wake)
static const float SHAKE_G = 1.2f;                        // |a| - 1 g beyond this counts as a jolt
// Every chip on the shared I2C bus (AXP2101, TCA9554, FT3168, QMI8658, PCF85063, ES8311) does
// 400 kHz fast mode: touch, PWR and the accelerometer are read 4x faster than at the 100 kHz default.
static const uint32_t I2C_HZ = 400000;
static const uint32_t LCD_HZ = 40000000;                  // QSPI clock; VERIFY whether 80 MHz is clean

static Arduino_DataBus* bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
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
static uint16_t* fb;
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
static void expander_init() {
    tca_write(0x03, 0xF9);        // config: 1 = input; EXIO1, EXIO2 outputs
    tca_write(0x01, 0x00);
    delay(20);
    tca_write(0x01, 0x06);
    delay(120);
}

// ---- panel ---------------------------------------------------------------------------------

static bool panel_stale = true;           // the panel's content is unknown: send the whole frame

static void panel_power(bool on) {
    if (on == panel_on) return;
    if (on) { lcd->displayOn(); lcd->setBrightness(game.screen_brightness()); panel_stale = true; }
    else {
        lcd->displayOff();        // AMOLED: every pixel off, nothing to leak
        if (pmu_ok) pmu.clearIrqStatus();   // a press made while lit is not a new one in the dark
    }
    panel_on = on;
}

// Only the rows that changed go over the QSPI bus (a full frame is 330 KB, ~17 ms at 40 MHz).
// The core redraws on every step while anything moves, but most of the screen stays the same
// and many frames are identical (breathing moves a pixel every ~200 ms): those send nothing.
// A checksum per row decides; a full frame every 64 sends heals any (2^-32) collision.
static uint32_t row_sum[up::SCREEN_H];

static void push_frame() {
    static uint8_t sends;
    up::render(game, fb);
    ota_frame_pushed(millis());            // a new firmware on trial is kept once it has drawn for a while
    bool full = panel_stale || ++sends % 64 == 0;
    int y0 = up::SCREEN_H, y1 = -1;
    const uint32_t* w = (const uint32_t*)fb;
    for (int y = 0; y < up::SCREEN_H; y++) {
        uint32_t s = 2166136261u;
        for (int i = 0; i < up::SCREEN_W / 2; i++) s = (s ^ *w++) * 16777619u;
        if (s == row_sum[y] && !full) continue;
        row_sum[y] = s;
        if (y < y0) y0 = y;
        y1 = y;
    }
    panel_stale = false;
    if (y1 < 0) return;                    // nothing changed
    y0 &= ~1;                              // SH8601/CO5300 windows start on an even row...
    y1 |= 1;                               // ...and span an even number of rows
    lcd->draw16bitRGBBitmap(0, y0, fb + y0 * up::SCREEN_W, up::SCREEN_W, y1 - y0 + 1);
}

// ---- inputs --------------------------------------------------------------------------------

static bool edge(bool down, bool* last, uint32_t* t, uint32_t debounce) {
    if (down != *last && millis() - *t > debounce) { *t = millis(); *last = down; return down; }
    return false;
}

// PWR is also the AXP2101's power key. While the screen is dark we only wake every SLEEP_POLL_US,
// so a child's quick press (~100 ms) would often fall between two looks at EXIO4: the PMU latches
// a short press as an interrupt flag, which is read (and cleared) instead.
static bool pmu_short_press() {
    if (!pmu_ok) return false;
    pmu.getIrqStatus();
    bool pressed = pmu.isPekeyShortPressIrq();
    pmu.clearIrqStatus();
    return pressed;
}

static bool pwr_pressed() {
    static bool last; static uint32_t t, polled;
    if (!panel_on) { tca_read(0x00); return pmu_short_press(); }
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

// ---- save ----------------------------------------------------------------------------------

static bool keep_old_save;                    // an unreadable save could not be backed up: leave it alone

static void save_game() {
    if (keep_old_save) return;
    uint8_t buf[up::SAVE_SIZE];
    game.save(buf);
    prefs.putBytes("save", buf, sizeof buf);
}

static void load_game() {
    size_t len = prefs.getBytesLength("save");
    if (!len) { Serial.println("no save: new egg"); return; }
    uint8_t* buf = (uint8_t*)malloc(len);     // any size: a newer firmware's save may be longer
    size_t n = buf ? prefs.getBytes("save", buf, len) : 0;
    if (n && game.load(buf, n)) { Serial.println("save loaded"); free(buf); return; }
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
static void nap() {
    gpio_wakeup_enable((gpio_num_t)KEY_BOOT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)TP_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(SLEEP_POLL_US);
    esp_light_sleep_start();
}

// ---- main ----------------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
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
    } else {
        lcd = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, up::SCREEN_W, up::SCREEN_H);
    }
    Serial.printf("board: %s panel, touch at 0x%02X\n", v2 ? "CO5300" : "SH8601", touch_addr);
    if (!lcd->begin(LCD_HZ)) Serial.println("display init failed");
    lcd->setBrightness(BRIGHTNESS);

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
//   boot MS (BOOT held that long; "boot 80" is a tap) | awake 0/1 (no screen timeout while testing)
// Physical access only: nothing secret is printed (no token, no WiFi password).
static int diag_mic = -1;                     // a bench recording is under way: its result comes here
static uint32_t diag_boot_until;              // "boot MS": BOOT held down from the bench
static bool diag_awake;                       // "awake 1": the screen never times out (bench only, not saved)

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
                          "acc=%.2f,%.2f,%.2f store=%s routines=%d names=%d listening=%d\n",
                          FW_VERSION, (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getFreePsram(), game.scene, game.stage(), game.screen_on,
                          game.screen_brightness(), game.volume(), game.s.lang, (unsigned long)game.wall_s,
                          (unsigned long)rtc_now(), game.plugged, pmu_ok && pmu.isVbusIn(),
                          pmu_ok && pmu.isCharging(), pmu_ok ? pmu.getBatteryPercent() : -1,
                          pmu_ok ? pmu.getBattVoltage() : -1,
                          x, y, z, store_where(), game.has_routines, game.has_names, audio_listening());
        } else if (!strcmp(cmd, "i2c")) {
            Serial.print("I2C");
            for (uint8_t ad = 1; ad < 127; ad++) {
                Wire.beginTransmission(ad);
                if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", ad);
            }
            Serial.println();
        } else if (sscanf(cmd, "tap %d %d", &a, &b) == 2) {
            game.input(up::IN_TOUCH, a, b);
            Serial.printf("OK tap %d %d scene=%d\n", a, b, game.scene);
        } else if (!strcmp(cmd, "sel")) {
            game.input(up::IN_SELECT, 0, 0); Serial.printf("OK scene=%d\n", game.scene);
        } else if (!strcmp(cmd, "shake")) {
            game.input(up::IN_SHAKE, 0, 0); Serial.printf("OK scene=%d\n", game.scene);
        } else if (!strcmp(cmd, "shot")) {
            up::render(game, fb);
            panel_stale = true;                   // the next push sends it all again
            Serial.printf("SHOT %d %d\n", up::SCREEN_W, up::SCREEN_H);
            b64_dump((const uint8_t*)fb, up::SCREEN_W * up::SCREEN_H * 2);
            Serial.println("END");
        } else if (sscanf(cmd, "mic %d", &a) == 1 && a >= up::LK_YESNO && a <= up::LK_TV) {
            diag_mic = a;
            audio_mic(true);
            audio_listen(a);
            Serial.printf("OK mic %d: speak now\n", a);
        } else if (sscanf(cmd, "play %d %d %d", &a, &b, &d) >= 1) {
            audio_play(a, b, game.s.lang, d);
            Serial.printf("OK play %d %d %d\n", a, b, d);
        } else if (sscanf(cmd, "gain %d", &a) == 1 && a >= 0 && a <= 7) {
            audio_mic_gain(a);
            Serial.printf("OK gain %d (%d dB)\n", a, a * 6);
        } else if (sscanf(cmd, "slot %d", &a) == 1 && (a == 0 || a == 1)) {
            audio_mic_slot = a;
            Serial.printf("OK slot %d\n", a);
        } else if (sscanf(cmd, "awake %d", &a) == 1) {
            diag_awake = a;
            Serial.printf("OK awake %d\n", a);
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
            if (h.wav && h.len) { Serial.println("WAV"); b64_dump(h.wav, h.len); Serial.println("END"); }
            return;
        }
        if (h.kind == up::LK_LEVEL) game.level_result(h.db, h.loud_pct);
        else if (h.kind == up::LK_TV) net_tv_sample(h.wav, h.len);
        else if (!h.speech) game.listen_result(up::ANS_SILENT);   // nothing said: nothing to send
        else net_answer(game.listen_q, h.kind, h.wav, h.len);
    }
}

void loop() {
    static uint32_t last = millis();
    uint32_t now = millis();
    uint32_t dt = now - last;                 // unsigned: survives the 49-day millis() wrap
    last = now;

    int x, y;
    game.set_boot(digitalRead(KEY_BOOT) == LOW || (int32_t)(millis() - diag_boot_until) < 0);   // taps, and the grown-up gesture (3 presses, hold the last)
    if (pwr_pressed()) game.input(up::IN_SELECT, 0, 0);
    if (touch_read(&x, &y)) { Serial.printf("TOUCH %d %d\n", x, y); game.input(up::IN_TOUCH, x, y); }
    diag_poll();
    read_accel();
    if (shaken()) game.input(up::IN_SHAKE, 0, 0);
    // tilt-a-cloud: sideways tilt at ~10 Hz, only while that game is open (VERIFY axis and sign)
    static uint32_t last_tilt;
    if (game.scene == up::SC_CLOUD && acc_fresh && now - last_tilt >= 100) {
        last_tilt = now;
        int t = (int)(acc_x * 200.0f);        // ~0.5 g of tilt = full deflection
        game.set_tilt(TILT_FLIP ? -t : t);
    }

    static uint32_t last_rtc, last_batt;
    if (now - last_rtc >= 1000) {
        last_rtc = now;
        { uint32_t utc = rtc_now();             // one read: local and UTC agree
      game.set_wall(clock_local(utc), clock_trusted(), utc); }   // local for the day, UTC for breaks
        if (pmu_ok) game.set_plugged(pmu.isVbusIn());   // USB power, not isCharging(): that ends at 100%
    }
    if (now - last_batt >= 30000 || !last_batt) {        // battery % for the indicator
        last_batt = now ? now : 1;
        game.set_battery(pmu_ok && pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1);   // -1 on a PMU error too
    }
    static int bright = -1;                              // parent settings: brightness, volume
    if (panel_on && game.screen_brightness() != bright) { bright = game.screen_brightness(); lcd->setBrightness(bright); }   // dims at night
    audio_level(game.volume());

    if (diag_awake) game.idle_ms = 0;
    uint32_t f = game.tick(dt);
    if (f & up::F_SAVE) save_game();
    if (f & up::F_CLOCK) clock_set_local(game.wall_s);   // a parent set the date and time
    clock_poll(now);                          // WiFi time (and zone) sync, about twice a day
    wifi_setup_poll(game, now, f);            // a parent adding a network from the toy
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
    net_poll(game, now);
    panel_power(f & up::F_SCREEN_ON);
    // the core asks for a frame on every call while something moves; cap pushes at ~30 fps
    static bool pending;
    static uint32_t last_push;
    pending |= (f & up::F_REDRAW) != 0;
    if (pending && panel_on && millis() - last_push >= 33) {
        push_frame();
        last_push = millis();
        pending = false;
    }

    // light sleep would drop the WiFi association: while the link is up only modem sleep (USB power, mostly)
    // On USB power light sleep saves nothing and takes the USB port down with it (no console, no
    // flashing without the BOOT button): only on the battery.
    if (panel_on || audio_busy() || clock_busy() || wifi_setup_busy() || ota_busy() || net_busy()) delay(5);
    else if (game.plugged) delay(20);
    else nap();
}
