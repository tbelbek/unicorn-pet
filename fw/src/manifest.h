#pragma once
// The release manifest (manifest.json next to firmware.bin in a GitHub release), as tools/release.py
// writes it: {"version": 2, "size": 3539527, "sha256": "<64 hex>", "sig": "<base64 DER ECDSA P-256>"}.
// It arrives over a connection nobody vouches for (TLS is not validated), so the parser only has to
// be memory-safe and strict; the signature over ota_message() is what makes it trustworthy.
// Header-only and libc-only, so test/test_ota.cpp runs it on the host.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Manifest { long version; uint32_t size; char sha256[65]; char sig[97]; };

// the value after "key": in json, or 0
inline const char* manifest_value(const char* json, const char* key) {
    char pat[16];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

// a quoted string of 1..n-1 characters, all from allowed; false otherwise
inline bool manifest_string(const char* json, const char* key, char* out, int n, const char* allowed) {
    const char* p = manifest_value(json, key);
    if (!p || *p++ != '"') return false;
    int i = 0;
    while (p[i] && p[i] != '"') {
        if (i >= n - 1 || !strchr(allowed, p[i])) return false;
        out[i] = p[i];
        i++;
    }
    out[i] = 0;
    return p[i] == '"' && i > 0;
}

inline bool manifest_number(const char* json, const char* key, long max, long* out) {
    const char* p = manifest_value(json, key);
    if (!p || *p < '0' || *p > '9') return false;   // no sign, no spaces, no hex
    char* end = 0;
    long v = strtol(p, &end, 10);
    if (v < 1 || v > max) return false;
    *out = v;
    return true;
}

inline bool parse_manifest(const char* json, Manifest* m) {
    static const char HEXD[] = "0123456789abcdef";
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    long size = 0;
    memset(m, 0, sizeof *m);
    if (!json || !manifest_number(json, "version", 65535, &m->version) ||
        !manifest_number(json, "size", 64L * 1024 * 1024, &size) ||
        !manifest_string(json, "sha256", m->sha256, sizeof m->sha256, HEXD) || strlen(m->sha256) != 64 ||
        !manifest_string(json, "sig", m->sig, sizeof m->sig, B64))
        return false;
    m->size = (uint32_t)size;
    return true;
}

// What the release signature covers, byte for byte (tools/release.py signs the same string).
inline int ota_message(const Manifest& m, char* out, int n) {
    return snprintf(out, n, "unicorn-pet|%ld|%lu|%s", m.version, (unsigned long)m.size, m.sha256);
}
