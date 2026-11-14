#pragma once
#include <stdint.h>
#include "../../core/unicorn.h"
// WiFi from the toy: a parent starts a setup in the settings, the toy opens a short-lived,
// password-protected network of its own and shows it as a QR code; the phone joins it, opens the
// setup page (portal.html), picks the home network and types its password there. Networks saved
// this way (at most 5, NVS namespace "wifi") join the ones built in through secrets.h.
struct WifiNet { char ssid[33]; char pass[65]; };
enum { WIFI_KNOWN_MAX = 8 };

void wifi_setup_begin(up::Game& game);    // load the saved list and show it in the settings
void wifi_setup_poll(up::Game& game, uint32_t now_ms, uint32_t flags);   // every loop, main task
bool wifi_setup_busy();                   // the setup holds the radio (no sync, no light sleep)
int wifi_known(WifiNet* out, int max);    // saved + built-in networks, saved first, no duplicates
// For the background tasks (clock sync, firmware update), with a copy of wifi_known() made on the main
// task: join the first of nets that answers (station mode, up to 15 s), and switch the radio off again.
// The link is shared and counted: the radio goes off when the last task leaves. Refused (false) while the
// parent's WiFi setup owns the radio; the setup starts only when nobody holds or is joining the link.
bool wifi_join(const WifiNet* nets, int n);
bool wifi_home_scan(const WifiNet* nets, int n);   // a known network in range (or connected): she is at home
void wifi_leave();
