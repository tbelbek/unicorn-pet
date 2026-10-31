// Records a scripted play-through from the real core: one PPM per 100 ms step in out/rec/.
//   make -C test record    (then tools/make_gif.py turns them into design/gameplay.gif)
#include <cstdio>
#include "../core/unicorn.h"

using namespace up;

static u16 fb[SCREEN_W * SCREEN_H];
static int frame_no;
static FILE* times;                        // out/rec/times.txt: how long each frame stays up

static void dump(const Game& g, int ms = 50) {
    std::fprintf(times, "%d\n", ms);
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

// 50 ms frames: the core simulates in 100 ms steps and render() interpolates in between
static void play(Game& g, int ms) {
    for (int t = 0; t < ms; t += 50) { g.tick(50); dump(g); }
}

int main() {
    times = std::fopen("out/rec/times.txt", "w");
    Game g;
    g.init(2024);
    play(g, 1200);                                            // four eggs to choose from
    g.input(IN_TOUCH, 268, 170); play(g, 700);                // highlight the starry one...
    g.input(IN_TOUCH, 268, 170); play(g, 500);                // ...and choose it
    for (int i = 0; i < EGG_TAPS; i++) { g.input(IN_TOUCH, 184, 200); play(g, 400); }
    play(g, HATCH_MS);
    for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000);
    g.idle_next = g.clock_ms + 200;        // start wandering right away for the recording
    play(g, 3500);
    play(g, 1500);                                            // the hand shows how to pet her
    g.input(IN_TOUCH, SCREEN_W / 2 + g.pet_x, 230);           // petting
    play(g, 1500);
    g.set_stat(HUNGER, 2000);
    play(g, 600);
    g.input(IN_TOUCH, 46, 400);                               // feed: pick a snack
    play(g, 1200);
    g.input(IN_TOUCH, 61 + 122, 200);                         // the strawberry
    play(g, ACTION_MS + 200);
    g.input(IN_TOUCH, 46 + MENU_STEP, 400);                   // Oyna: the toy box
    play(g, 900);
    g.input(IN_TOUCH, 116, 148);                              // the star game
    for (int t = 0; t < GAME_MS; t += 100) {
        if (t % 900 == 500) {
            for (int i = 0; i < GAME_TARGETS; i++)
                if (g.tgt[i].y < SCREEN_H - 40) { g.input(IN_TOUCH, g.tgt[i].x, g.tgt[i].y); break; }
        }
        g.tick(100);
        if (t % 200 == 0) dump(g, 200);    // the mini-game is long: every 200 ms is enough
    }
    play(g, CELEBRATE_MS);
    g.input(IN_TOUCH, 46 + MENU_STEP, 400); play(g, 600);     // toy box again: the music garden
    g.input(IN_TOUCH, 252, 148); play(g, 500);
    for (int i = 0; i < 4; i++) { g.input(IN_TOUCH, 64 + (i % 3) * 120, 270); play(g, 350); }
    play(g, 3200);                                            // ...she sings it back
    g.input(IN_TOUCH, 320, 40); play(g, 300);
    g.input(IN_TOUCH, 46 + MENU_STEP, 400); play(g, 400);     // the mirror: a tiara
    g.input(IN_TOUCH, 252, 264); play(g, 600);
    g.input(IN_TOUCH, 24 + 3 * 86 + 39, 390); play(g, 1200);
    g.input(IN_TOUCH, 320, 40); play(g, 300);
    g.input(IN_TOUCH, 46 + MENU_STEP, 400); play(g, 400);     // tilt-a-cloud
    g.input(IN_TOUCH, 116, 264);
    for (int t = 0; t < 7000; t += 50) { g.set_tilt(t < 3000 ? -80 : 80); g.tick(50); dump(g); }
    g.input(IN_TOUCH, 320, 40); play(g, 300);
    g.set_stat(CLEAN, 800);
    play(g, 800);
    g.input(IN_TOUCH, 46 + 3 * MENU_STEP, 400);               // wash the messes away
    play(g, 700);
    g.input(IN_TOUCH, 184, 150); play(g, 400);                // pop the foam: a foam crown
    g.input(IN_TOUCH, 104, 190); play(g, 300);
    play(g, ACTION_MS - 1200);
    g.input(IN_TOUCH, 46 + 2 * MENU_STEP, 400);               // good night
    play(g, 2500);
    g.input(IN_TOUCH, 184, 200);                              // wake her up again...
    play(g, 800);
    g.play_ms = PLAY_LIMIT_MS;                                // ...after a long play session
    play(g, LEAVE_MS + 300);                                  // "I have things to do!"
    play(g, 1200);                                            // the door with the sign
    g.input(IN_TOUCH, 184, 200);                              // knock knock
    play(g, 1200);
    g.input(IN_PARENT, 0, 0);                                 // a grown-up calls her back
    play(g, RETURN_MS + 300);
    std::fclose(times);
    std::printf("%d frames\n", frame_no);
    return 0;
}
