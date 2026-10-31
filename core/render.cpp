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
enum { IC_APPLE, IC_HEART, IC_STAR, IC_BUBBLE, IC_MOON, IC_BALL, IC_STRAWBERRY, IC_CARROT, IC_HAND,
       IC_NOTE, IC_CLOUD, IC_MIRROR, IC_HOUSE, IC_BOW, IC_FLOWER, IC_TIARA };   // export_c.ICONS

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

// flip: mirror horizontally (the unicorn walking left); sx/sy: separate scales for squash & stretch
static void blit2(const u8* data, int w, int h, int sx, int sy, int x0, int y0, const u16* pal, bool flip) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u8 i = data[y * w + (flip ? w - 1 - x : x)];
            if (i) fill(x0 + x * sx, y0 + y * sy, x0 + (x + 1) * sx, y0 + (y + 1) * sy, pal[i]);
        }
}

static void blit(const u8* data, int w, int h, int scale, int x0, int y0, const u16* pal, bool flip = false) {
    blit2(data, w, h, scale, scale, x0, y0, pal, flip);
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

// ---- smooth motion: render interpolates between 100 ms simulation steps ---------------------

static const short SIN_Q[17] = {0, 25, 50, 74, 98, 121, 142, 162, 181, 198, 213, 226, 237, 245, 251, 255, 256};

static int sin64(u32 ph) {                 // one cycle = 64 phases, result -256..256
    ph &= 63;
    int q = (int)(ph & 15), v;
    switch (ph >> 4) {
    case 0: v = SIN_Q[q]; break;
    case 1: v = SIN_Q[16 - q]; break;
    case 2: v = -SIN_Q[q]; break;
    default: v = -SIN_Q[16 - q]; break;
    }
    return v;
}

static int wave(u32 t, u32 period, int amp) { return amp * sin64((t % period) * 64 / period) / 256; }

// a hop: 0 at both ends, -height in the middle (parabola), p in [0, len]
static int arc(u32 p, u32 len, int height) {
    if (p >= len) return 0;
    return -(int)(4 * (u32)height * p * (len - p) / (len * len));
}

static u32 now(const Game& g) { return g.anim_ms + g.acc_ms; }          // ms into the scene, smooth
static int ahead(int per_step, const Game& g) { return per_step * (int)g.acc_ms / STEP_MS; }

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
    ellipse(64, 126, 22, 22, MOON);        // top left: her Zz and dreams rise on the right
    ellipse(85, 119, 21, 21, BLACK);
}

// Day and evening skies for the home screens (night uses sky()). Kept dim: it is a black AMOLED.
static void backdrop(const Game& g) {
    int ph = g.phase();
    if (ph == PH_NIGHT) { sky(); return; }
    u16 cloud = rgb(0x3A, 0x34, 0x5E), cloud2 = rgb(0x2E, 0x29, 0x4C);
    if (ph == PH_DAY) {
        int cx = SCREEN_W - 62, cy = 128;
        for (int k = 0; k < 8; k++) {      // rays
            static const i8 DX[8] = {0, 1, 1, 1, 0, -1, -1, -1}, DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
            int rx = cx + DX[k] * (DX[k] && DY[k] ? 22 : 30), ry = cy + DY[k] * (DX[k] && DY[k] ? 22 : 30);
            fill(rx - 3, ry - 3, rx + 3, ry + 3, rgb(0xFF, 0xC8, 0x4D));
        }
        ellipse(cx, cy, 18, 18, YELLOW);
    } else {                               // evening: a big low orange sun, a first star
        ellipse(SCREEN_W - 60, 150, 24, 24, rgb(0xFF, 0x9F, 0x43));
        cloud = rgb(0x4A, 0x2E, 0x52);
        cloud2 = rgb(0x3A, 0x26, 0x46);
        sparkle(70, 180, 2, rgb(0xC9, 0xCC, 0xFF));
    }
    ellipse(70, 128, 30, 12, cloud2);      // a soft cloud
    ellipse(56, 122, 16, 12, cloud);
    ellipse(80, 118, 18, 14, cloud);
}

