// Minimal PCF85063 driver (NXP datasheet: registers 0x04..0x0A hold BCD seconds..years; bit 7 of
// the seconds register, OS, is set when the oscillator stopped and the time is unreliable).
//
// VERIFY on the board: whether the AXP2101 keeps the RTC supplied after a long-press power-off.
// If it does not, rtc_now() reports 0 after a power cut and the game counts powered time only.
#include <Arduino.h>
#include <Wire.h>
#include "rtc.h"

static const uint8_t ADDR = 0x51, REG_SECONDS = 0x04;
static bool present, lost;

static uint8_t bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t to_bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// days since 2000-01-01 (H. Hinnant's days_from_civil, shifted)
static int32_t days_from_2000(int y, int m, int d) {
    y -= m <= 2;
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 730425;    // 730425 = days from 0000-03-01 to 2000-01-01
}

static bool read_regs(uint8_t* r) {
    Wire.beginTransmission(ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDR, (uint8_t)7) != 7) return false;
    for (int i = 0; i < 7; i++) r[i] = Wire.read();
    return true;
}

bool rtc_begin() {
    uint8_t r[7];
    present = read_regs(r);
    lost = present && (r[0] & 0x80);       // oscillator stopped: the time is meaningless
    return present;
}

bool rtc_lost() { return lost; }

bool rtc_set(uint32_t t) {                 // UTC seconds since 2000-01-01; also clears OS
    if (!present) return false;
    uint32_t d = t / 86400, s = t % 86400;
    int y = 2000, m = 1;                   // walk the calendar: runs once per flash, not per frame
    for (;;) { int n = (y % 4 == 0 && (y % 100 || y % 400 == 0)) ? 366 : 365; if (d < (uint32_t)n) break; d -= n; y++; }
    static const uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (;; m++) { int n = DAYS[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 || y % 400 == 0)); if (d < (uint32_t)n) break; d -= n; }
    Wire.beginTransmission(ADDR);
    Wire.write(REG_SECONDS);
    const uint8_t v[7] = {to_bcd(s % 60), to_bcd(s / 60 % 60), to_bcd(s / 3600), to_bcd(d + 1), 0,
                          to_bcd(m), to_bcd(y - 2000)};
    Wire.write(v, sizeof v);
    if (Wire.endTransmission() != 0) return false;   // NACK: the chip still has its old time
    lost = false;
    return true;
}

uint32_t rtc_now() {
    uint8_t r[7];
    if (!present || !read_regs(r) || (r[0] & 0x80)) return 0;
    int sec = bcd(r[0] & 0x7F), min = bcd(r[1] & 0x7F), hour = bcd(r[2] & 0x3F);
    int day = bcd(r[3] & 0x3F), month = bcd(r[5] & 0x1F), year = 2000 + bcd(r[6]);
    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
    uint32_t t = (uint32_t)days_from_2000(year, month, day) * 86400u + hour * 3600u + min * 60u + sec;
    return t ? t : 1;                      // 0 means "unknown" to the game
}
