// Renders a Game into an RGB565 framebuffer. Layout mirrors design/showcase.png.
#include "unicorn.h"
#include "assets.gen.h"

namespace up {

static u16* FB;
static u16 PAL[UP_PALETTE], NIGHT[UP_PALETTE];
static bool pal_ready;

static constexpr u16 rgb(int r, int g, int b) {
    return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static const u16 BLACK = 0, INK = rgb(0xF4, 0xEE, 0xFF), DIM = rgb(0xA9, 0x9B, 0xD6),
                 SEL = rgb(0x24, 0x1C, 0x40), GROUND1 = rgb(0x1B, 0x15, 0x30), GROUND2 = rgb(0x25, 0x1D, 0x42),
                 NGROUND1 = rgb(0x10, 0x14, 0x2E), NGROUND2 = rgb(0x16, 0x1B, 0x3D), TRACK = rgb(0x2A, 0x24, 0x40),
                 YELLOW = rgb(0xFF, 0xE0, 0x66), PINK = rgb(0xFF, 0x8F, 0xB8), LILAC = rgb(0xC9, 0xB8, 0xFF),
                 MOON = rgb(0xFF, 0xE9, 0xA8), BUBBLE = rgb(0xF4, 0xEE, 0xFF);
static const u16 METER[NSTATS] = {rgb(0xFF, 0x5C, 0x6E), rgb(0xFF, 0x5C, 0x9C), rgb(0xFF, 0xD8, 0x4D),
                                  rgb(0x6F, 0xD6, 0xFF)};
enum { IC_APPLE, IC_HEART, IC_STAR, IC_BUBBLE, IC_MOON, IC_BALL };   // order from export_c.py

static void palettes() {
    if (pal_ready) return;
    for (int i = 0; i < UP_PALETTE; i++) {
        int r = UP_PAL_RGB[i * 3], g = UP_PAL_RGB[i * 3 + 1], b = UP_PAL_RGB[i * 3 + 2];
        PAL[i] = rgb(r, g, b);
        // night: blend 34% toward deep blue, then dim to 90% (matches showcase.night)
        int nr = (r * 66 + 34 * 34) / 100 * 9 / 10, ng = (g * 66 + 44 * 34) / 100 * 9 / 10,
            nb = (b * 66 + 120 * 34) / 100 * 9 / 10;
        NIGHT[i] = rgb(nr, ng, nb);
    }
    pal_ready = true;
}

// ---- primitives ---------------------------------------------------------------------------

static inline void px(int x, int y, u16 c) {
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) FB[y * SCREEN_W + x] = c;
}

static void fill(int x0, int y0, int x1, int y1, u16 c) {          // [x0,x1) x [y0,y1)
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) FB[y * SCREEN_W + x] = c;
}

static void ellipse(int cx, int cy, int rx, int ry, u16 c) {
    for (int y = -ry; y <= ry; y++)
        for (int x = -rx; x <= rx; x++)
            if (x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry) px(cx + x, cy + y, c);
}

static void rrect(int x0, int y0, int x1, int y1, int r, u16 fillc, u16 line, int width) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int dx = x < x0 + r ? x0 + r - x : x > x1 - r ? x - (x1 - r) : 0;
            int dy = y < y0 + r ? y0 + r - y : y > y1 - r ? y - (y1 - r) : 0;
            int d2 = dx * dx + dy * dy;
            if (d2 > r * r) continue;
            int inner = r - width;
            bool edge = x < x0 + width || x > x1 - width || y < y0 + width || y > y1 - width ||
                        (dx && dy && d2 > inner * inner);
            px(x, y, edge ? line : fillc);
        }
}

static void blit(const u8* data, int w, int h, int scale, int x0, int y0, const u16* pal) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u8 i = data[y * w + x];
            if (i) fill(x0 + x * scale, y0 + y * scale, x0 + (x + 1) * scale, y0 + (y + 1) * scale, pal[i]);
        }
}

static void icon(int i, int scale, int x, int y) {
    blit(UP_ICONS + i * UP_ICON_W * UP_ICON_H, UP_ICON_W, UP_ICON_H, scale, x, y, PAL);
}

// ---- text (Press Start 2P, 8 px cells, UTF-8 in) -----------------------------------------

static const char* next_cp(const char* s, u32* cp) {
    u8 c = (u8)*s;
    if (c < 0x80) { *cp = c; return s + 1; }
    if ((c & 0xE0) == 0xC0) { *cp = ((c & 0x1F) << 6) | (s[1] & 0x3F); return s + 2; }
    *cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    return s + 3;
}

static int glyph(u32 cp) {
    for (int i = 0; i < UP_GLYPHS; i++)
        if (UP_FONT_CP[i] == cp) return i;
    return '?' - 32;
}

static int text_w(const char* s, int scale) {
    int n = 0;
    u32 cp;
    while (*s) { s = next_cp(s, &cp); n++; }
    return n * 8 * scale;
}