// a speech bubble with pictures instead of words (she cannot read either)
static void icon_bubble(int tail_x, int y, int ic1, int ic2) {
    const int s = 4, iw = UP_ICON_W * s, w = (ic2 >= 0 ? 2 * iw + 12 : iw) + 28;
    int cx = tail_x;
    if (cx - w / 2 < 12) cx = 12 + w / 2;
    if (cx + w / 2 > SCREEN_W - 12) cx = SCREEN_W - 12 - w / 2;
    rrect(cx - w / 2, y, cx + w / 2, y + 48, 14, INK, INK, 1);
    int tx = tail_x < cx - w / 2 + 16 ? cx - w / 2 + 16 : tail_x > cx + w / 2 - 16 ? cx + w / 2 - 16 : tail_x;
    for (int k = 0; k < 10; k++) fill(tx - 10 + k, y + 48 + k, tx - 2, y + 49 + k, INK);
    int x = cx - w / 2 + 14;
    icon(ic1, s, x, y + 8);
    if (ic2 >= 0) icon(ic2, s, x + iw + 12, y + 8);
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

// Battery, top right: four bars (green, yellow, red as it empties) and a bolt while plugged in.
// Hidden while the level is unknown. Nearly empty and unplugged: the outline blinks red.
static void battery_icon(const Game& g, int x0, int y0) {
    if (!g.battery) return;
    int pct = g.battery, bars = (pct + 24) / 25;
    u16 c = pct > 50 ? rgb(0x5E, 0xE0, 0x7A) : pct > 20 ? YELLOW : rgb(0xFF, 0x5C, 0x6E);
    bool blink = pct < 10 && !g.plugged && ((g.clock_ms + g.acc_ms) / 500) & 1;
    u16 edge = blink ? rgb(0xFF, 0x5C, 0x6E) : DIM;
    rrect(x0, y0, x0 + 30, y0 + 16, 4, BLACK, edge, 2);
    fill(x0 + 30, y0 + 5, x0 + 33, y0 + 11, edge);   // the nub
    for (int i = 0; i < bars && i < 4; i++) fill(x0 + 4 + i * 6, y0 + 4, x0 + 8 + i * 6, y0 + 12, c);
    if (g.plugged) {                       // a little lightning bolt over the bars
        static const i8 BX[7] = {9, 7, 5, 9, 7, 9, 7}, BY[7] = {-3, 0, 3, 3, 6, 9, 12};
        for (int i = 0; i < 7; i++) fill(x0 + 4 + BX[i], y0 + BY[i], x0 + 9 + BX[i], y0 + BY[i] + 4, YELLOW);
    }
}

static void header(const Game& g, const char* sub) {
    text(UP_NAMES[g.s.character], SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);
    text(sub, SCREEN_W / 2, 74, 1, DIM);
}

static int arc(u32 p, u32 len, int height);
static int wave(u32 t, u32 period, int amp);

static void menu(const Game& g) {
    static const int icons[MENU_COUNT] = {IC_APPLE, IC_BALL, IC_MOON, IC_BUBBLE};
    static const char* labels[MENU_COUNT] = {"BESLE", "OYNA", "UYUT", "YIKA"};
    u32 t = g.clock_ms + g.acc_ms;
    int rec = g.recommended();
    for (int i = 0; i < MENU_COUNT; i++) {
        int cx = 46 + i * MENU_STEP, scale = 3, dy = 0;
        if (i == g.menu)
            rrect(cx - 36, MENU_Y0, cx + 36, MENU_Y1, 12, SEL, PAL[UP_ACCENT[g.s.character]], 2);
        if (i == g.press_item && (int)(g.press_end - t) > 0) {      // dip, then pop past and settle
            int left = (int)(g.press_end - t);
            scale = left > PRESS_MS * 2 / 3 ? 2 : left > PRESS_MS / 3 ? 4 : 3;
        } else if (i == rec && !g.calm()) {                          // her need gently bounces
            dy = arc(t % 1100, 500, 6);
        }
        icon(icons[i], scale, cx - UP_ICON_W * scale / 2, 378 + 12 - UP_ICON_H * scale / 2 + dy);
        text(labels[i], cx, 414, 1, i == g.menu ? INK : DIM);
    }
}

static void hand_hint(const Game& g, int x, int y) {   // a little hand taps: "touch here"
    if (x > SCREEN_W - 26) x = SCREEN_W - 26;           // always fully on screen
    if (x < 26) x = 26;
    int tap = arc((g.clock_ms + g.acc_ms) % 900, 450, 14);
    icon(IC_HAND, 4, x - UP_ICON_W * 2, y - tap);
}

// Side-view states, in export_c.SIDE_STATES order.
enum { ST_SLEEP = 5, ST_EAT = 6, ST_PLAY = 7, ST_WASH = 8, ST_WALK = 9, ST_BLINK = 10, ST_HOP = 11, ST_WALK2 = 12 };

static const u16 RAINBOW[7] = {rgb(0xFF, 0x5C, 0x8A), rgb(0xFF, 0x9F, 0x43), rgb(0xFF, 0xE0, 0x66),
                               rgb(0x5E, 0xE0, 0x7A), rgb(0x4C, 0xC9, 0xF0), rgb(0x9B, 0x6B, 0xFF),
                               rgb(0xFF, 0x8F, 0xB8)};

static int burn_x(const Game& g) { return (int)((g.s.age_s / 120) % 5) - 2; }   // anti burn-in nudge
static int burn_y(const Game& g) { return (int)((g.s.age_s / 300) % 3) - 1; }

static void ground(const Game& g, bool night) {
    int nx = burn_x(g), ny = burn_y(g);
    ellipse(SCREEN_W / 2 + nx, 341 + ny, 120, 11, night ? NGROUND1 : GROUND1);
    ellipse(SCREEN_W / 2 + nx, 339 + ny, 96, 11, night ? NGROUND2 : GROUND2);
}

// Her side-view scale: a foal is drawn a size smaller. Offsets around her were designed at x6,
// so P() scales them with her; her feet stay on the same ground line.
static int SC = 6;
static int P(int n) { return n * SC / 6; }

// x of the sprite's left edge, following the unicorn as she wanders
static int side_x(const Game& g) {
    return (SCREEN_W - UP_SIDE_W * SC) / 2 + SC + burn_x(g) + g.pet_x + ahead(g.vel_x, g);
}
static int side_y(const Game& g) { return 346 - UP_SIDE_FEET * SC + burn_y(g); }

// squash > 0: wider and lower (landing), < 0: taller and thinner (take-off); feet stay planted
static int squash_now;
static int turn = 1;                       // -1: drawn facing the other way (a twirl or dance)
static int acc_draw = -1;                  // dress-up preview; -1 = what she is wearing

static void unicorn_side(const Game& g, int state, bool night, int frame, int dy) {
    const int sz = UP_SIDE_W * UP_SIDE_H;
    const u8* spr = UP_SIDE + ((g.s.character * UP_SIDE_STATES + state) * 2 + frame) * sz;
    int sx = SC + squash_now, sy = SC - squash_now;
    bool flip = g.facing * turn < 0;
    int x0 = side_x(g) - (sx - SC) * UP_SIDE_W / 2, top = side_y(g) + UP_SIDE_FEET * SC + dy - UP_SIDE_FEET * sy;
    blit2(spr, UP_SIDE_W, UP_SIDE_H, sx, sy, x0, top, night ? NIGHT : PAL, flip);
    int acc = acc_draw >= 0 ? acc_draw : g.s.accessory;
    if (acc > ACC_NONE && acc < ACC_COUNT) {   // the accessory follows her head in every pose
        int ax = UP_HEAD[state][frame][0], ay = UP_HEAD[state][frame][1];
        if (flip) ax = UP_SIDE_W - 1 - ax;
        const int s4 = SC * 2 / 3;         // drawn a bit finer than her own pixels
        blit2(UP_ICONS + (IC_BOW + acc - 1) * UP_ICON_W * UP_ICON_H, UP_ICON_W, UP_ICON_H, s4, s4,
              x0 + ax * sx - UP_ICON_W * s4 / 2, top + ay * sy - UP_ICON_H * s4 + P(8), night ? NIGHT : PAL, flip);
    }
}

// squash & stretch around a hop of length len: stretch on take-off, squash on landing
static int squash_for(u32 p, u32 len) {
    if (p < 80) return -1;
    if (p >= len && p < len + SQUASH_MS) return 1;
    return 0;
}

static void unicorn_front(const Game& g, int mood, int y, int scale = 5) {
    const int sz = UP_FRONT_W * UP_FRONT_H;
    const u8* spr = UP_FRONT + ((g.s.character * UP_MOODS + mood) * 2 + g.frame) * sz;
    blit(spr, UP_FRONT_W, UP_FRONT_H, scale, (SCREEN_W - UP_FRONT_W * scale) / 2,
         y + (UP_FRONT_H * (5 - scale)) / 2, PAL);
}

// A small rainbow swirl: what a unicorn leaves behind when nobody washes her. size 0..4
static void mess(int cx, int base, int size) {
    static const int widths[4] = {8, 6, 4, 2};
    for (int r = 0; r < size && r < 4; r++) {
        int w = widths[r] * 6, y = base - (r + 1) * 7;
        fill(cx - w / 2, y, cx + w / 2, y + 7, RAINBOW[r * 2]);
    }
    if (size >= 3) fill(cx + 3, base - 30, cx + 7, base - 26, INK);          // a glint
}

static void particles(const Game& g) {
    for (int i = 0; i < NPART; i++) {
        const Part& p = g.part[i];
        if (p.kind == P_NONE) continue;
        int x = p.x + ahead(p.vx * 2, g), y = p.y + ahead(p.vy * 2, g);
        switch (p.kind) {
        case P_HEART:    x += wave(now(g) + i * 97, 900, 4); icon(IC_HEART, 3, x - 13, y - 12); break;
        case P_STAR:     icon(IC_STAR, 2, x - 9, y - 8); break;
        case P_NOTE:     x += wave(now(g) + i * 131, 700, 5); icon(IC_NOTE, 3, x - 13, y - 12); break;
        case P_SPARKLE:  sparkle(x, y, 3, p.col & 1 ? YELLOW : PINK); break;
        case P_CRUMB:    fill(x, y, x + 5, y + 5, RAINBOW[p.col % 3]); break;
        case P_CONFETTI: fill(x, y, x + 6, y + 6, RAINBOW[p.col % 7]); break;
        case P_RING: {                     // an expanding outline where a star popped
            int rr = 10 + (6 - p.life) * 6 + ahead(6, g), in = rr - 3;
            for (int yy = -rr; yy <= rr; yy++)
                for (int xx = -rr; xx <= rr; xx++) {
                    int d2 = xx * xx + yy * yy;
                    if (d2 <= rr * rr && d2 > in * in) px(x + xx, y + yy, YELLOW);
                }
            break;
        }
        case P_BUBBLE:                     // a ring, so she shows through it
            for (int yy = -7; yy <= 7; yy++)
                for (int xx = -7; xx <= 7; xx++) {
                    int d2 = xx * xx + yy * yy;
                    if (d2 <= 49 && d2 > 25) px(x + xx, y + yy, rgb(0x6F, 0xD6, 0xFF));
                }
            fill(x - 3, y - 3, x - 1, y - 1, INK);
            break;
        }
    }
}

static void bubble(int tail_x, int y, const char* msg) {   // speech bubble, tail at her mouth
    int w = text_w(msg, 2) + 28;
    int cx = tail_x;                      // keep the whole bubble on screen
    if (cx - w / 2 < 12) cx = 12 + w / 2;
    if (cx + w / 2 > SCREEN_W - 12) cx = SCREEN_W - 12 - w / 2;
    rrect(cx - w / 2, y, cx + w / 2, y + 36, 12, INK, INK, 1);
    int tx = tail_x < cx - w / 2 + 14 ? cx - w / 2 + 14 : tail_x > cx + w / 2 - 14 ? cx + w / 2 - 14 : tail_x;
    for (int r = 0; r < 10; r++) fill(tx - 10 + r, y + 36 + r, tx - 2, y + 37 + r, INK);
    text(msg, cx, y + 10, 2, rgb(0x2B, 0x18, 0x40));
}

static void (*under)(const Game& g, int sx, int sy);   // drawn behind her (the sun lounger)

static void home_like(const Game& g, int state, const char* sub, bool night, int frame = -1, int dy = 0,
                      bool show_menu = true) {
    if (frame < 0) frame = g.frame;
    if (night) sky(); else backdrop(g);
    status(g);
    battery_icon(g, SCREEN_W - 52, 46);
    header(g, sub);
    ground(g, night);
    // messes sit on the floor beside her; during a bath they shrink away
    int n = g.scene == SC_WASH ? g.wash_messes : g.scene == SC_SLEEP ? 0 : g.messes();
    int size = 4;
    if (g.scene == SC_WASH) size = g.scene_ms >= 1600 ? 0 : 4 - (int)(g.scene_ms / 400);
    static const int mx[3] = {58, 312, 98};
    for (int i = 0; i < n; i++) mess(mx[i] + burn_x(g), 344 + burn_y(g), size);
    if (under) under(g, side_x(g), side_y(g) + dy);
    unicorn_side(g, state, night, frame, dy);
    int sx = side_x(g), sy = side_y(g) + dy;
    if (g.stage() >= STAGE_GROWN && state != ST_SLEEP && g.scene != SC_SLEEP) {   // her grown-up marks
        static const i8 FX[3] = {15, 11, 19}, FY[3] = {29, 32, 33};
        static const u16* const FC[3] = {&YELLOW, &PINK, &LILAC};
        int n = g.stage() == STAGE_STORYKEEPER ? 3 : 1;
        for (int k = 0; k < n; k++) {
            int fx = g.facing * turn < 0 ? UP_SIDE_W - 1 - FX[k] : FX[k];
            sparkle(sx + fx * SC, sy + FY[k] * SC, 3, *FC[k]);
        }
    }
    if (state == M_HAPPY) { sparkle(sx + P(34), sy + P(52), 4, YELLOW); sparkle(sx + P(230), sy + P(42), 3, PINK); }
    if (state == M_HUNGRY) thought(sx + P(g.facing < 0 ? 10 : 196), sy + P(6));
    if (state == M_SLEEPY) zz(sx + P(220) + wave(now(g), 2000, 4), sy + P(32) - (int)(now(g) / 40 % 24));
    if (state == ST_SLEEP && !g.dream() && (g.scene != SC_LOUNGE || night)) zz(sx + P(220) + wave(now(g), 2600, 6), sy + P(118) - (int)(now(g) / 45 % 40));
    if (state == ST_EAT) {                 // today's treat in the bowl, eaten bite by bite
        static const int treats[TREAT_COUNT] = {IC_APPLE, IC_STRAWBERRY, IC_CARROT};
        int left = 3 - (int)(g.scene_ms * 3 / ACTION_MS);
        if (left > 0) icon(treats[g.treat], left, sx + 34 * SC + 3 - UP_ICON_W * left / 2,
                           sy + (36 + 4) * SC - UP_ICON_H * left);
    }
    particles(g);
    if (show_menu) menu(g);
}

// ---- eggs: one shell per character, patterned in her mane colours -------------------------

static void cracks(int x0, int y0, int stage);

static void egg_draw(int c, int x0, int y0, int scale, int crack_stage) {
    const u8* e = UP_EGG;
    for (int y = 0; y < UP_EGG_H; y++)
        for (int x = 0; x < UP_EGG_W; x++) {
            u8 i = e[y * UP_EGG_W + x];
            if (!i) continue;
            bool edge = x == 0 || y == 0 || x == UP_EGG_W - 1 || y == UP_EGG_H - 1 || !e[y * UP_EGG_W + x - 1] ||
                        !e[y * UP_EGG_W + x + 1] || !e[(y - 1) * UP_EGG_W + x] || !e[(y + 1) * UP_EGG_W + x];
            u16 col = PAL[i];
            if (!edge) {
                bool mark = false;
                int k = 0;
                switch (c) {
                case 0: mark = (x + y) % 6 == 0; k = (y / 4) % 6; break;                    // rainbow stripes
                case 1: mark = (x * 7 + y * 11) % 17 == 0; k = 5; break;                    // little stars
                case 2: mark = x % 4 == 1 && y % 4 == 1; k = (x + y) % 6; break;            // candy dots
                default: mark = (y + (x % 4 < 2 ? 0 : 1)) % 5 == 0; k = 2 + (y / 5) % 3; break;   // waves
                }
                if (mark) col = PAL[UP_MANE[c][k]];
            }
            fill(x0 + x * scale, y0 + y * scale, x0 + (x + 1) * scale, y0 + (y + 1) * scale, col);
        }
    if (crack_stage > 0) cracks(x0, y0, crack_stage);
}

static void house_button() {                // top-right: back home
    rrect(292, 16, 348, 72, 12, SEL, LILAC, 2);
    icon(IC_HOUSE, 4, 320 - UP_ICON_W * 2, 44 - UP_ICON_H * 2);
}

static void scene_egg_pick(const Game& g) {
    static const short EX[4] = {100, 268, 100, 268}, EY[4] = {170, 170, 310, 310};
    text("YUMURTANI SEÇ", SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < 4; i++) {
        bool on = g.preview && g.sel == i;
        int lift = on && !g.calm() ? -4 + wave(now(g), 900, 3) : 0;
        if (on) rrect(EX[i] - 56, EY[i] - 64, EX[i] + 56, EY[i] + 64, 18, SEL, PAL[UP_ACCENT[i]], 3);
        egg_draw(i, EX[i] - UP_EGG_W * 2, EY[i] - UP_EGG_H * 2 + lift, 4, 0);
    }
    if (g.preview) {                       // the heart: "this one!"
        rrect(SCREEN_W / 2 - 36, 382, SCREEN_W / 2 + 36, 438, 14, SEL, PINK, 2);
        icon(IC_HEART, 5, SCREEN_W / 2 - UP_ICON_W * 5 / 2, 390);
        if (g.idle_ms > EGG_HINT_MS) hand_hint(g, SCREEN_W / 2 + 40, 420);
    } else if (g.idle_ms > EGG_HINT_MS) {
        hand_hint(g, EX[0] + 40, EY[0] + 50);
    }
}

// ---- bath foam -------------------------------------------------------------------------------

static void foam_blob(int cx, int cy, int r, u16 c) {
    ellipse(cx, cy, r, r, c);
    ellipse(cx - r * 2 / 3, cy + r / 3, r * 2 / 3, r * 2 / 3, c);
    ellipse(cx + r * 2 / 3, cy + r / 3, r * 2 / 3, r * 2 / 3, c);
}

static void bath_foam(const Game& g) {
    static const short FX[3] = {104, 184, 264}, FY[3] = {190, 150, 190};
    u16 foam = rgb(0xF0, 0xF8, 0xFF), shade = rgb(0xC8, 0xE8, 0xFF);
    if (g.foam_window())
        for (int i = 0; i < 3; i++) {
            if (g.foam_mask & (1 << i)) continue;
            int bob = g.calm() ? 0 : wave(now(g) + i * 300, 1400, 3);
            foam_blob(FX[i], FY[i] + bob + 3, 22, shade);
            foam_blob(FX[i], FY[i] + bob, 22, foam);
            if (i == g.foam_sel) {         // which one PWR would pop
                for (int a = 0; a < 64; a += 2) px(FX[i] + 34 * sin64(a) / 256, FY[i] + bob + 34 * sin64(a + 16) / 256, LILAC);
            }
        }
    if (g.foam_mask && g.scene_ms < CROWN_TO_MS) {   // the first popped puff became a silly crown
        int ax = UP_HEAD[ST_WASH][g.frame][0], ay = UP_HEAD[ST_WASH][g.frame][1];
        int cx = side_x(g) + ax * SC, cy = side_y(g) + ay * SC - SC;
        foam_blob(cx, cy, 16, foam);
        foam_blob(cx + 14, cy - 10, 10, foam);
    }
}

// ---- butterfly neighbour --------------------------------------------------------------------

static void butterfly(const Game& g) {
    if (!g.visitor) return;
    u32 t = g.clock_ms + g.acc_ms - g.visit_start;
    int x, y, restx = 52, resty = 318;
    if (g.calm() || (t >= 1000 && t < 3000)) { x = restx; y = resty; }
    else if (t < 1000) {                    // flutters in from the top right
        x = 330 + (restx - 330) * (int)t / 1000;
        y = 110 + (resty - 110) * (int)t / 1000 + wave(t, 300, 12);
    } else {                               // and away to the top left
        u32 p = t - 3000;
        x = restx - 60 * (int)p / 1000;
        y = resty - 260 * (int)p / 1000 + wave(p, 300, 10);
    }
    bool resting = t >= 1000 && t < 3000;  // resting: wings mostly open, a slow flap now and then
    bool open = g.calm() || (resting ? (t / 500) % 3 != 0 : ((t / 120) & 1));
    u16 wing = rgb(0xFF, 0xC8, 0x4D), body = rgb(0x2B, 0x18, 0x40);
    int w = open ? 22 : 8;
    u16 spot = PINK;
    for (int k = 0; k < 16; k++) {          // upper wings, then smaller lower wings
        int span = k < 10 ? w - k / 3 : (w * 2 / 3) - (k - 10) / 2;
        fill(x - 3 - span, y - 14 + k * 2, x - 3, y - 12 + k * 2, wing);
        fill(x + 3, y - 14 + k * 2, x + 3 + span, y - 12 + k * 2, wing);
    }
    if (open) { ellipse(x - 12, y - 6, 3, 3, spot); ellipse(x + 12, y - 6, 3, 3, spot); }
    fill(x - 2, y - 16, x + 2, y + 18, body);     // body and feelers
    fill(x - 6, y - 22, x - 4, y - 16, body);
    fill(x + 4, y - 22, x + 6, y - 16, body);
}

// ---- Oyna's toy box and its modes -------------------------------------------------------------

static void scene_toys(const Game& g) {
    static const int icons[TOY_COUNT] = {IC_STAR, IC_NOTE, IC_CLOUD, IC_MIRROR, IC_HOUSE};
    header(g, "NE OYNAYALIM?");
    for (int i = 0; i < TOY_COUNT; i++) {
        int cx = i == TOY_HOME ? SCREEN_W / 2 : 116 + (i % 2) * 136, cy = 148 + (i / 2) * 116;
        rrect(cx - 52, cy - 52, cx + 52, cy + 52, 18, SEL, i == g.sel ? PAL[UP_ACCENT[g.s.character]] : TRACK, 3);
        int scale = 6;
        if (i == g.press_item && (int)(g.press_end - (g.clock_ms + g.acc_ms)) > 0) scale = 7;
        int bob = (i == g.sel && !g.calm()) ? wave(now(g), 1200, 3) : 0;
        icon(icons[i], scale, cx - UP_ICON_W * scale / 2, cy - UP_ICON_H * scale / 2 + bob);
    }
}

static void scene_music(const Game& g) {
    header(g, "MÜZİK BAHÇESİ");
    house_button();
    unicorn_front(g, g.echo_i ? M_HAPPY : M_IDLE, 22, 2);
    static const int petal_k[3] = {0, 2, 4};
    for (int i = 0; i < 3; i++) {
        int cx = 64 + i * 120, cy = 270;
        if (i == g.sel) rrect(cx - 48, cy - 56, cx + 48, cy + 74, 16, BLACK, LILAC, 2);   // under the flower
        u32 since = g.flower_at[i] == 0xFFFF ? 100000 : g.scene_ms - g.flower_at[i];
        int open = since < 100 ? 10 : since < 500 ? 22 : 18;          // closes, bursts open, settles
        if (g.calm()) open = since < 500 ? 20 : 18;
        fill(cx - 3, cy + 10, cx + 3, cy + 60, rgb(0x5E, 0xE0, 0x7A));   // stem
        u16 petal = RAINBOW[petal_k[i]];
        for (int a = 0; a < 64; a += 8)
            ellipse(cx + open * sin64(a) / 256, cy + open * sin64(a + 16) / 256, 11, 11, petal);
        ellipse(cx, cy, 10, 10, YELLOW);
    }
    particles(g);
    // dots for the notes she will sing back
    for (int i = 0; i < g.note_count; i++)
        ellipse(SCREEN_W / 2 - (g.note_count - 1) * 12 + i * 24, 360, 6, 6, RAINBOW[petal_k[g.notes[i]]]);
}

static void scene_cloud(const Game& g) {
    header(g, "TÜYLERİ YAKALA");
    house_button();
    for (int i = 0; i < 3; i++) ellipse(64 + i * 120, CLOUD_Y + 50, 4, 4, TRACK);   // lane guides
    // the cloud, sliding smoothly toward where it is going
    int cx = g.cloud_x;
    u16 white = rgb(0xF4, 0xF8, 0xFF), shade = rgb(0xC9, 0xD8, 0xF0);
    ellipse(cx, CLOUD_Y + 6, 52, 16, shade);
    ellipse(cx - 26, CLOUD_Y, 24, 16, white);
    ellipse(cx + 26, CLOUD_Y, 24, 16, white);
    ellipse(cx, CLOUD_Y - 10, 30, 20, white);
    int stacked = 0;
    for (int i = 0; i < FEATHERS; i++) {
        const Game::Feather& f = g.fea[i];
        int fx = f.x, fy = f.y;
        if (f.state == 0) continue;
        if (f.state == 1) { fy += ahead(FEATHER_FALL, g); fx += g.calm() ? 0 : wave(now(g) + i * 200, 1000, 8); }
        if (f.state == 2) { fx = cx - 30 + stacked * 12; fy = CLOUD_Y - 26; stacked++; }
        if (f.state == 3) fy = 382;
        u16 vane = RAINBOW[i % 7];
        fill(fx - 1, fy - 14, fx + 1, fy + 14, INK);                  // quill
        for (int k = 0; k < 9; k++) fill(fx - 7 + k / 3, fy - 12 + k * 2, fx + 7 - k / 3, fy - 10 + k * 2, vane);
    }
    particles(g);
}

static void scene_dress(const Game& g) {
    static const int icons[ACC_COUNT] = {-1, IC_BOW, IC_FLOWER, IC_TIARA};
    header(g, "AYNA");
    house_button();
    ground(g, false);
    bool happy = g.dress_at && (int)(g.clock_ms - g.dress_at) < 700;
    acc_draw = g.preview;
    unicorn_side(g, happy ? M_HAPPY : M_IDLE, false, 0, 0);
    acc_draw = -1;
    for (int i = 0; i < ACC_COUNT; i++) {
        int x0 = 24 + i * 86;
        u16 line = i == g.s.accessory ? PAL[UP_ACCENT[g.s.character]] : i == g.preview ? LILAC : TRACK;
        rrect(x0, 352, x0 + 78, 430, 14, SEL, line, 3);
        if (icons[i] < 0) {                // "nothing": an empty ring
            for (int a = 0; a < 64; a += 2) px(x0 + 39 + 16 * sin64(a) / 256, 391 + 16 * sin64(a + 16) / 256, DIM);
        } else {
            icon(icons[i], 5, x0 + 39 - UP_ICON_W * 5 / 2, 391 - UP_ICON_H * 5 / 2);
        }
    }
    particles(g);
}

// birthday cake: two tiers, icing, a candle per birthday (up to five), flickering flames
static void cake(const Game& g, int cx, int base) {
    u32 t = g.clock_ms + g.acc_ms;
    ellipse(cx, base, 50, 7, rgb(0xC9, 0xB8, 0xFF));                  // plate
    rrect(cx - 42, base - 40, cx + 42, base - 2, 8, rgb(0xFF, 0x8F, 0xB8), rgb(0xFF, 0x8F, 0xB8), 1);
    rrect(cx - 28, base - 64, cx + 28, base - 40, 8, rgb(0xFF, 0xF0, 0xF6), rgb(0xFF, 0xF0, 0xF6), 1);
    for (int k = 0; k < 7; k++) {          // icing drips and sprinkles
        int x = cx - 39 + k * 12;
        fill(x, base - 40, x + 7, base - 33 - (k % 2) * 5, rgb(0xFF, 0xF0, 0xF6));
        fill(cx - 36 + k * 11, base - 18, cx - 33 + k * 11, base - 15, RAINBOW[k]);
    }
    int n = g.years();
    n = n < 1 ? 1 : n > 5 ? 5 : n;
    for (int i = 0; i < n; i++) {
        int x = cx - (n - 1) * 8 + i * 16;
        fill(x - 2, base - 80, x + 3, base - 64, RAINBOW[(i * 2) % 7]);
        int fl = ((t / 120 + i) & 1) ? 6 : 5;
        ellipse(x, base - 86, 3, fl, rgb(0xFF, 0x9F, 0x43));
        ellipse(x, base - 85, 2, fl - 2, YELLOW);
    }
}

// one picture per day she was cared for, in this order
static const int ALBUM_ICONS[ALBUM_PICS] = {IC_HEART, IC_APPLE, IC_STAR, IC_BUBBLE, IC_MOON, IC_BALL,
                                           IC_STRAWBERRY, IC_NOTE, IC_CLOUD, IC_FLOWER, IC_BOW, IC_TIARA};

// every book's picture for day i of that book: the same twelve, shuffled differently per book
static int album_icon(int book, int i) { return ALBUM_ICONS[(i + book * 5) % ALBUM_PICS]; }

static void scene_home(const Game& g) {
    char day[32] = "BİRLİKTE ";             // days she was cared for, not powered hours
    int n = 0;
    while (day[n]) n++;
    utoa(g.s.care_days ? g.s.care_days : 1, day + n);
    while (day[n]) n++;
    const char* tail = ". GÜN";
    for (int i = 0; tail[i]; i++) day[n++] = tail[i];
    day[n] = 0;
    u32 t = g.clock_ms + g.acc_ms;
    int state = g.mood(), frame = 0, dy = wave(now(g), 2400, 3) + 2;   // slow breathing bob
    squash_now = 0;
    if (g.petting) {                                       // being petted: a happy little hop
        state = M_HAPPY;
        u32 p = t - (g.pet_react_end - PET_REACT_MS);
        dy = arc(p, PET_REACT_MS - SQUASH_MS, 10);
        squash_now = squash_for(p, PET_REACT_MS - SQUASH_MS);
    } else if (g.idle_act == IDLE_WALK) {                  // 4-phase walk cycle
        u32 ph = now(g) / 140 % 4;
        state = ph < 2 ? ST_WALK : ST_WALK2;
        frame = (int)(ph & 1);
        dy = 0;
    } else if (g.idle_act == IDLE_BLINK) {
        state = ST_BLINK;
    } else if (g.idle_act == IDLE_HOP) {
        state = ST_HOP;
        u32 p = t - (g.idle_end - 600);
        dy = arc(p, 600 - SQUASH_MS, 26);
        squash_now = squash_for(p, 600 - SQUASH_MS);
    } else if (g.idle_act == IDLE_HUM) {                   // eyes closed, swaying to her own tune
        state = ST_BLINK;
        dy = wave(now(g), 600, 2) + 2;
    } else if (g.idle_act == IDLE_TWIRL) {                 // little hops, turning each time
        state = ST_HOP;
        u32 q = t - (g.idle_end - TWIRL_MS), p = q % 300;
        dy = arc(p, 300 - 60, 10);
        turn = (q / 300) & 1 ? -1 : 1;
    } else if (g.idle_act == IDLE_DANCE) {                 // two big hops, a turn in between
        state = ST_HOP;
        u32 q = t - (g.idle_end - DANCE_MS), p = q % (DANCE_MS / 2);
        dy = arc(p, DANCE_MS / 2 - SQUASH_MS, 24);
        turn = q >= DANCE_MS / 2 ? -1 : 1;
        squash_now = squash_for(p, DANCE_MS / 2 - SQUASH_MS);
    }
    if (g.greeting && !g.petting) {                        // hello after a break
        u32 p = GREET_MS - (u32)(g.greet_end - t > GREET_MS ? 0 : g.greet_end - t);
        int ph = g.phase();
        if (ph == PH_NIGHT) state = M_SLEEPY;              // a big yawn
        else if (ph == PH_DAY) { state = ST_HOP; dy = -arc(p % 1000, 900, 8); squash_now = p % 1000 < 300 ? -1 : 0; }
        else state = M_HAPPY;
    }
    if (g.party && !g.petting) {                           // a celebration: happy hops
        u32 p = t % 700;
        state = g.party == PARTY_REUNION ? M_HAPPY : ST_HOP;
        dy = arc(p, 700 - SQUASH_MS, g.party == PARTY_MILESTONE ? 24 : 16);
        squash_now = squash_for(p, 700 - SQUASH_MS);
    }
    if (g.calm()) { dy = 0; squash_now = 0; }               // parent setting: calm motion
    home_like(g, state, day, false, frame, dy);
    squash_now = 0;
    turn = 1;
    butterfly(g);
    if (g.memory_new && g.memory_at && g.s.care_days) {   // today's picture floats up to her name
        int p = (int)(t - g.memory_at);
        p = p < 0 ? 0 : p > MEMORY_SHOW_MS ? MEMORY_SHOW_MS : p;
        int x = side_x(g) + P(150), y = 220 - (220 - 56) * p / MEMORY_SHOW_MS;
        x += (SCREEN_W / 2 - x) * p / MEMORY_SHOW_MS;
        int sc = p > MEMORY_SHOW_MS - 300 ? 3 : 5;
        int last = g.s.care_days - 1;
        icon(album_icon(last / ALBUM_PICS, last % ALBUM_PICS), sc, x - UP_ICON_W * sc / 2, y - UP_ICON_H * sc / 2);
        sparkle(x + 30, y - 10, 2, YELLOW);
    }
    if (g.greeting) {                      // "hello! remember our snack?"
        static const int treats[TREAT_COUNT] = {IC_APPLE, IC_STRAWBERRY, IC_CARROT};
        icon_bubble(side_x(g) + P(g.facing < 0 ? 40 : 200), side_y(g) + P(6), IC_HEART,
                    g.s.last_treat ? treats[g.s.last_treat - 1] : -1);
    }
    if (g.party == PARTY_BIRTHDAY) cake(g, g.facing < 0 ? SCREEN_W - 86 : 86, 344);
    if (g.party) {
        static const char* MILE[MILESTONES] = {"BÜYÜDÜM!", "BÜYÜDÜM!", "BÜYÜDÜM!", "1 YIL!"};
        const char* says = g.party == PARTY_REUNION ? "ÖZLEDİM!" : g.party == PARTY_BIRTHDAY ? "İYİ Kİ DOĞDUM!"
                         : MILE[g.party_arg];
        bubble(side_x(g) + P(g.facing < 0 ? 40 : 200), side_y(g) + P(6), says);
    }
    if (g.hint_pet && !g.petting) hand_hint(g, side_x(g) + P(252), side_y(g) + P(156));   // "pet me", on black
}

// SC_SLEEP: a patchwork quilt to tuck her in, then little dreams
static constexpr u16 nightc(int r, int g, int b) {   // same blend as palettes(): night-time colours
    return rgb((r * 66 + 34 * 34) / 100 * 9 / 10, (g * 66 + 44 * 34) / 100 * 9 / 10, (b * 66 + 120 * 34) / 100 * 9 / 10);
}
static const u16 Q_LINE = nightc(0x3A, 0x1E, 0x5A), Q_SHEET = nightc(0xFF, 0xE4, 0xA8),
                 Q_A = nightc(0xFF, 0x8F, 0xB8), Q_B = nightc(0xB7, 0x9C, 0xFF), Q_STITCH = nightc(0xFF, 0xE0, 0x66);

// One quilt pixel. Column u's top edge is top[u] (a dome over her back, or flat when folded); the
// top rows are the turned-down sheet with a scalloped hem, the rest is patchwork with stitches.
static u16 quilt_px(int u, int v, int w, int h, const u8* top) {
    int t = top[u];
    if (v < t) return 0;
    bool edge = u == 0 || u == w - 1 || v == h - 1 || v == t ||
                (u > 0 && v < top[u - 1]) || (u < w - 1 && v < top[u + 1]);
    if ((u == 0 || u == w - 1) && v == h - 1) return 0;            // rounded bottom corners
    if (edge) return Q_LINE;
    if (v <= t + 2) return (v == t + 2 && (u % 4 == 1 || u % 4 == 2)) ? Q_LINE : Q_SHEET;
    int a = u - 1, b = v - 1;
    if (a % 4 == 0 && b % 4 == 0) return Q_STITCH;                  // a little yellow stitch at each seam
    return ((a / 4 + b / 4) & 1) ? Q_B : Q_A;
}

static void quilt(int x0, int y0, int w, int h, const u8* top, int show_w, bool flip, int cell = 6) {
    for (int v = 0; v < h; v++)
        for (int u = 0; u < w && u < show_w; u++) {
            u16 c = quilt_px(u, v, w, h, top);
            int px0 = x0 + (flip ? w - 1 - u : u) * cell;
            if (c) fill(px0, y0 + v * cell, px0 + cell, y0 + v * cell + cell, c);
        }
}

static void upper_half(int cx, int cy, int rx, int ry, u16 c) {   // a filled half-ellipse, flat side down
    for (int y = -ry; y <= 0; y++) {
        int w = rx;
        while (w > 0 && w * w * ry * ry + y * y * rx * rx > rx * rx * ry * ry) w--;
        fill(cx - w, cy + y, cx + w + 1, cy + y + 1, c);
    }
}

static void icon_c(int id, int scale, int cx, int cy) {   // an icon centred on (cx, cy)
    icon(id, scale, cx - UP_ICON_W * scale / 2, cy - UP_ICON_H * scale / 2);
}

// A dream: everything is laid out around the cloud's centre and stays inside a 104 x 60 box.
enum { DREAM_RX = 70, DREAM_RY = 46 };
static void dream_scene(const Game& g, int d, int cx, int cy, u32 t) {
    static const int treats[TREAT_COUNT] = {IC_APPLE, IC_STRAWBERRY, IC_CARROT};
    const u16 CLOUD_C = rgb(0x4C, 0x4F, 0x8A);
    switch (d) {
    case D_SNACK:
        icon_c(g.s.last_treat ? treats[g.s.last_treat - 1] : IC_APPLE, 5, cx, cy + wave(t, 800, 3));
        sparkle(cx + 34, cy - 16, 2, YELLOW);
        break;
    case D_BUBBLES:
        for (int i = 0; i < 4; i++) {
            int bx = cx - 36 + i * 24, by = cy + 20 - (int)((t / 14 + i * 11) % 40), r = 6 + i % 2 * 3;
            ellipse(bx, by, r, r, rgb(0x9F, 0xE6, 0xFF));
            ellipse(bx, by, r - 2, r - 2, CLOUD_C);
            fill(bx - r / 2, by - r / 2, bx - r / 2 + 3, by - r / 2 + 3, INK);   // shine
        }
        break;
    case D_STARS:
        for (int i = 0; i < 3; i++)
            icon_c(IC_STAR, ((t / 250 + i) & 1) ? 3 : 4, cx - 34 + i * 34, cy + (i == 1 ? -8 : 6));
        break;
    case D_MUSIC:
        for (int i = 0; i < 2; i++)
            icon_c(IC_NOTE, 4, cx - 20 + i * 40 + wave(t + i * 300, 900, 3), cy + 14 - (int)((t / 24 + i * 14) % 28));
        break;
    case D_CLOUD: {                        // she rides a little cloud, a rainbow behind it
        int x = cx + 12 + wave(t, 2400, 10), y = cy + wave(t, 1200, 3);
        for (int k = 0; k < 3; k++) fill(x - 46, y - 4 + k * 5, x - 16, y + k * 5, RAINBOW[k * 2]);
        icon_c(IC_CLOUD, 4, x, y);
        break;
    }
    case D_CROWN:
        icon_c(g.s.accessory ? IC_BOW + g.s.accessory - 1 : IC_TIARA, 5, cx, cy);
        if ((t / 300) & 1) sparkle(cx - 34, cy - 14, 2, YELLOW); else sparkle(cx + 34, cy + 10, 2, PINK);
        break;
    case D_HEARTS:
        for (int i = 0; i < 3; i++) icon_c(IC_HEART, 3, cx - 30 + i * 30, cy + 14 - (int)((t / 20 + i * 9) % 28));
        break;
    case D_BUTTERFLY: {
        int x = cx, y = cy + wave(t, 800, 3);   // flutters in place, centred
        bool open = (t / 160) & 1;
        int w = open ? 12 : 5;
        u16 wing = rgb(0xFF, 0xC8, 0x4D), body = rgb(0x2B, 0x18, 0x40);
        ellipse(x - 3 - w, y - 6, w, 10, wing);   // upper wings
        ellipse(x + 3 + w, y - 6, w, 10, wing);
        ellipse(x - 3 - w * 2 / 3, y + 9, w * 2 / 3, 6, wing);   // lower wings
        ellipse(x + 3 + w * 2 / 3, y + 9, w * 2 / 3, 6, wing);
        if (open) { ellipse(x - 13, y - 7, 3, 3, PINK); ellipse(x + 13, y - 7, 3, 3, PINK); }
        fill(x - 2, y - 14, x + 2, y + 15, body);
        fill(x - 6, y - 20, x - 4, y - 14, body);
        fill(x + 4, y - 20, x + 6, y - 14, body);
        break;
    }
    case D_FLOWERS:
        fill(cx - 46, cy + 20, cx + 46, cy + 23, RAINBOW[3]);
        for (int i = 0; i < 3; i++) {
            int x = cx - 30 + i * 30 + wave(t + i * 400, 1800, 2);
            fill(x - 1, cy, x + 2, cy + 20, RAINBOW[3]);
            icon_c(IC_FLOWER, 3, x, cy - 6);
        }
        break;
    case D_RAINBOW:
        for (int k = 0; k < 6; k++) upper_half(cx, cy + 20, 44 - k * 5, 36 - k * 5, RAINBOW[k]);
        upper_half(cx, cy + 20, 18, 11, CLOUD_C);
        sparkle(cx + 40 - (int)(t / 30 % 80), cy - 24, 2, INK);
        break;
    case D_FRIEND: {                       // a friend from the other eggs, smiling
        int fc = (g.s.character + 1 + g.dreams[0] % 3) % UP_CHARS;
        const int sz = UP_FRONT_W * UP_FRONT_H;
        blit(UP_FRONT + ((fc * UP_MOODS + M_HAPPY) * 2 + (t / 600) % 2) * sz, UP_FRONT_W, UP_FRONT_H, 1,
             cx - UP_FRONT_W / 2, cy - UP_FRONT_H / 2 + wave(t, 1000, 2), PAL);
        icon_c(IC_HEART, 2, cx + 38, cy - 12 - (int)(t / 50 % 10));
        break;
    }
    case D_BALL:
        icon_c(IC_BALL, 4, cx, cy + 4 - arc(t % 700, 700, 14));   // bounces in place, centred
        break;
    case D_MOON:
        icon_c(IC_MOON, 5, cx, cy);
        for (int i = 0; i < 3; i++) if (((t / 400) + i) % 3) sparkle(cx - 36 + i * 36, cy - 20 + (i & 1) * 34, 2, MOON);
        break;
    case D_EGG:                            // when she was still an egg
        egg_draw(g.s.character, cx - UP_EGG_W + wave(t, 500, 2), cy - UP_EGG_H, 2, 0);
        break;
    }
}

// The dream cloud: symmetric puffs on a flat-ish base, grown and shrunk by `grow` (0..256).
static void dream_cloud(int cx, int cy, int grow, u16 c) {
    int rx = DREAM_RX * grow / 256, ry = DREAM_RY * grow / 256;
    if (rx < 2 || ry < 2) return;
    ellipse(cx, cy + ry / 5, rx, ry * 3 / 4, c);                 // body
    ellipse(cx - rx * 3 / 5, cy + ry / 8, ry * 9 / 16, ry * 9 / 16, c);   // side puffs
    ellipse(cx + rx * 3 / 5, cy + ry / 8, ry * 9 / 16, ry * 9 / 16, c);
    ellipse(cx - rx / 4, cy - ry / 3, ry * 5 / 8, ry * 5 / 8, c);         // top puffs, mirrored
    ellipse(cx + rx / 4, cy - ry / 3, ry * 5 / 8, ry * 5 / 8, c);
}

static void scene_sleep(const Game& g) {
    int dy = g.calm() ? 2 : wave(now(g), 3200, 2) + 2;
    home_like(g, ST_SLEEP, "UYUYOR · İYİ GECELER", true, 0, dy);
    int sx = side_x(g), sy = side_y(g) + dy;
    bool flip = g.facing < 0;
    if (g.tucked_in()) {                   // up to her chin, pulled up from the tail end
        u32 since = g.scene_ms + g.acc_ms - (g.tucked ? g.tuck_ms : TUCK_MS);
        const int w = 26, u0 = 2;          // sprite columns 2..27, rows 29..43
        u8 top[w];
        for (int u = 0; u < w; u++) { int d = u - w / 2; top[u] = (u8)(1 + d * d / 36); }   // over her back
        int show = since >= TUCK_PULL_MS ? w : 2 + (w - 2) * (int)since / TUCK_PULL_MS;
        quilt(sx + (flip ? UP_SIDE_W - u0 - w : u0) * SC, sy + 29 * SC, w, 15, top, show, flip, SC);
    } else {                               // folded on the floor beside her: tap it (or wait)
        static const u8 flat[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        quilt(14, 288, 12, 7, flat, 12, false);
        if (g.scene_ms > 600) hand_hint(g, 52, 326);
    }
    int d = g.dream();
    if (d) {                               // a dream cloud grows, shows a little scene, and fades
        u32 t = (u32)g.dream_ms() + g.acc_ms;
        int grow = t < 300 ? (int)t * 256 / 300 : t > DREAM_MS - 250 ? (int)(DREAM_MS - t) * 256 / 250 : 256;
        if (grow < 0) grow = 0;
        u16 c = rgb(0x4C, 0x4F, 0x8A);
        // just above her horn (sprite column 34, row 19), then up to a cloud kept on screen
        int hx = sx + (flip ? UP_SIDE_W - 1 - 34 : 34) * SC + SC / 2, hy = sy + 19 * SC;
        int cx = hx + (flip ? 64 : -64), cy = hy - 22 - DREAM_RY;   // up and over her back
        if (cx < DREAM_RX + 60) cx = DREAM_RX + 60;            // clear of the moon
        if (cx > SCREEN_W - DREAM_RX - 8) cx = SCREEN_W - DREAM_RX - 8;
        int bx = cx + (flip ? -30 : 30), by = cy + DREAM_RY * 3 / 4;   // where the trail meets the cloud
        static const u8 R[3] = {3, 5, 7};
        for (int k = 0; k < 3; k++)                            // three dots, growing towards the cloud
            if (grow > k * 80) ellipse(hx + (bx - hx) * (k + 1) / 4, hy + (by - hy) * (k + 1) / 4, R[k], R[k], c);
        dream_cloud(cx, cy, grow, c);
        if (grow == 256) dream_scene(g, d - 1, cx, cy + 4, t);
    }
}

// SC_LOUNGE: plugged in, she lies on a striped sun lounger: sunglasses by day, a sleep mask at night.
static void lounger(const Game& g, int sx, int sy) {
    bool flip = g.facing < 0;
    int y = sy + 43 * SC, x0 = sx - P(6), x1 = sx + 40 * SC;   // the bed, just under her
    if (flip) { x0 = sx; x1 = sx + 40 * SC + P(6); }                 // mirrored: the same span, other end
    u16 wood = rgb(0xB0, 0x78, 0x50), wood_d = rgb(0x8B, 0x5A, 0x3C);
    u16 stripe[2] = {PAL[UP_ACCENT[g.s.character]], rgb(0xF4, 0xEE, 0xFF)};
    for (int x = x0 + P(20); x < x1 - P(10); x += P(70)) fill(x, y + 14, x + 6, 344, wood_d);   // legs
    for (int x = x0; x < x1; x += 16) fill(x, y, x + 16 < x1 ? x + 16 : x1, y + 14, stripe[((x - x0) / 16) & 1]);
    fill(x0, y + 14, x1, y + 18, wood);
    // the backrest at her head end, leaning back, taller than her head
    int dir = flip ? -1 : 1, bx = flip ? x0 + P(40) : x1 - P(40), h = P(120), w = P(48);
    for (int k = 0; k < h; k++) {
        int cx = bx + dir * (k * 4 / 10);
        u16 c = k >= h - 4 ? wood : stripe[(k / 16) & 1];
        fill(cx - w / 2, y - k, cx + w / 2, y - k + 1, c);
        fill(cx - w / 2 - 3, y - k, cx - w / 2, y - k + 1, wood);   // the frame
        fill(cx + w / 2, y - k, cx + w / 2 + 3, y - k + 1, wood);
    }
}

static void scene_lounge(const Game& g) {
    int lift = P(30), dy = -lift + (g.calm() ? 0 : wave(now(g), 3600, 1));
    bool night = g.phase() == PH_NIGHT;
    under = lounger;
    home_like(g, ST_SLEEP, night ? "DİNLENİYOR · ŞARJ OLUYOR" : "GÜNEŞLENİYOR · ŞARJ OLUYOR", night, 0, dy);
    under = 0;
    int sx = side_x(g), sy = side_y(g) + dy;
    bool flip = g.facing < 0;
    int ex = flip ? UP_SIDE_W - 1 - 36 : 31;                          // sunglasses over her closed eye
    int gx = sx + ex * SC, gy = sy + 34 * SC;
    u16 frame = rgb(0x2B, 0x18, 0x40);
    if (night) {                           // a pink sleep mask instead of sunglasses
        rrect(gx - 2, gy - 2, gx + 6 * SC + 2, gy + 3 * SC + 2, 6, PINK, LILAC, 2);
        fill(gx + SC, gy + SC, gx + 5 * SC, gy + SC + 3, rgb(0xC0, 0x4A, 0x80));   // a sleepy line
        int arm = flip ? gx + 6 * SC : gx - 3 * SC;
        fill(arm, gy + 2, arm + 3 * SC, gy + 5, PINK);
        return;
    }
    rrect(gx - 2, gy - 2, gx + 6 * SC + 2, gy + 3 * SC + 2, 6, rgb(0x30, 0x24, 0x58), frame, 2);
    fill(gx + SC, gy + 2, gx + 2 * SC, gy + 4, INK);                 // a glint
    int arm = flip ? gx + 6 * SC : gx - 3 * SC;                      // the arm back to her ear
    fill(arm, gy + 2, arm + 3 * SC, gy + 5, frame);
    int dx = flip ? SCREEN_W - 54 : 32;                              // a cold drink on the grass
    fill(dx, 306, dx + 22, 340, rgb(0xFF, 0x9F, 0x43));
    fill(dx, 306, dx + 22, 312, rgb(0xFF, 0xE0, 0x66));
    fill(dx + 14, 290, dx + 17, 312, PINK);
    ellipse(dx + 22, 308, 6, 6, rgb(0x5E, 0xE0, 0x7A));
}

// SC_FEED: three big snacks to choose from
static void scene_feed(const Game& g) {
    static const int treats[TREAT_COUNT] = {IC_APPLE, IC_STRAWBERRY, IC_CARROT};
    header(g, "NE YEMEK İSTER?");
    // a small, patient timer: if nobody chooses, she takes the highlighted one
    int left = (int)(FEED_PICK_MS - (g.scene_ms < FEED_PICK_MS ? g.scene_ms : FEED_PICK_MS));
    fill(SCREEN_W / 2 - 100, 96, SCREEN_W / 2 - 100 + 200 * left / FEED_PICK_MS, 100, TRACK);
    for (int i = 0; i < TREAT_COUNT; i++) {
        int cx = SCREEN_W / 6 + i * SCREEN_W / 3, bob = g.calm() ? 0 : wave(now(g) + i * 400, 1600, 4);
        if (i == g.sel) rrect(cx - 54, 116, cx + 54, 290, 18, SEL, PAL[UP_ACCENT[g.s.character]], 3);
        int scale = 8;
        if (i == g.press_item && (int)(g.press_end - (g.clock_ms + g.acc_ms)) > 0) scale = 9;
        icon(treats[i], scale, cx - UP_ICON_W * scale / 2, 203 - UP_ICON_H * scale / 2 + bob);
    }
    // she looks at the food (front face, small)
    const int sz = UP_FRONT_W * UP_FRONT_H;
    blit(UP_FRONT + ((g.s.character * UP_MOODS + M_HAPPY) * 2 + g.frame) * sz, UP_FRONT_W, UP_FRONT_H, 3,
         (SCREEN_W - UP_FRONT_W * 3) / 2, 296, PAL);
    if (g.scene_ms > 3000) hand_hint(g, SCREEN_W / 6 + g.sel * SCREEN_W / 3 + 30, 250);   // "pick one"
}

// SC_REFUSE: too tired to play, and a big pillow to tap
static void scene_refuse(const Game& g) {
    home_like(g, M_SLEEPY, "ÇOK YORGUN: UYUSUN MU?", false, 0, g.calm() ? 0 : wave(now(g), 2400, 3) + 2, false);
    int pulse = g.calm() ? 0 : wave(now(g), 1200, 3);
    rrect(40 - pulse, 360 - pulse, 328 + pulse, 436 + pulse, 18, rgb(0x2A, 0x24, 0x5E), LILAC, 3);
    icon(IC_MOON, 5, SCREEN_W / 2 - UP_ICON_W * 5 / 2 - 40, 378);
    text("UYU", SCREEN_W / 2 + 40, 390, 2, INK);
}

// SC_SETTINGS: for grown-ups (BOOT held 5 s). Every value cycles on tap / PWR.
// "07" style two-digit number into out (no libc)
static void two(int v, char* out) { out[0] = (char)('0' + v / 10 % 10); out[1] = (char)('0' + v % 10); out[2] = 0; }
static void cat(char* dst, const char* src) { while (*dst) dst++; while ((*dst++ = *src++)) {} }

static void settings_row(const Game& g, int i, const char* name, const char* val, bool accent) {
    int y = SET_Y0 + i * SET_ROW_H;
    if (i == g.set_row) rrect(16, y, SCREEN_W - 16, y + SET_ROW_H - 4, 10, SEL, LILAC, 2);
    text(name, 32, y + 9, 2, accent ? YELLOW : INK, 0);
    if (*val) text(val, SCREEN_W - 32 - text_w(val, 1), y + 13, 1, DIM, 0);
}

// SC_SETTINGS page 1: the date and time, and her birthday. Left half of a row: -1, right half: +1.
static void scene_clock(const Game& g) {
    static const char* names[CLK_ROWS] = {"YIL", "AY", "GÜN", "SAAT", "DAKİKA", "DOĞUM AYI", "DOĞUM GÜNÜ",
                                          "KAYDET", "GERİ"};
    text("SAAT VE DOĞUM GÜNÜ", SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < CLK_ROWS; i++) {
        char v[24] = "";
        int n = -1;
        switch (i) {
        case CLK_YEAR: n = g.ed_year; break;
        case CLK_MONTH: n = g.ed_month; break;
        case CLK_DAY: n = g.ed_day; break;
        case CLK_HOUR: n = g.ed_hour; break;
        case CLK_MIN: n = g.ed_min; break;
        case CLK_BMONTH: n = g.s.hatched ? g.ed_bmonth : -2; break;
        case CLK_BDAY: n = g.s.hatched ? g.ed_bday : -2; break;
        }
        if (n >= 0) {                      // "-  2026  +"
            char num[8];
            if (n >= 100) utoa((u32)n, num); else two(n, num);
            cat(v, "-  "); cat(v, num); cat(v, "  +");
        } else if (n == -2) {
            cat(v, "YUMURTADAN SONRA");
        }
        settings_row(g, i, names[i], v, i == CLK_SAVE);
    }
    text("BOOT: SATIR · PWR: +1", SCREEN_W / 2, 420, 1, DIM);
}

// SC_SETTINGS: for grown-ups (BOOT held 5 s). Every value cycles on tap / PWR.
static void scene_settings(const Game& g) {
    if (g.set_page) { scene_clock(g); return; }
    static const char* names[SET_ROWS] = {"SES", "OTURUM", "MOLA", "PARLAKLIK", "SAKİN MOD", "SALLAMA", "SAAT",
                                          "ÇIKIŞ"};
    static const char* vols[3] = {"NORMAL", "KISIK", "KAPALI"};
    text("EBEVEYN AYARLARI", SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < SET_ROWS; i++) {
        char v[32] = "";
        switch (i) {
        case SET_VOLUME: cat(v, vols[g.volume()]); break;
        case SET_LIMIT:  utoa(g.play_limit_ms() / 60000, v); cat(v, " DK"); break;
        case SET_BREAK:  utoa(g.break_ms() / 60000, v); cat(v, " DK"); break;
        case SET_BRIGHT: utoa(g.brightness() * 100 / 255, v); cat(v, "%"); break;
        case SET_CALM:   cat(v, g.calm() ? "AÇIK" : "KAPALI"); break;
        case SET_SHAKE:  cat(v, (g.s.opts & OPT_NO_SHAKE) ? "KAPALI" : "AÇIK"); break;
        case SET_CLOCK: {                  // "31.10.2026 14:05 >" or a nudge to set it
            Cal c = g.today();
            if (!c.known) { cat(v, "AYARLA >"); break; }
            char t[8];
            two(c.mday, t); cat(v, t); cat(v, ".");
            two(c.month, t); cat(v, t); cat(v, ".");
            utoa(c.year, t); cat(v, t); cat(v, " ");
            two(c.minute / 60, t); cat(v, t); cat(v, ":");
            two(c.minute % 60, t); cat(v, t); cat(v, " >");
            break;
        }
        }
        settings_row(g, i, names[i], v, i == SET_EXIT || (i == SET_CLOCK && !g.today().known));
    }
    char v[8], line[40] = "PİL ";
    if (g.battery) { utoa(g.battery, v); cat(line, v); cat(line, "%"); } else cat(line, "?");
    text(line, SCREEN_W / 2, 392, 1, DIM);
    text("BOOT: SEÇ · PWR: DEĞİŞTİR", SCREEN_W / 2, 412, 1, DIM);
}

// LEAVE / AWAY / RETURN: she goes out to play with friends and hangs a sign on the door.
static void walker(const Game& g, const char* sub, const char* says) {
    bool walking = g.vel_x != 0;
    u32 ph = now(g) / 120 % 4;
    int state = walking ? (ph < 2 ? ST_WALK : ST_WALK2) : M_HAPPY;
    home_like(g, state, sub, false, walking ? (int)(ph & 1) : 0, 0, false);
    if (g.scene == SC_LEAVE) {             // a friend comes to pick her up (drawn smaller: further back)
        int fc = (g.s.character + 1) % UP_CHARS, t = (int)now(g), arrive = 1200 + FRIEND_MS;
        int fx;
        if (t < 1200) fx = SCREEN_W + 10;
        else if (t < arrive) {             // ease-out walk in from the right
            int p = (t - 1200) * 256 / FRIEND_MS, e = 256 - (256 - p) * (256 - p) / 256;
            fx = SCREEN_W + 10 - (SCREEN_W + 10 - 238) * e / 256;
        } else fx = 238 + (t - arrive) * 120 / 1000;
        bool leaving = t >= arrive, fwalk = t >= 1200 && (t < arrive - 300 || leaving);
        const int sz = UP_SIDE_W * UP_SIDE_H;
        u32 fph = now(g) / 120 % 4;
        int fstate = fwalk ? (fph < 2 ? ST_WALK : ST_WALK2) : M_HAPPY;
        const u8* spr = UP_SIDE + ((fc * UP_SIDE_STATES + fstate) * 2 + (fwalk ? (int)(fph & 1) : 0)) * sz;
        blit(spr, UP_SIDE_W, UP_SIDE_H, 4, fx - UP_SIDE_W * 2, 346 - UP_SIDE_FEET * 4 + burn_y(g), PAL, !leaving);
        if (t >= arrive - 400 && t < arrive + 600) bubble(fx, 150, "HADİ!");
    }
    if (says && side_x(g) < SCREEN_W - 120 && now(g) < 1200) bubble(side_x(g) + P(200), side_y(g) - P(6), says);
}

static void scene_away(const Game& g) {
    static const u16 WOOD = rgb(0x8B, 0x5A, 0x3C), WOOD2 = rgb(0x74, 0x48, 0x30), FRAME = rgb(0x4A, 0x30, 0x22);
    int shake = g.wobbling ? wave(g.clock_ms + g.acc_ms, 120, 3) : 0;
    text(UP_NAMES[g.s.character], SCREEN_W / 2, 26, 2, PAL[UP_ACCENT[g.s.character]]);
    text("ARKADAŞLARIYLA OYNUYOR", SCREEN_W / 2, 48, 1, DIM);
    ellipse(SCREEN_W / 2, 352, 130, 10, GROUND1);
    int x0 = 108 + shake;
    fill(x0 - 8, 76, x0 + 160, 352, FRAME);
    fill(x0, 84, x0 + 152, 352, WOOD);
    fill(x0 + 14, 98, x0 + 138, 196, WOOD2);
    fill(x0 + 14, 212, x0 + 138, 338, WOOD2);
    fill(x0 + 18, 102, x0 + 134, 192, WOOD);
    fill(x0 + 18, 216, x0 + 134, 334, WOOD);
    ellipse(x0 + 130, 206, 7, 7, YELLOW);
    // the sign hangs from the knob and swings when someone knocks
    int sw = g.wobbling ? wave(g.clock_ms + g.acc_ms, 260, 10) : 0;
    int kx = x0 + 130, ky = 206, sx = kx - 4 + sw, sy = 228;
    for (int i = 0; i <= 60; i++) {                       // two strings to the knob
        fill(kx + (sx - 50 - kx) * i / 60, ky + (sy - ky) * i / 60, kx + (sx - 50 - kx) * i / 60 + 2,
             ky + (sy - ky) * i / 60 + 2, INK);
        fill(kx + (sx + 50 - kx) * i / 60, ky + (sy - ky) * i / 60, kx + (sx + 50 - kx) * i / 60 + 2,
             ky + (sy - ky) * i / 60 + 2, INK);
    }
    rrect(sx - 60, sy, sx + 60, sy + 64, 12, rgb(0xFF, 0xC2, 0xE0), rgb(0x8A, 0x1F, 0x5C), 3);
    text("RAHATSIZ", sx, sy + 14, 1, rgb(0x3A, 0x0F, 0x2B));
    text("ETMEYİN", sx, sy + 30, 1, rgb(0x3A, 0x0F, 0x2B));
    icon(IC_MOON, 1, sx - 4, sy + 46);
    // a calm picture of the friends playing: no countdown to keep checking (time is for grown-ups)
    for (int i = 0; i < 3; i++) {
        int fc = (g.s.character + 1 + i) % UP_CHARS;
        const int sz = UP_FRONT_W * UP_FRONT_H;
        int hop = g.calm() ? 0 : arc((now(g) + i * 500) % 1500, 600, 6);
        blit(UP_FRONT + ((fc * UP_MOODS + M_HAPPY) * 2) * sz, UP_FRONT_W, UP_FRONT_H, 1,
             SCREEN_W / 2 - 100 + i * 76, 362 + hop, PAL);
    }
    text("SEN DE DİNLEN!", SCREEN_W / 2, 422, 1, DIM);
}

static void scene_game(const Game& g) {
    char line[24];
    text("YILDIZ YAKALA!", SCREEN_W / 2, 18, 2, YELLOW);
    // rainbow time bar shrinking from both sides
    int left = (int)(GAME_MS - (g.scene_ms < GAME_MS ? g.scene_ms : GAME_MS));
    int half = 150 * left / GAME_MS;
    for (int x = SCREEN_W / 2 - half; x < SCREEN_W / 2 + half; x += 6)
        fill(x, 44, x + 6, 52, RAINBOW[((x + 2000) / 6) % 7]);
    icon(IC_STAR, 3, 132, 62);
    utoa(g.score, line);
    text(line, 172, 64, 3, INK, 0);
    // a small unicorn cheering at the bottom, behind the stars so they always stay tappable
    const int sz = UP_SIDE_W * UP_SIDE_H;
    const u8* spr = UP_SIDE + ((g.s.character * UP_SIDE_STATES + ST_HOP) * 2 + g.frame) * sz;
    blit(spr, UP_SIDE_W, UP_SIDE_H, 3, SCREEN_W / 2 - UP_SIDE_W * 3 / 2,
         SCREEN_H - UP_SIDE_H * 3 - 4 + (g.calm() ? 0 : arc(now(g) % 600, 600, 18)), PAL);
    for (int i = 0; i < GAME_TARGETS; i++) {
        const Target& t = g.tgt[i];
        int ty = t.y + ahead(t.vy, g), tx = t.x + wave(now(g) + i * 300, 1600, 3);   // drifting up
        if (!t.alive || ty > SCREEN_H + 30) continue;
        if (i == g.sel) rrect(tx - 34, ty - 32, tx + 34, ty + 32, 14, BLACK, LILAC, 2);   // buttons
        icon(IC_STAR, 6, tx - UP_ICON_W * 3, ty - UP_ICON_H * 3);
    }
    particles(g);
}

// Each crack stage adds one zigzag line on the shell (egg grid units, scale 8).
static void cracks(int x0, int y0, int stage) {
    static const signed char lines[4][8] = {
        {5, 10, 7, 9, 8, 11, -1, -1},
        {8, 11, 10, 10, 12, 12, -1, -1},
        {12, 12, 14, 10, 15, 12, -1, -1},
        {4, 13, 6, 15, 9, 14, 11, 16},
    };
    for (int s = 0; s < stage && s < 4; s++)
        for (int k = 0; k + 3 < 8 && lines[s][k + 2] >= 0; k += 2) {     // join the zigzag points
            int ax = lines[s][k], ay = lines[s][k + 1], bx = lines[s][k + 2], by = lines[s][k + 3];
            int n = iabs(bx - ax) > iabs(by - ay) ? iabs(bx - ax) : iabs(by - ay);
            for (int i = 0; i <= n; i++) {
                int cx = ax + (bx - ax) * i / (n ? n : 1), cy = ay + (by - ay) * i / (n ? n : 1);
                fill(x0 + cx * 8, y0 + cy * 8, x0 + cx * 8 + 8, y0 + cy * 8 + 8, rgb(0x7A, 0x4E, 0x9E));
            }
        }
}

static void scene_egg(const Game& g) {
    text("YUMURTA", SCREEN_W / 2, 52, 2, YELLOW);
    text("BİR ŞEY KIPIRDIYOR...", SCREEN_W / 2, 74, 1, DIM);
    int wob = 0;
    u32 t = g.clock_ms + g.acc_ms;
    if (g.wobbling) {                      // a damped wobble after each tap
        u32 p = t - (g.wobble_end - WOBBLE_MS);
        if (p < WOBBLE_MS) wob = wave(p, 130, 9) * (int)(WOBBLE_MS - p) / WOBBLE_MS;
    } else if (g.s.egg_ms > EGG_HATCH_MS * 2 / 3) {
        wob = wave(t, 900, 3);             // restless near the end
    }
    int x0 = (SCREEN_W - 160) / 2 + wob, y0 = 118;
    int stage = g.taps;
    if (g.s.egg_ms > EGG_HATCH_MS / 3 && stage < 1) stage = 1;
    if (g.s.egg_ms > EGG_HATCH_MS * 2 / 3 && stage < 2) stage = 2;
    egg_draw(g.s.character, x0, y0, 8, stage);
    ellipse(SCREEN_W / 2, 318, 90, 9, GROUND1);
    text("DOKUN, ÇATLASIN!", SCREEN_W / 2, 360, 2, INK);
    if (g.taps == 0) hand_hint(g, SCREEN_W / 2 + 70, 250);     // wordless: tap here
    for (int i = 0; i < EGG_TAPS; i++)
        ellipse(SCREEN_W / 2 - 48 + i * 24, 404, 6, 6, i < g.taps ? YELLOW : TRACK);
}

static void scene_hatch(const Game& g) {
    text(UP_NAMES[g.s.character], SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);
    u32 t = g.scene_ms;
    if (t < 700) {                         // the shell splits: top flies up, bottom settles
        int lift = (int)(t * 90 / 700), drop = (int)(t * 20 / 700);
        const u8* egg = UP_EGG;
        for (int y = 0; y < UP_EGG_H; y++) {
            int dy = y < 12 ? -lift : drop;
            blit(egg + y * UP_EGG_W, UP_EGG_W, 1, 8, (SCREEN_W - 160) / 2 + (y < 12 ? -lift / 3 : 0),
                 118 + y * 8 + dy, PAL);
        }
    } else {                               // and out she pops, growing in
        particles(g);                      // confetti behind her, not on her face
        int scale = t < 820 ? 3 : t < 940 ? 4 : 5;
        int bounce = t < 940 ? 0 : arc(t - 940, 360, 14);
        unicorn_front(g, M_HAPPY, 90 + bounce, scale);
        sparkle(70, 140 + 8 * g.frame, 5, YELLOW);
        sparkle(300, 120 - 8 * g.frame, 4, PINK);
        sparkle(84, 300, 3, LILAC);
        if (t >= 940) text("MERHABA!", SCREEN_W / 2, 372 + arc(t - 940, 360, 8), 3, YELLOW);
        return;
    }
    particles(g);
}


static void book(int x0, int y0, u16 edge) {   // a little open book, 64 x 28
    rrect(x0, y0, x0 + 64, y0 + 28, 6, edge, edge, 1);
    fill(x0 + 4, y0 + 3, x0 + 30, y0 + 25, INK);
    fill(x0 + 34, y0 + 3, x0 + 60, y0 + 25, INK);
    for (int k = 0; k < 3; k++) { fill(x0 + 8, y0 + 8 + k * 6, x0 + 26, y0 + 10 + k * 6, DIM); fill(x0 + 38, y0 + 8 + k * 6, x0 + 56, y0 + 10 + k * 6, DIM); }
}


// four imaginary garden kinds, one after another every three books: flowers, hearts, autumn, stars
static void plant(int cx, int base, int stage, u32 t, bool calm, int style) {
    u16 green = style == 2 ? rgb(0xC9, 0x8A, 0x3C) : RAINBOW[3];
    int sway = calm ? 0 : wave(t + cx * 7, 2600, 2);
    if (stage == 1) ellipse(cx, base - 4, 8, 5, green);                    // a seed peeking out
    if (stage >= 2) {
        int h = stage == 2 ? 26 : stage == 3 ? 44 : 52;
        fill(cx - 2 + sway, base - h, cx + 2 + sway, base, green);
        ellipse(cx - 9 + sway, base - h / 2, 8, 4, green);                  // two leaves
        ellipse(cx + 9 + sway, base - h / 2 - 6, 8, 4, green);
        static const u16 BUD[4] = {PINK, rgb(0xFF, 0x5C, 0x9C), rgb(0xFF, 0x9F, 0x43), YELLOW};
        if (stage == 3) ellipse(cx + sway, base - h - 6, 7, 9, BUD[style]);   // a bud
        if (stage == 4) {
            if (style == 2) {              // autumn: a round crown of orange and red leaves
                ellipse(cx + sway, base - h - 12, 18, 14, rgb(0xFF, 0x9F, 0x43));
                ellipse(cx + sway - 8, base - h - 16, 8, 7, rgb(0xFF, 0x5C, 0x6E));
                ellipse(cx + sway + 9, base - h - 8, 7, 6, YELLOW);
            } else {
                static const int BLOOM[4] = {IC_FLOWER, IC_HEART, IC_FLOWER, IC_STAR};
                icon(BLOOM[style], 5, cx + sway - UP_ICON_W * 5 / 2, base - h - UP_ICON_H * 5 + 8);
            }
        }
    }
}

// SC_ALBUM: a picture for every day she was cared for, and the garden those days grew
static void scene_album(const Game& g) {
    char title[24] = "ANILARIMIZ";
    if (g.books() > 1) {                   // "ANILARIMIZ 3": which book this is
        int k = 10;
        title[k++] = ' ';
        utoa(g.book + 1, title + k);
    }
    text(title, 32, 30, 2, YELLOW, 0);
    house_button();
    int n = g.album();
    u16 edge = g.book ? RAINBOW[g.book % 7] : PAL[UP_ACCENT[g.s.character]];
    for (int i = 0; i < ALBUM_PICS; i++) {
        int x0 = 12 + (i % 4) * 86, y0 = 82 + (i / 4) * 76;
        if (i < n) {
            rrect(x0, y0, x0 + 80, y0 + 68, 12, SEL, edge, 2);
            icon(album_icon(g.book, i), 6, x0 + 40 - UP_ICON_W * 3, y0 + 34 - UP_ICON_H * 3);
        } else {
            rrect(x0, y0, x0 + 80, y0 + 68, 12, BLACK, TRACK, 2);   // blank paper, never a locked shape
        }
    }
    u16 soil = rgb(0x5A, 0x3A, 0x2A), pot = rgb(0x8B, 0x5A, 0x3C);
    rrect(40, 400, 328, 436, 10, pot, rgb(0xB0, 0x78, 0x50), 2);
    fill(48, 396, 320, 404, soil);
    for (int p = 0; p < 3; p++) plant(104 + p * 80, 398, g.garden(p), now(g), g.calm(), g.book / 3 % 4);
    for (int side = -1; side <= 1; side += 2) {   // leaf to the older or newer book
        int b = g.book + side;
        if (b < 0 || b >= g.books()) continue;
        int cx = side < 0 ? 19 : SCREEN_W - 19;
        for (int k = 0; k < 14; k++) {
            int half = side < 0 ? k : 13 - k;
            fill(cx - 7 + k, 412 - half, cx - 6 + k, 413 + half, LILAC);
        }
    }
    particles(g);
}

static void scene_info(const Game& g) {
    static const char* says[] = {"İYİYİM!", "ÇOK MUTLUYUM!", "KARNIM AÇ!", "UYKUM VAR...", "SARILALIM MI?"};
    static const int icons[NSTATS] = {IC_APPLE, IC_HEART, IC_STAR, IC_BUBBLE};
    Mood m = g.mood();
    // sound on/off toggle (hit box SOUND_X0 / SOUND_Y1 in unicorn.h), clear of the rounded corner
    bool off = g.volume() == VOL_OFF;
    rrect(SOUND_X0 + 8, 14, SCREEN_W - 30, 40, 10, SEL, off ? TRACK : PAL[UP_ACCENT[g.s.character]], 2);
    text(off ? "SES KAPALI" : "SES AÇIK", (SOUND_X0 + 8 + SCREEN_W - 30) / 2, 23, 1, off ? DIM : INK);
    int lift = g.album_unseen && !g.calm() ? wave(now(g), 900, 3) : 0;   // something new inside
    book(36, 14 + lift, g.album_unseen ? YELLOW : TRACK);
    if (g.album_unseen && g.scene_ms > 1500) hand_hint(g, 84, 54);
    text(UP_NAMES[g.s.character], SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);
    battery_icon(g, SCREEN_W - 52, 54);
    unicorn_front(g, m, 72);
    static const int STARS[4] = {0, 1, 2, 4};               // grown-up stars around her portrait
    static const short SX[4] = {58, 282, 58, 282}, SY[4] = {92, 92, 270, 270};
    for (int k = 0; k < STARS[g.stage()]; k++) icon(IC_STAR, 3, SX[k], SY[k]);
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
    SC = g.stage() == STAGE_FOAL ? 4 : 6;
    palettes();
    fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
    char line[32];
    switch (g.scene) {
    case SC_EGG:   scene_egg(g); break;
    case SC_HATCH: scene_hatch(g); break;
    case SC_INFO:  scene_info(g); break;
    case SC_GAME:  scene_game(g); break;
    case SC_EAT:   home_like(g, ST_EAT, "YEMEK YİYOR", false, -1, 0, false); break;
    case SC_FEED:  scene_feed(g); break;
    case SC_SETTINGS: scene_settings(g); break;
    case SC_PLAY: {                        // celebration after the mini-game
        utoa(g.score, line);
        int n = 0;
        while (line[n]) n++;
        const char* tail = " YILDIZ! ALKIŞ!";
        for (int i = 0; tail[i]; i++) line[n++] = tail[i];
        line[n] = 0;
        squash_now = g.calm() ? 0 : squash_for(now(g) % 700, 700 - SQUASH_MS);
        home_like(g, ST_PLAY, line, false, 0, g.calm() ? 0 : arc(now(g) % 700, 700 - SQUASH_MS, 30), false);
        squash_now = 0;
        break;
    }
    case SC_WASH:  home_like(g, ST_WASH, "BANYO ZAMANI", false, -1, 0, false); bath_foam(g); break;
    case SC_EGG_PICK: scene_egg_pick(g); break;
    case SC_TOYS:  scene_toys(g); break;
    case SC_MUSIC: scene_music(g); break;
    case SC_CLOUD: scene_cloud(g); break;
    case SC_DRESS: scene_dress(g); break;
    case SC_ALBUM: scene_album(g); break;
    case SC_LOUNGE: scene_lounge(g); break;
    case SC_SLEEP: scene_sleep(g); break;
    case SC_LEAVE: walker(g, "ARKADAŞLARI ÇAĞIRIYOR", "İŞİM VAR!"); break;
    case SC_RETURN: walker(g, "EVE DÖNDÜ", g.vel_x ? (const char*)0 : "GELDİM!"); break;
    case SC_AWAY:  scene_away(g); break;
    case SC_REFUSE: scene_refuse(g); break;
    default:       scene_home(g); break;
    }
}

}  // namespace up
