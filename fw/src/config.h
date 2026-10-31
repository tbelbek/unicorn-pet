#pragma once
// Where the toy lives. Empty = automatic: when it is on WiFi it asks ip-api.com which time zone
// its internet connection is in (that is the "location": a city from the IP address, nothing
// more precise, nothing stored but the zone name). Set an IANA name to fix it instead, e.g.
// "Europe/Stockholm" or "Europe/Istanbul" (the list is in tz.h), or a POSIX rule such as
// "CET-1CEST,M3.5.0,M10.5.0/3". A name not in the list counts as empty: WiFi looks it up.
#define TIMEZONE ""

// Until a zone is known (no WiFi yet, nothing set above): where the toy was bought.
#define TIMEZONE_FALLBACK "Europe/Stockholm"
