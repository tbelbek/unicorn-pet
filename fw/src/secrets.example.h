#pragma once
// Copy this file to secrets.h (git ignores secrets.h, so passwords never reach the public repo)
// and list your WiFi networks. Without secrets.h the firmware builds and runs without WiFi.
//
// The toy tries them in turn about once every 12 hours, only to set its clock (and, if
// TIMEZONE in config.h is empty, to look up its time zone); then the radio goes off again.
#define WIFI_NETWORKS { \
    {"Home WiFi", "password"}, \
    {"Grandma's WiFi", "password"}, \
}