// align: 0 = left edge at x, 1 = centred on x
static void text(const char* s, int x, int y, int scale, u16 c, int align = 1) {
    if (align == 1) x -= text_w(s, scale) / 2;
    u32 cp;
    while (*s) {
        s = next_cp(s, &cp);
        const u8* g = UP_FONT + glyph(cp) * UP_FONT_H;
        for (int row = 0; row < UP_FONT_H; row++)
            for (int col = 0; col < 8; col++)
                if (g[row] & (0x80 >> col)) fill(x + col * scale, y + row * scale,
                                                 x + (col + 1) * scale, y + (row + 1) * scale, c);
        x += 8 * scale;
    }
}

static void utoa(u32 v, char* out) {
    char tmp[11];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *out++ = tmp[--n];
    *out = 0;
}

// ---- decorations --------------------------------------------------------------------------

static void sparkle(int x, int y, int s, u16 c) {
    for (int i = -2; i <= 2; i++) {
        fill(x + i * s, y, x + i * s + s, y + s, c);
        fill(x, y + i * s, x + s, y + i * s + s, c);
    }
}

static void zz(int x, int y) {
    text("Z", x, y, 3, LILAC, 0);
    text("z", x + 26, y - 20, 2, DIM, 0);
}

static void thought(int x, int y) {
    ellipse(x + 20, y + 20, 20, 20, BUBBLE);
    ellipse(x - 4, y + 42, 6, 6, BUBBLE);
    ellipse(x - 16, y + 56, 3, 3, BUBBLE);
    icon(IC_APPLE, 4, x + 2, y + 4);
}

static void sky() {
    u32 r = 7;
    for (int i = 0; i < 26; i++) {
        r = r * 1103515245u + 12345u;
        int x = 8 + (int)((r >> 8) % (SCREEN_W - 16));
        r = r * 1103515245u + 12345u;
        int y = 96 + (int)((r >> 8) % 204);
        int s = (r >> 20) % 3 == 0 ? 3 : 2;
        static const u16 star[3] = {rgb(0x5A, 0x5F, 0x9E), rgb(0x8C, 0x90, 0xD6), rgb(0xC9, 0xCC, 0xFF)};
        fill(x, y, x + s, y + s, star[(r >> 24) % 3]);
    }
    ellipse(SCREEN_W - 64, 126, 22, 22, MOON);
    ellipse(SCREEN_W - 43, 119, 21, 21, BLACK);
}

// ---- screen parts -------------------------------------------------------------------------

static void status(const Game& g) {
    static const int icons[NSTATS] = {IC_APPLE, IC_HEART, IC_STAR, IC_BUBBLE};
    for (int i = 0; i < NSTATS; i++) {
        int x = 14 + i * 88;
        icon(icons[i], 2, x, 16);
        int pips = (g.s.stat[i] * 5 + MAX_STAT / 2) / MAX_STAT;
        for (int p = 0; p < 5; p++)
            fill(x + 22 + p * 12, 20, x + 22 + p * 12 + 9, 29, p < pips ? METER[i] : TRACK);
    }
}

static void header(const Game& g, const char* sub) {
    text(UP_NAMES[g.s.character], SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);
    text(sub, SCREEN_W / 2, 74, 1, DIM);
}

static void menu(const Game& g) {
    static const int icons[MENU_COUNT] = {IC_APPLE, IC_BALL, IC_MOON, IC_BUBBLE};
    static const char* labels[MENU_COUNT] = {"BESLE", "OYNA", "UYUT", "YIKA"};
    for (int i = 0; i < MENU_COUNT; i++) {
        int cx = 46 + i * MENU_STEP;
        if (i == g.menu)
            rrect(cx - 36, MENU_Y0, cx + 36, MENU_Y1, 12, SEL, PAL[UP_ACCENT[g.s.character]], 2);
        icon(icons[i], 3, cx - UP_ICON_W * 3 / 2, 378);
        text(labels[i], cx, 414, 1, i == g.menu ? INK : DIM);
    }
}

// Side-view states: 0..4 moods standing, then sleep, eat, play, wash (export_c.SIDE_STATES).
static void unicorn_side(const Game& g, int state, bool night) {
    int nx = (int)((g.s.age_s / 120) % 5) - 2, ny = (int)((g.s.age_s / 300) % 3) - 1;   // anti burn-in
    const int sz = UP_SIDE_W * UP_SIDE_H;
    const u8* spr = UP_SIDE + ((g.s.character * UP_SIDE_STATES + state) * 2 + g.frame) * sz;
    ellipse(SCREEN_W / 2 + nx, 341 + ny, 120, 11, night ? NGROUND1 : GROUND1);
    ellipse(SCREEN_W / 2 + nx, 339 + ny, 96, 11, night ? NGROUND2 : GROUND2);
    int sx = (SCREEN_W - UP_SIDE_W * 6) / 2 + 6 + nx, sy = 346 - UP_SIDE_FEET * 6 + ny;
    blit(spr, UP_SIDE_W, UP_SIDE_H, 6, sx, sy, night ? NIGHT : PAL);
}

