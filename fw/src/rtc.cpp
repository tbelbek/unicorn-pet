// Minimal PCF85063 driver (NXP datasheet: registers 0x04..0x0A hold BCD seconds..years; bit 7 of
// the seconds register, OS, is set when the oscillator stopped and the time is unreliable).
//
// VERIFY on the board: whether the AXP2101 keeps the RTC supplied after a long-press power-off.
// If it does not, rtc_now() reports 0 after a power cut and the game counts powered time only.
#include <Arduino.h>
#include <Wire.h>
#include "rtc.h"

static const uint8_t ADDR = 0x51, REG_SECONDS = 0x04;
static bool present;

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
    if (!present) return false;
    if (r[0] & 0x80) {                     // oscillator stopped: start over at 2026-01-01 00:00:00
        Wire.beginTransmission(ADDR);
        Wire.write(REG_SECONDS);
        const uint8_t t[7] = {0x00, 0x00, 0x00, to_bcd(1), 4, to_bcd(1), to_bcd(26)};   // clears OS
        Wire.write(t, sizeof t);
        Wire.endTransmission();
    }
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
