// Records a scripted play-through from the real core: one PPM per 100 ms step in out/rec/.
//   make -C test record    (then tools/make_gif.py turns them into design/gameplay.gif)
#include <cstdio>
#include "../core/unicorn.h"

using namespace up;

static u16 fb[SCREEN_W * SCREEN_H];
static int frame_no;

static void dump(const Game& g) {
    render(g, fb);
    char path[64];
    std::snprintf(path, sizeof path, "out/rec/%04d.ppm", frame_no++);
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

static void play(Game& g, int ms) {
    for (int t = 0; t < ms; t += 100) { g.tick(100); dump(g); }
}

int main() {
    Game g;
    g.init(2024);
    play(g, 600);
    for (int i = 0; i < EGG_TAPS; i++) { g.input(IN_TOUCH, 184, 200); play(g, 400); }
    play(g, HATCH_MS);
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000);
    g.idle_next = g.clock_ms + 200;        // start wandering right away for the recording
    play(g, 3500);
    g.input(IN_TOUCH, SCREEN_W / 2 + g.pet_x, 230);           // petting
    play(g, 1500);
    g.set_stat(HUNGER, 2000);
    play(g, 600);
    g.input(IN_TOUCH, 46, 400);                               // feed
    play(g, ACTION_MS + 200);
    g.input(IN_TOUCH, 46 + MENU_STEP, 400);                   // play: star mini-game
    for (int t = 0; t < GAME_MS; t += 100) {
        if (t % 900 == 500) {
            for (int i = 0; i < GAME_TARGETS; i++)
                if (g.tgt[i].y < SCREEN_H - 40) { g.input(IN_TOUCH, g.tgt[i].x, g.tgt[i].y); break; }
        }
        g.tick(100);
        if (t % 200 == 0) dump(g);         // the mini-game is long: keep every other frame
    }
    play(g, CELEBRATE_MS);
    g.set_stat(CLEAN, 800);
    play(g, 800);
    g.input(IN_TOUCH, 46 + 3 * MENU_STEP, 400);               // wash the messes away
    play(g, ACTION_MS + 200);
    g.input(IN_TOUCH, 46 + 2 * MENU_STEP, 400);               // good night
    play(g, 2500);
    std::printf("%d frames\n", frame_no);
    return 0;
}