static void unicorn_front(const Game& g, int mood, int y) {
    const int sz = UP_FRONT_W * UP_FRONT_H;
    const u8* spr = UP_FRONT + ((g.s.character * UP_MOODS + mood) * 2 + g.frame) * sz;
    blit(spr, UP_FRONT_W, UP_FRONT_H, 5, (SCREEN_W - UP_FRONT_W * 5) / 2, y, PAL);
}

static void home_like(const Game& g, int state, const char* sub, bool night) {
    if (night) sky();
    status(g);
    header(g, sub);
    unicorn_side(g, state, night);
    int sx = 70, sy = 94;
    if (state == M_HAPPY) { sparkle(sx + 34, sy + 52, 4, YELLOW); sparkle(sx + 230, sy + 42, 3, PINK); }
    if (state == M_HUNGRY) thought(sx + 196, sy + 6);
    if (state == M_SLEEPY) zz(sx + 220, sy + 32);
    if (state == 5) zz(sx + 220, sy + 110 - 12 * g.frame);     // sleeping
    if (state == 7) { sparkle(sx + 40, sy + 72, 4, YELLOW); sparkle(sx + 210, sy + 132, 3, PINK); }
    menu(g);
}

static void scene_egg(const Game& g) {
    text("YUMURTA", SCREEN_W / 2, 52, 2, YELLOW);
    text("BİR ŞEY KIPIRDIYOR...", SCREEN_W / 2, 74, 1, DIM);
    bool crack = g.taps >= 2 || g.s.egg_ms >= EGG_HATCH_MS * 2 / 3;
    const int sz = UP_EGG_W * UP_EGG_H;
    blit(UP_EGG + ((crack ? 2 : 0) + g.frame) * sz, UP_EGG_W, UP_EGG_H, 8, (SCREEN_W - 160) / 2, 118, PAL);
    ellipse(SCREEN_W / 2, 318, 90, 9, GROUND1);
    text("DOKUN, ÇATLASIN!", SCREEN_W / 2, 360, 2, INK);
    for (int i = 0; i < EGG_TAPS; i++)
        ellipse(SCREEN_W / 2 - 48 + i * 24, 404, 6, 6, i < g.taps ? YELLOW : TRACK);
}

static void scene_hatch(const Game& g) {
    text(UP_NAMES[g.s.character], SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);
    unicorn_front(g, M_HAPPY, 90);
    sparkle(70, 140 + 8 * g.frame, 5, YELLOW);
    sparkle(300, 120 - 8 * g.frame, 4, PINK);
    sparkle(84, 300, 3, LILAC);
    text("MERHABA!", SCREEN_W / 2, 372, 3, YELLOW);
}

static void scene_info(const Game& g) {
    static const char* says[] = {"İYİYİM!", "ÇOK MUTLUYUM!", "KARNIM AÇ!", "UYKUM VAR...", "BENİMLE İLGİLEN!"};
    static const int icons[NSTATS] = {IC_APPLE, IC_HEART, IC_STAR, IC_BUBBLE};
    Mood m = g.mood();
    // sound on/off toggle (hit box SOUND_X0 / SOUND_Y1 in unicorn.h)
    // kept clear of the panel's rounded corner
    rrect(SOUND_X0 + 8, 14, SCREEN_W - 30, 40, 10, SEL, g.s.muted ? TRACK : PAL[UP_ACCENT[g.s.character]], 2);
    text(g.s.muted ? "SES KAPALI" : "SES AÇIK", (SOUND_X0 + 8 + SCREEN_W - 30) / 2, 23, 1,
         g.s.muted ? DIM : INK);
    text(UP_NAMES[g.s.character], SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);
    unicorn_front(g, m, 72);
    text(says[m], SCREEN_W / 2, 326, 2, INK);
    for (int i = 0; i < NSTATS; i++) {
        int y = 352 + i * 22;
        icon(icons[i], 2, 64, y);
        fill(96, y + 3, 304, y + 13, TRACK);
        fill(96, y + 3, 96 + 208 * g.s.stat[i] / MAX_STAT, y + 13, METER[i]);
    }
}

void render(const Game& g, u16* fb) {
    FB = fb;
    palettes();
    fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
    char day[24];
    switch (g.scene) {
    case SC_EGG:   scene_egg(g); break;
    case SC_HATCH: scene_hatch(g); break;
    case SC_INFO:  scene_info(g); break;
    case SC_EAT:   home_like(g, 6, "YEMEK YİYOR", false); break;
    case SC_PLAY:  home_like(g, 7, "OYNUYOR", false); break;
    case SC_WASH:  home_like(g, 8, "BANYO ZAMANI", false); break;
    case SC_SLEEP: home_like(g, 5, "UYUYOR · İYİ GECELER", true); break;
    case SC_REFUSE: home_like(g, M_SLEEPY, "ÇOK YORGUN, OYNAYAMAZ", false); break;
    default: {
        utoa(g.s.age_s / 86400 + 1, day);
        int n = 0;
        while (day[n]) n++;
        const char* tail = ". GÜN";
        for (int i = 0; tail[i]; i++) day[n++] = tail[i];
        day[n] = 0;
        home_like(g, g.mood(), day, false);
    }
    }
}

}  // namespace up
