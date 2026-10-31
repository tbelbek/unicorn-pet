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
enum { IC_APPLE, IC_HEART, IC_STAR, IC_BUBBLE, IC_MOON, IC_BALL, IC_STRAWBERRY, IC_CARROT, IC_HAND };   // export_c.ICONS

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

// x of the sprite's left edge, following the unicorn as she wanders
static int side_x(const Game& g) {
    return (SCREEN_W - UP_SIDE_W * 6) / 2 + 6 + burn_x(g) + g.pet_x + ahead(g.vel_x, g);
}
static int side_y(const Game& g) { return 346 - UP_SIDE_FEET * 6 + burn_y(g); }

// squash > 0: wider and lower (landing), < 0: taller and thinner (take-off); feet stay planted
static int squash_now;

static void unicorn_side(const Game& g, int state, bool night, int frame, int dy) {
    const int sz = UP_SIDE_W * UP_SIDE_H;
    const u8* spr = UP_SIDE + ((g.s.character * UP_SIDE_STATES + state) * 2 + frame) * sz;
    int sx = 6 + squash_now, sy = 6 - squash_now;
    int x0 = side_x(g) - (sx - 6) * UP_SIDE_W / 2, feet = side_y(g) + UP_SIDE_FEET * 6 + dy;
    blit2(spr, UP_SIDE_W, UP_SIDE_H, sx, sy, x0, feet - UP_SIDE_FEET * sy, night ? NIGHT : PAL, g.facing < 0);
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

static void home_like(const Game& g, int state, const char* sub, bool night, int frame = -1, int dy = 0,
                      bool show_menu = true) {
    if (frame < 0) frame = g.frame;
    if (night) sky();
    status(g);
    header(g, sub);
    ground(g, night);
    // messes sit on the floor beside her; during a bath they shrink away
    int n = g.scene == SC_WASH ? g.wash_messes : g.scene == SC_SLEEP ? 0 : g.messes();
    int size = 4;
    if (g.scene == SC_WASH) size = g.scene_ms >= 1600 ? 0 : 4 - (int)(g.scene_ms / 400);
    static const int mx[3] = {58, 312, 98};
    for (int i = 0; i < n; i++) mess(mx[i] + burn_x(g), 344 + burn_y(g), size);
    unicorn_side(g, state, night, frame, dy);
    int sx = side_x(g), sy = side_y(g) + dy;
    if (state == M_HAPPY) { sparkle(sx + 34, sy + 52, 4, YELLOW); sparkle(sx + 230, sy + 42, 3, PINK); }
    if (state == M_HUNGRY) thought(sx + (g.facing < 0 ? 10 : 196), sy + 6);
    if (state == M_SLEEPY) zz(sx + 220 + wave(now(g), 2000, 4), sy + 32 - (int)(now(g) / 40 % 24));
    if (state == ST_SLEEP) zz(sx + 220 + wave(now(g), 2600, 6), sy + 118 - (int)(now(g) / 45 % 40));
    if (state == ST_EAT) {                 // today's treat in the bowl, eaten bite by bite
        static const int treats[TREAT_COUNT] = {IC_APPLE, IC_STRAWBERRY, IC_CARROT};
        int left = 3 - (int)(g.scene_ms * 3 / ACTION_MS);
        if (left > 0) icon(treats[g.treat], left, sx + 34 * 6 + 3 - UP_ICON_W * left / 2,
                           sy + (36 + 4) * 6 - UP_ICON_H * left);
    }
    particles(g);
    if (show_menu) menu(g);
}

static void scene_home(const Game& g) {
    char day[24];
    utoa(g.s.age_s / 86400 + 1, day);
    int n = 0;
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
    }
    if (g.calm()) { dy = 0; squash_now = 0; }               // parent setting: calm motion
    home_like(g, state, day, false, frame, dy);
    squash_now = 0;
    if (g.hint_pet && !g.petting) hand_hint(g, side_x(g) + 252, side_y(g) + 156);   // "pet me", on black
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
static void scene_settings(const Game& g) {
    static const char* names[SET_ROWS] = {"SES", "OTURUM", "MOLA", "PARLAKLIK", "SAKİN MOD", "SALLAMA", "ÇIKIŞ"};
    static const char* vols[3] = {"NORMAL", "KISIK", "KAPALI"};
    char v[16];
    text("EBEVEYN AYARLARI", SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < SET_ROWS; i++) {
        int y = 70 + i * 44;
        if (i == g.set_row) rrect(16, y, SCREEN_W - 16, y + 38, 10, SEL, LILAC, 2);
        text(names[i], 32, y + 12, 2, i == SET_EXIT ? YELLOW : INK, 0);
        const char* val = "";
        switch (i) {
        case SET_VOLUME: val = vols[g.volume()]; break;
        case SET_LIMIT:  utoa(g.play_limit_ms() / 60000, v); val = v; break;
        case SET_BREAK:  utoa(g.break_ms() / 60000, v); val = v; break;
        case SET_BRIGHT: utoa(g.brightness() * 100 / 255, v); val = v; break;
        case SET_CALM:   val = g.calm() ? "AÇIK" : "KAPALI"; break;
        case SET_SHAKE:  val = (g.s.opts & OPT_NO_SHAKE) ? "KAPALI" : "AÇIK"; break;
        }
        if (*val) {
            int n = 0;
            while (val[n]) n++;
            const char* unit = i == SET_LIMIT || i == SET_BREAK ? " DK" : i == SET_BRIGHT ? "%" : "";
            char buf[24];
            int k = 0;
            for (int j = 0; val[j]; j++) buf[k++] = val[j];
            for (int j = 0; unit[j]; j++) buf[k++] = unit[j];
            buf[k] = 0;
            text(buf, SCREEN_W - 32 - text_w(buf, 1), y + 16, 1, DIM, 0);
        }
    }
    utoa(g.battery, v);
    char line[40];
    int k = 0;
    const char* a = "PİL ";
    for (int j = 0; a[j]; j++) line[k++] = a[j];
    if (g.battery) { for (int j = 0; v[j]; j++) line[k++] = v[j]; line[k++] = '%'; } else line[k++] = '?';
    const char* b = g.wall_s ? "  ·  SAAT: VAR" : "  ·  SAAT: YOK";
    for (int j = 0; b[j]; j++) line[k++] = b[j];
    line[k] = 0;
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
    if (says && side_x(g) < SCREEN_W - 120 && now(g) < 1200) bubble(side_x(g) + 200, side_y(g) - 6, says);
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
         SCREEN_H - UP_SIDE_H * 3 - 4 + arc(now(g) % 600, 600, 18), PAL);
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
    blit(UP_EGG, UP_EGG_W, UP_EGG_H, 8, x0, y0, PAL);
    int stage = g.taps;
    if (g.s.egg_ms > EGG_HATCH_MS / 3 && stage < 1) stage = 1;
    if (g.s.egg_ms > EGG_HATCH_MS * 2 / 3 && stage < 2) stage = 2;
    cracks(x0, y0, stage);
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

static void scene_info(const Game& g) {
    static const char* says[] = {"İYİYİM!", "ÇOK MUTLUYUM!", "KARNIM AÇ!", "UYKUM VAR...", "SARILALIM MI?"};
    static const int icons[NSTATS] = {IC_APPLE, IC_HEART, IC_STAR, IC_BUBBLE};
    Mood m = g.mood();
    // sound on/off toggle (hit box SOUND_X0 / SOUND_Y1 in unicorn.h), clear of the rounded corner
    bool off = g.volume() == VOL_OFF;
    rrect(SOUND_X0 + 8, 14, SCREEN_W - 30, 40, 10, SEL, off ? TRACK : PAL[UP_ACCENT[g.s.character]], 2);
    text(off ? "SES KAPALI" : "SES AÇIK", (SOUND_X0 + 8 + SCREEN_W - 30) / 2, 23, 1, off ? DIM : INK);
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
    case SC_WASH:  home_like(g, ST_WASH, "BANYO ZAMANI", false, -1, 0, false); break;
    case SC_SLEEP: home_like(g, ST_SLEEP, "UYUYOR · İYİ GECELER", true, 0, wave(now(g), 3200, 2) + 2); break;
    case SC_LEAVE: walker(g, "ARKADAŞLARI ÇAĞIRIYOR", "İŞİM VAR!"); break;
    case SC_RETURN: walker(g, "EVE DÖNDÜ", g.vel_x ? (const char*)0 : "GELDİM!"); break;
    case SC_AWAY:  scene_away(g); break;
    case SC_REFUSE: scene_refuse(g); break;
    default:       scene_home(g); break;
    }
}

}  // namespace up
