#pragma once
#include <stdint.h>
// PCF85063 real-time clock on the shared I2C bus. Only elapsed time matters to the game, so
// the calendar does not need to be right, just steady.
bool rtc_begin();            // after Wire.begin(); restarts a clock that lost power
uint32_t rtc_now();          // seconds since 2000-01-01, or 0 if the clock is not trustworthy
