// A scripted tour of every feature, from the real core: one PPM per frame in out/tour/, with a
// caption per frame (captions.txt) and its on-screen time (times.txt).
//   make -C test tour    (then tools/make_tour.py turns them into design/tour.gif)
#include <cstdio>
#include "../core/unicorn.h"

using namespace up;

static u16 fb[SCREEN_W * SCREEN_H];
static int frame_no;
static FILE *times, *caps;
static const char* caption = "";

static void dump(const Game& g, int ms = 100) {
    std::fprintf(times, "%d\n", ms);
    std::fprintf(caps, "%s\n", caption);
    render(g, fb);
    char path[64];
    std::snprintf(path, sizeof path, "out/tour/%04d.ppm", frame_no++);
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

// the screen never goes dark in a tour (it would after 20 s without a touch); toy modes still time out
static void play(Game& g, int ms) {
    for (int t = 0; t < ms; t += 100) { if (g.idle_ms > 15000) g.idle_ms = 15000; g.tick(100); dump(g); }
}
static void chapter(const char* c) { caption = c; }

static const u32 DAY = 9803;               // 2026-11-03
static u32 at(int h, int m, int day = 0) { return (DAY + day) * 86400u + h * 3600u + m * 60u; }
static void tap_menu(Game& g, int item) { g.input(IN_TOUCH, 46 + item * MENU_STEP, 400); }
static void tap_row(Game& g, int row) { g.input(IN_TOUCH, 300, SET_Y0 + row * SET_ROW_H + 18); }
static void tap_button(Game& g, int right) { g.input(IN_TOUCH, right ? 280 : 90, (WBTN_Y0 + WBTN_Y1) / 2); }
static void quiet(Game& g) { g.greeting = 0; g.party = 0; g.memory_new = 0; g.petting = 0; g.hint_pet = 0; }
static void full(Game& g) { for (int i = 0; i < NSTATS; i++) g.set_stat(i, 9000); }

int main() {
    times = std::fopen("out/tour/times.txt", "w");
    caps = std::fopen("out/tour/captions.txt", "w");
    Game g;
    g.init(2024);
    g.set_wall(at(11, 0));
    g.s.opts |= OPT_NO_BEDTIME;            // bedtime gets its own chapter

    chapter("Dört yumurtadan birini seç");
    play(g, 1000);
    g.input(IN_TOUCH, 268, 170); play(g, 600);
    g.input(IN_TOUCH, 268, 170); play(g, 500);
    chapter("Dokundukça çatlar, yumurtadan çıkar");
    for (int i = 0; i < EGG_TAPS; i++) { g.input(IN_TOUCH, 184, 200); play(g, 300); }
    play(g, HATCH_MS);
    full(g);
    quiet(g);

    chapter("Önce küçücük bir tay: sev, kalpler uçsun");
    play(g, 800);
    g.input(IN_TOUCH, SCREEN_W / 2 + g.pet_x, 230); play(g, 1200);
    g.input(IN_TOUCH, SCREEN_W / 2 + g.pet_x, 230); play(g, 1000);

    chapter("Besle: atıştırmalığını kendisi seçer");
    tap_menu(g, MENU_FEED); play(g, 900);
    g.input(IN_TOUCH, 183, 200); play(g, ACTION_MS);

    chapter("Yıka: köpükleri patlat, köpükten taç");
    g.set_stat(CLEAN, 800); quiet(g); play(g, 500);
    tap_menu(g, MENU_WASH); play(g, 600);
    g.input(IN_TOUCH, 184, 150); play(g, 300);
    g.input(IN_TOUCH, 104, 190); play(g, 300);
    g.input(IN_TOUCH, 264, 190); play(g, ACTION_MS - 1200);

    chapter("Oyna: oyuncak kutusu");
    tap_menu(g, MENU_PLAY); play(g, 1200);
    chapter("Yıldız yakala");
    g.input(IN_TOUCH, 116, 148);
    for (int t = 0; t < 6000; t += 100) {
        if (t % 700 == 300)
            for (int i = 0; i < GAME_TARGETS; i++)
                if (g.tgt[i].y < SCREEN_H - 40) { g.input(IN_TOUCH, g.tgt[i].x, g.tgt[i].y); break; }
        g.tick(100); dump(g);
    }
    g.tick(GAME_MS); play(g, 1500);
    chapter("Müzik bahçesi: çal, o geri söylesin");
    tap_menu(g, MENU_PLAY); play(g, 300);
    g.input(IN_TOUCH, 252, 148); play(g, 400);
    for (int i = 0; i < 4; i++) { g.input(IN_TOUCH, 64 + (i % 3) * 120, 270); play(g, 300); }
    play(g, 3000);
    g.input(IN_TOUCH, 320, 40); play(g, 200);
    chapter("Ayna: fiyonk, çiçek ya da taç");
    tap_menu(g, MENU_PLAY); play(g, 300);
    g.input(IN_TOUCH, 252, 264); play(g, 500);
    g.input(IN_TOUCH, 24 + 3 * 86 + 39, 390); play(g, 1200);
    g.input(IN_TOUCH, 320, 40); play(g, 200);
    chapter("Bulut: cihazı eğ, tüyleri yakala");
    tap_menu(g, MENU_PLAY); play(g, 300);
    g.input(IN_TOUCH, 116, 264);
    for (int t = 0; t < 6000; t += 100) { g.set_tilt(t < 2500 ? -80 : 80); g.tick(100); dump(g); }
    g.input(IN_TOUCH, 320, 40); play(g, 300);

    chapter("Kelebek komşu uğrar");
    full(g); quiet(g);
    g.visit_due = g.clock_ms + 200; g.visitor = 0;
    play(g, 4200);

    chapter("Gündüz, akşam, gece");
    g.idle_next = g.clock_ms + 99999;
    play(g, 800);
    g.set_wall(at(18, 0)); play(g, 1300);
    g.set_wall(at(22, 0)); play(g, 1300);
    g.set_wall(at(11, 0)); g.idle_next = g.clock_ms + 3000;

    chapter("Birlikte geçen günlerle büyür");
    g.s.care_days = 7; play(g, 300); quiet(g); g.s.milestones = 0;
    play(g, MILESTONE_MS + 600);
    g.s.care_days = 30; play(g, MILESTONE_MS + 600);

    chapter("Zamanla öğrendiği numaralar");
    g.s.care_days = 250; g.s.milestones = 15; g.s.tricks_seen = TRICKS;
    static const int SHOW[] = {IDLE_HUM, IDLE_BOW, IDLE_RAINBOW, IDLE_BUBBLES, IDLE_JUGGLE, IDLE_BALANCE,
                               IDLE_MAGIC, IDLE_DANCE, IDLE_SHOW};
    for (int k : SHOW) {
        quiet(g); g.pet_x = 0; g.vel_x = 0;
        g.idle_act = (u8)k;
        g.idle_end = g.clock_ms + TRICK_MS[k - IDLE_HUM];
        g.idle_next = g.idle_end + 99999;
        for (int t = 0; t < TRICK_MS[k - IDLE_HUM]; t += 100) { g.idle_ms = 0; g.tick(100); g.idle_act = (u8)k; dump(g); }
        g.idle_act = IDLE_NONE; play(g, 300);
    }
    g.idle_next = g.clock_ms + 3000;

    chapter("Anı defteri: her bakım günü bir resim, bir bahçe");
    g.s.care_days = 40; quiet(g);
    g.input(IN_TOUCH, 184, 60); play(g, 900);
    g.input(IN_TOUCH, 70, 30); play(g, 1600);
    g.input(IN_TOUCH, 19, 412); play(g, 1200);
    g.input(IN_TOUCH, 19, 412); play(g, 1200);
    g.input(IN_TOUCH, 320, 44); play(g, 300);

    chapter("Doğum günü: her yıl bir mum");
    { Cal c = g.today(); g.s.hatch_day = (u16)(c.day - 365); g.s.party_year = (u8)(c.year - 2001); }
    quiet(g); play(g, PARTY_MS + 600);
    chapter("Uzun aradan sonra: ÖZLEDİM!");
    g.s.seen_day = (u16)(DAY - 10); play(g, PARTY_MS + 600);

    chapter("Pil göstergesi; şarja takınca şezlongda güneşlenir");
    g.set_battery(64); g.set_plugged(false); g.set_plugged(true); g.set_plugged(true);
    play(g, 3000);
    g.set_wall(at(23, 0)); play(g, 2000);
    g.set_plugged(false); g.set_plugged(false); g.set_wall(at(11, 0)); play(g, 1200);

    chapter("Yatma saati: önce ayakta sallanır...");
    g.s.opts &= ~OPT_NO_BEDTIME; quiet(g);
    g.set_wall(at(19, 40)); play(g, 3000);
    chapter("...bir saat sonra ayakta uyuyakalır");
    g.set_wall(at(20, 35)); play(g, 2500);
    g.input(IN_TOUCH, 184, 220); play(g, 1200);
    g.set_wall(at(7, 1, 1)); play(g, 1500);
    g.s.opts |= OPT_NO_BEDTIME;

    chapter("Uyut: yorgan ve rüyalar");
    g.set_wall(at(15, 0)); g.set_stat(ENERGY, 3000); quiet(g);
    tap_menu(g, MENU_SLEEP); play(g, 1300);
    g.input(IN_TOUCH, 50, 300); play(g, 3800);
    g.dreams[0] = D_RAINBOW; play(g, 3200);
    g.input(IN_TOUCH, 184, 200); play(g, 800);

    chapter("10 dakika dolunca arkadaşlarıyla oynamaya gider");
    full(g); quiet(g);
    g.play_ms = g.play_limit_ms(); play(g, LEAVE_MS + 300);
    play(g, 1500);
    g.input(IN_PARENT, 0, 0); play(g, RETURN_MS + 400);

    chapter("Ebeveyn: BOOT'a 3 kez bas, sonuncuyu tut");
    quiet(g);
    g.s.opts &= ~OPT_NO_BEDTIME;           // daytime: the rule is on but has nothing to do
    g.input(IN_PARENT, 0, 0); play(g, 1500);
    tap_row(g, SET_SLEEP); play(g, 1500);
    tap_row(g, BEDR_BACK); play(g, 300);
    chapter("10 dil: dil satırına her dokunuşta sıradaki");
    for (int l = 1; l < LANG_COUNT; l++) { tap_row(g, SET_LANG); play(g, 900); }
    tap_row(g, SET_LANG); play(g, 500);    // back to Turkish
    chapter("Ebeveyn: BOOT'a 3 kez bas, sonuncuyu tut");
    tap_row(g, SET_CLOCK); play(g, 1200);
    chapter("WiFi: telefonla QR okut, ağı telefonda seç");
    tap_row(g, HUB_WIFI); play(g, 900);
    tap_row(g, WF_ADD); play(g, 1500);
    tap_button(g, 1); play(g, 600);
    g.wifi_req = WREQ_NONE;
    g.wifi_session("Unicorn-A7C2", "kq7m4xtz9p2w"); play(g, 2500);
    g.wifi_status(WST_PHONE, 0); play(g, 1200);
    g.wifi_status(WST_JOIN, "Ev WiFi 2.4"); play(g, 800);
    g.wifi_status(WST_ADDR, 0); play(g, 800);
    g.wifi_status(WST_CLOCK, 0); play(g, 800);
    g.wifi_status(WST_OK, 0); play(g, 2200);
    tap_button(g, 1); g.wifi_req = WREQ_NONE; play(g, 400);
    g.input(IN_TOUCH, 184, SET_Y0 + WF_BACK * SET_ROW_H + 18); play(g, 200);
    tap_row(g, HUB_BACK); play(g, 200);
    g.input(IN_TOUCH, 184, SET_Y0 + SET_EXIT * SET_ROW_H + 18);

    chapter("Unicorn Pet");
    full(g); quiet(g);
    g.idle_act = IDLE_HOP; g.idle_end = g.clock_ms + 600; play(g, 2500);

    std::fclose(times);
    std::fclose(caps);
    std::printf("%d frames\n", frame_no);
    return 0;
}
