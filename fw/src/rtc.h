#pragma once
#include <stdint.h>
// PCF85063 real-time clock on the shared I2C bus, holding UTC. clock.cpp turns it into local time
// with a time-zone rule (summer time included) and decides when to set it.
bool rtc_begin();            // after Wire.begin(); false if the chip does not answer
bool rtc_lost();             // the oscillator had stopped (battery ran flat): time is meaningless
bool rtc_set(uint32_t t);    // UTC seconds since 2000-01-01; false if the write failed
uint32_t rtc_now();          // UTC seconds since 2000-01-01, or 0 if not trustworthy
