// The release manifest parser the firmware runs on what it downloads (fw/src/manifest.h).
//   make -C test ota
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../fw/src/manifest.h"

int main() {
    const char* sha = "3f1a9c0b4e6d2f8a7b5c1d0e9f8a7b6c5d4e3f2a1b0c9d8e7f6a5b4c3d2e1f0a";
    const char* sig = "MEUCIQDx3k7m4Vz9pQ2rT8uW1yA5bC6dE7fG8hI9jK0lM1nO2gIgP3qR4sT5uV6wX7yZ8aB9cD0eF1gH2iJ3kL4mN5oP6qR=";
    char good[512];
    std::snprintf(good, sizeof good, "{\"version\": 2, \"size\": 3555323, \"sha256\": \"%s\", \"sig\": \"%s\"}\n", sha, sig);
    Manifest m;
    assert(parse_manifest(good, &m));               // json.dumps' layout, as tools/release.py writes it
    assert(m.version == 2 && m.size == 3555323 && !std::strcmp(m.sha256, sha) && !std::strcmp(m.sig, sig));
    char msg[160];
    ota_message(m, msg, sizeof msg);                // what the signature covers
    char want[160];
    std::snprintf(want, sizeof want, "unicorn-pet|2|3555323|%s", sha);
    assert(!std::strcmp(msg, want));

    char bad[700];
    auto rejects = [&](const char* fmt, const char* a, const char* b) {
        std::snprintf(bad, sizeof bad, fmt, a, b);
        return !parse_manifest(bad, &m);
    };
    const char* F = "{\"version\": 2, \"size\": 10, \"sha256\": \"%s\", \"sig\": \"%s\"}";
    assert(!rejects(F, sha, sig));
    assert(rejects(F, "3F1A9C0B4E6D2F8A7B5C1D0E9F8A7B6C5D4E3F2A1B0C9D8E7F6A5B4C3D2E1F0A", sig));   // upper case
    assert(rejects(F, "3f1a", sig));                                                                 // short hash
    assert(rejects(F, sha, ""));                                                                     // no signature
    assert(rejects(F, sha, "MEUC\\\"x"));                                                            // not base64
    char longsig[200];
    std::memset(longsig, 'A', 120); longsig[120] = 0;
    assert(rejects(F, sha, longsig));                                                                // too long
    assert(rejects("{\"version\": -2, \"size\": 10, \"sha256\": \"%s\", \"sig\": \"%s\"}", sha, sig));
    assert(rejects("{\"version\": 0, \"size\": 10, \"sha256\": \"%s\", \"sig\": \"%s\"}", sha, sig));
    assert(rejects("{\"version\": 70000, \"size\": 10, \"sha256\": \"%s\", \"sig\": \"%s\"}", sha, sig));
    assert(rejects("{\"version\": \"2\", \"size\": 10, \"sha256\": \"%s\", \"sig\": \"%s\"}", sha, sig));
    assert(rejects("{\"version\": 2, \"size\": 99999999999, \"sha256\": \"%s\", \"sig\": \"%s\"}", sha, sig));
    assert(rejects("{\"version\": 2, \"sha256\": \"%s\", \"sig\": \"%s\"}", sha, sig));                // no size
    assert(rejects("{\"version\": 2, \"size\": 10, \"sha256\": \"%s, \"sig\": \"%s\"}", sha, sig));    // unterminated
    assert(!parse_manifest("", &m) && !parse_manifest("{", &m) && !parse_manifest("\"version\"", &m));
    assert(!parse_manifest(nullptr, &m));
    std::puts("ota manifest ok");
    return 0;
}
