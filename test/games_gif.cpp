// Previews of the two tilt games and the toy box from the real core: PPM frames under argv[1]/<name>/, 20 fps,
// played by a scripted tilt.   make -C test games    (ffmpeg then writes design/game_<name>.gif and toybox.png)
#include <cstdio>
#include <sys/stat.h>
#include "../core/unicorn.h"

using namespace up;

static u16 fb[SCREEN_W * SCREEN_H];

static void dump(const Game& g, const char* root, const char* name, int n) {
    render(g, fb);
    char path[200];
    std::snprintf(path, sizeof path, "%s/%s", root, name);
    mkdir(path, 0755);
    std::snprintf(path, sizeof path, "%s/%s/f%03d.ppm", root, name, n);
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

static Game toy_box() {
    Game g;
    g.init(2024);
    g.sel = 2;                             // the third egg
    g.input(IN_SELECT, 0, 0);
    for (int i = 0; i < EGG_TAPS; i++) g.input(IN_SELECT, 0, 0);
    for (u32 t = 0; t < HATCH_MS + 100; t += 100) g.tick(100);
    g.set_wall(86400u * 9500 + 11 * 3600); // some day at 11:00
    g.input(IN_TOUCH, 46 + MENU_STEP, 400);
    g.tick(100);
    return g;
}

static int lean_to(int d, int most) {      // a child's hand: lean towards it, less when nearly there
    int t = d * 3 / 2;
    return t > most ? most : t < -most ? -most : t;
}

int main(int argc, char** argv) {
    const char* root = argc > 1 ? argv[1] : "out/games";
    mkdir(root, 0755);
    Game g = toy_box();
    if (g.scene != SC_TOYS) { std::printf("no toy box\n"); return 1; }
    dump(g, root, "toybox", 0);

    g.input(IN_TOUCH, 184, 264);           // roll the ball: lean towards the hole, ease off before it
    for (int n = 0; n < 110; n++) {
        g.set_tilt(g.hole_rest ? 0 : lean_to(g.hole_x - g.cloud_x - g.roll_v / 3, 70));
        g.tick(50);
        dump(g, root, "marble", n);
    }

    g = toy_box();
    g.input(IN_TOUCH, 300, 264);           // balloon ride: lean towards the next ring (skipping one, to show it drift off)
    for (int n = 0; n < 150; n++) {
        int to = g.cloud_x;
        for (int i = 0; i < FEATHERS; i++)
            if (g.fea[i].state == 1 && i != 2) { to = g.fea[i].x; break; }
        g.set_tilt(lean_to(to - g.cloud_x, 80));
        g.tick(50);
        if (n >= 50) dump(g, root, "balloon", n - 50);
    }
    std::printf("frames in %s\n", root);
    return 0;
}
