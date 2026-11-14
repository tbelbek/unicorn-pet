// Previews of the away vignettes from the real core: PPM frames per vignette under argv[1]/<name>/,
// 20 fps for 3 s.   make -C test away    (ffmpeg then writes design/away_<name>.gif)
#include <cstdio>
#include <sys/stat.h>
#include "../core/unicorn.h"

using namespace up;

static u16 fb[SCREEN_W * SCREEN_H];

static void dump(const Game& g, const char* dir, int n) {
    render(g, fb);
    char path[160];
    std::snprintf(path, sizeof path, "%s/f%03d.ppm", dir, n);
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u16 c = fb[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                                (unsigned char)((c & 31) << 3)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

static u32 at(int y, int mo, int d, int h, int mi) {   // local seconds since 2000-01-01 (as test_core.cpp)
    static const int CUM[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    u32 days = 0;
    for (int k = 2000; k < y; k++) days += k % 4 ? 365 : 366;
    days += CUM[mo - 1] + d - 1 + (mo > 2 && y % 4 == 0);
    return days * 86400u + h * 3600u + mi * 60u;
}

int main(int argc, char** argv) {
    const char* root = argc > 1 ? argv[1] : "out/away";
    Game g;
    g.init(2024);
    g.sel = 2;                             // the third egg
    g.input(IN_SELECT, 0, 0);
    for (int i = 0; i < EGG_TAPS; i++) g.input(IN_SELECT, 0, 0);
    for (u32 t = 0; t < HATCH_MS + 100; t += 100) g.tick(100);
    g.s.accessory = ACC_BOW;
    g.play_ms = PLAY_LIMIT_MS;
    while (g.scene != SC_AWAY) g.tick(100);
    static const char* const NAMES[] = {"ball", "seesaw", "bubbles", "rope", "butterfly"};
    struct Shot { const char* suffix; int hour; } shots[] = {{"", 11}, {"_evening", 18}};
    for (int p = 0; p <= AWAY_PICKS; p++)
        for (const Shot& sh : shots) {
            bool night = p == AWAY_PICKS;
            if (night && sh.hour != 11) continue;   // the night one has no evening
            char dir[128];
            std::snprintf(dir, sizeof dir, "%s/%s%s", root, night ? "fireflies" : NAMES[p], night ? "" : sh.suffix);
            mkdir(dir, 0755);
            g.set_wall(at(2026, 7, 1, night ? 22 : sh.hour, 0));
            g.away_pick = (u8)(night ? 0 : p);
            g.screen_on = 1;
            g.idle_ms = 0;
            for (int n = 0; n < 60; n++) {
                if (n == 8) g.input(IN_TOUCH, 184, 200);   // a tap at 0.4 s: hearts, the only reaction
                g.tick(50);
                dump(g, dir, n);
            }
        }
    // her own break: act_<name>_away (the vignette) and act_<name>_yes (SC_LEAVE: she grabs the thing)
    static const char* const ACTS[ACT_COUNT] = {"", "color", "lego", "read", "dolls"};
    for (int act = ACT_COLOR; act < ACT_COUNT; act++) {
        char dir[128];
        std::snprintf(dir, sizeof dir, "%s/act_%s_away", root, ACTS[act]);
        mkdir(dir, 0755);
        g.set_wall(at(2026, 7, 1, 11, 0));
        g.s.away_act = (u8)act;
        g.away_pick = (u8)act;
        g.screen_on = 1;
        g.idle_ms = 0;
        for (int n = 0; n < 60; n++) { g.tick(50); dump(g, dir, n); }
        Game l;                            // fresh, so the yes moment starts at 0
        l.init(2024);
        l.sel = 2;
        l.input(IN_SELECT, 0, 0);
        for (int i = 0; i < EGG_TAPS; i++) l.input(IN_SELECT, 0, 0);
        for (u32 t = 0; t < HATCH_MS + 100; t += 100) l.tick(100);
        l.s.accessory = ACC_BOW;
        l.set_wall(at(2026, 7, 1, 11, 0));
        l.s.away_act = (u8)act;
        l.away_pick = (u8)act;
        l.play_ms = PLAY_LIMIT_MS;
        while (l.scene != SC_LEAVE) l.tick(100);
        std::snprintf(dir, sizeof dir, "%s/act_%s_yes", root, ACTS[act]);
        mkdir(dir, 0755);
        for (int n = 0; n < 60; n++) { l.tick(50); dump(l, dir, n); }
    }
    std::printf("frames in %s\n", root);
    return 0;
}
