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

static const int LCD_CS = 12, LCD_SCLK = 11, LCD_D0 = 4, LCD_D1 = 5, LCD_D2 = 6, LCD_D3 = 7;
static const int I2C_SDA = 15, I2C_SCL = 14, TP_INT = 21, KEY_BOOT = 0;
static const uint8_t TCA9554_ADDR = 0x20, FT3168_ADDR = 0x38;
static const uint8_t EXIO_PWR = 4;                        // PWR key reads HIGH while held
static const bool TOUCH_SWAP_XY = false, TOUCH_FLIP_X = false, TOUCH_FLIP_Y = false;   // VERIFY
static const bool TILT_FLIP = false;                      // VERIFY: tilting right moves the cloud right
static const uint8_t BRIGHTNESS = 180;                    // indoor toy: no need for full AMOLED
static const uint32_t SLEEP_POLL_US = 200000;             // light-sleep wake to poll PWR (no GPIO wake)
static const float SHAKE_G = 1.2f;                        // |a| - 1 g beyond this counts as a jolt

static Arduino_DataBus* bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
// Waveshare's examples drive this board as SH8601; the product page also lists CO5300 for
// newer batches. Build with -DPANEL_CO5300 if the screen stays black.
#ifdef PANEL_CO5300
static Arduino_CO5300* lcd = new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, false, up::SCREEN_W, up::SCREEN_H);
#else
static Arduino_SH8601* lcd = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, up::SCREEN_W, up::SCREEN_H);
#endif
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

static void panel_power(bool on) {
    if (on == panel_on) return;
    if (on) { lcd->displayOn(); lcd->setBrightness(game.brightness()); }
    else lcd->displayOff();       // AMOLED: every pixel off, nothing to leak
    panel_on = on;
}

static void push_frame() {
    up::render(game, fb);
    lcd->draw16bitRGBBitmap(0, 0, fb, up::SCREEN_W, up::SCREEN_H);
}

// ---- inputs --------------------------------------------------------------------------------

static bool edge(bool down, bool* last, uint32_t* t, uint32_t debounce) {
    if (down != *last && millis() - *t > debounce) { *t = millis(); *last = down; return down; }
    return false;
}

static bool pwr_pressed() {
    static bool last; static uint32_t t;
    int v = tca_read(0x00);
    return edge(v >= 0 && ((v >> EXIO_PWR) & 1), &last, &t, 60);
}

static bool touch_read(int* x, int* y) {
    static bool was_down = false;
    uint8_t r[5];
    Wire.beginTransmission(FT3168_ADDR);
    Wire.write(0x02);             // finger count, then X1 hi/lo, Y1 hi/lo
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(FT3168_ADDR, (uint8_t)5) != 5) return false;
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

static void read_accel() {                // once per loop; shake and tilt both use it
    acc_fresh = imu_ok && imu.getDataReady() && imu.getAccelerometer(acc_x, acc_y, acc_z);
}

static bool shaken() {
    static uint32_t first_jolt, cooldown_until;
    if (!acc_fresh || millis() < cooldown_until) return false;
    float x = acc_x, y = acc_y, z = acc_z;
    float g = sqrtf(x * x + y * y + z * z);
    if (fabsf(g - 1.0f) < SHAKE_G) return false;
    uint32_t now = millis();
    if (!panel_on || (first_jolt && now - first_jolt < 500)) {
        first_jolt = 0;
        cooldown_until = now + 1500;
        return true;
    }
    first_jolt = now;
    return false;
}

// ---- save ----------------------------------------------------------------------------------

static void save_game() {
    uint8_t buf[up::SAVE_SIZE];
    game.save(buf);
    prefs.putBytes("save", buf, sizeof buf);
}

static void load_game() {
    static uint8_t buf[512];                  // older saves are shorter, a newer firmware's longer
    size_t len = prefs.getBytesLength("save");
    size_t n = len && len <= sizeof buf ? prefs.getBytes("save", buf, len) : 0;
    if (n && game.load(buf, n)) { Serial.println("save loaded"); return; }
    Serial.println("no valid save: new egg");
    // Keep an unreadable save (a newer firmware's, or a bad write) instead of overwriting it with
    // the new egg: a later firmware can still read it back from "save_bad".
    if (n && !prefs.isKey("save_bad")) prefs.putBytes("save_bad", buf, n);
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
    }
    expander_init();

    if (!lcd->begin()) Serial.println("display init failed");
    lcd->setBrightness(BRIGHTNESS);

    imu_ok = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    if (imu_ok) {
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_1000Hz,
                                SensorQMI8658::LPF_MODE_0);
        imu.enableAccelerometer();
    } else {
        Serial.println("QMI8658 not found: shake disabled");
    }
    if (!audio_begin()) Serial.println("ES8311 init failed: sound disabled");
    if (!rtc_begin()) Serial.println("PCF85063 not found: breaks count powered time only");

    fb = (uint16_t*)ps_malloc(up::SCREEN_W * up::SCREEN_H * sizeof(uint16_t));
    if (!fb) { Serial.println("no PSRAM for framebuffer"); for (;;) delay(1000); }

    prefs.begin("unicorn", false);
    game.init(esp_random());
    clock_begin(prefs);
    game.set_wall(clock_local(), clock_trusted());   // before the save, so time spent off counts
    load_game();
    push_frame();
}

void loop() {
    static uint32_t last = millis();
    uint32_t now = millis();
    uint32_t dt = now - last;                 // unsigned: survives the 49-day millis() wrap
    last = now;

    int x, y;
    game.set_boot(digitalRead(KEY_BOOT) == LOW);   // taps, and the grown-up gesture (3 presses, hold the last)
    if (pwr_pressed()) game.input(up::IN_SELECT, 0, 0);
    if (touch_read(&x, &y)) game.input(up::IN_TOUCH, x, y);
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
        game.set_wall(clock_local(), clock_trusted());
        if (pmu_ok) game.set_plugged(pmu.isVbusIn());   // USB power, not isCharging(): that ends at 100%
    }
    if (now - last_batt >= 30000 || !last_batt) {        // battery % for the indicator
        last_batt = now ? now : 1;
        if (pmu_ok && pmu.isBatteryConnect()) game.set_battery(pmu.getBatteryPercent());
    }
    static int bright = -1;                              // parent settings: brightness, volume
    if (panel_on && game.brightness() != bright) { bright = game.brightness(); lcd->setBrightness(bright); }
    audio_level(game.volume());

    uint32_t f = game.tick(dt);
    if (f & up::F_SAVE) save_game();
    if (f & up::F_CLOCK) clock_set_local(game.wall_s);   // a parent set the date and time
    clock_poll(now);                          // WiFi time (and zone) sync, about twice a day
    if (f & up::F_SOUND) audio_play(game.take_sfx());
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

    if (panel_on || audio_busy() || clock_busy()) delay(5);
    else nap();
}
