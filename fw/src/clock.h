#pragma once
#include <stdint.h>
#include <Preferences.h>
// The toy's clock. The PCF85063 keeps UTC; local time (what the game wants) comes from a POSIX
// time-zone rule, so summer time switches by itself, WiFi or not. With WiFi (fw/src/secrets.h)
// the time is fetched over NTP about every 12 h, and the zone from the toy's IP address when
// config.h leaves TIMEZONE empty. All I2C and NVS writes happen on the caller's (main) task.
void clock_begin(Preferences& prefs);   // after rtc_begin(): pick the zone, seed a fresh build
uint32_t clock_local();                 // local seconds since 2000, 0 if the RTC has no time
bool clock_trusted();                   // the date can be believed (else day/night etc. pause)
void clock_set_local(uint32_t local);   // a parent set the clock on the settings page
void clock_poll(uint32_t now_ms);       // call every loop: starts and finishes WiFi syncs
bool clock_busy();                      // a sync is running: do not light-sleep
// for the WiFi setup, which runs its own short NTP check on the main task
void ntp_start();
bool ntp_done(uint32_t* utc);           // UTC seconds since 2000 once a fresh answer is in
void ntp_stop();
void clock_set_utc(uint32_t utc);
