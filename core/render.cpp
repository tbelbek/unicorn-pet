// Renders a Game into an RGB565 framebuffer. Layout mirrors design/showcase.png.
#include "unicorn.h"
#include "assets.gen.h"
#include "strings.gen.h"

namespace up {

static u16* FB;                             // rows [CY0, CY1) of the screen: a whole frame, or one band of it
static int CY0 = 0, CY1 = SCREEN_H;
static inline bool off_band(int y0, int y1) { return y1 <= CY0 || y0 >= CY1; }   // rows [y0, y1) miss the band
static int LANG;                            // the save's language, set at the top of render()
static_assert(STR_LANGS == LANG_COUNT, "tools/i18n.py and enum Lang disagree");
static const char* T(int id) { return UP_STR[LANG][id]; }
// a string with '#' standing for a number: fmt(buf, S_DAYS, 12) -> "DAY 12 TOGETHER"
static void utoa(u32 v, char* out);
template <int N> static const char* fmt(char (&out)[N], int id, u32 n) {
    const char* p = T(id);
    int k = 0;
    for (; *p && k < N - 1; p++) {         // never past the end of out, however long the language
        if (*p != '#') { out[k++] = *p; continue; }
        char d[12];
        utoa(n, d);
        for (int i = 0; d[i] && k < N - 1; i++) out[k++] = d[i];
    }
    out[k] = 0;
    return out;
}
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
    if ((unsigned)x < SCREEN_W && y >= CY0 && y < CY1) FB[(y - CY0) * SCREEN_W + x] = c;
}

static void fill(int x0, int y0, int x1, int y1, u16 c) {          // [x0,x1) x [y0,y1)
    if (x0 < 0) x0 = 0;
    if (y0 < CY0) y0 = CY0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > CY1) y1 = CY1;
    if (x0 >= x1) return;
    u32 cc = (u32)c << 16 | c;
    for (int y = y0; y < y1; y++) {        // two pixels per store: every frame clears and fills a lot
        u16* p = FB + (y - CY0) * SCREEN_W + x0;
        int n = x1 - x0;
        if ((unsigned long)p & 2) { *p++ = c; n--; }   // (unsigned long: pointer-sized here, host and boards)
        u32* q = (u32*)p;
        for (; n >= 2; n -= 2) *q++ = cc;
        if (n) *(u16*)q = c;
    }
}

// The same pixels as testing every (x, y) against x²ry² + y²rx² <= rx²ry², one span per row: |x| only
// shrinks as |y| grows, so each row's edge starts from the last one's.
static void ellipse(int cx, int cy, int rx, int ry, u16 c) {
    if (off_band(cy - ry, cy + ry + 1)) return;
    long long rr = (long long)rx * rx * ry * ry;
    int xm = rx;
    for (int y = 0; y <= ry; y++) {
        long long yy = (long long)y * y * rx * rx;
        while (xm >= 0 && (long long)xm * xm * ry * ry + yy > rr) xm--;
        if (xm < 0) break;
        fill(cx - xm, cy + y, cx + xm + 1, cy + y + 1, c);
        if (y) fill(cx - xm, cy - y, cx + xm + 1, cy - y + 1, c);
    }
}

static void rrect(int x0, int y0, int x1, int y1, int r, u16 fillc, u16 line, int width) {
    for (int y = y0 > CY0 ? y0 : CY0; y <= y1 && y < CY1; y++)
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
    if (sy <= 0 || off_band(y0, y0 + h * sy)) return;
    for (int y = y0 >= CY0 ? 0 : (CY0 - y0) / sy; y < h && y0 + y * sy < CY1; y++)
        for (int x = 0; x < w; x++) {
            u8 i = data[y * w + (flip ? w - 1 - x : x)];
            if (i) fill(x0 + x * sx, y0 + y * sy, x0 + (x + 1) * sx, y0 + (y + 1) * sy, pal[i]);
        }
}

static void blit(const u8* data, int w, int h, int scale, int x0, int y0, const u16* pal, bool flip = false) {
    blit2(data, w, h, scale, scale, x0, y0, pal, flip);
}

// Small hand-drawn sprites: one char per pixel, '.' is clear, `keys` names the colours in `cols`.
static void art(const char* const* rows, int h, const char* keys, const u16* cols, int x0, int y0, int s,
                bool flip = false) {
    if (s <= 0 || off_band(y0, y0 + h * s)) return;
    for (int y = y0 >= CY0 ? 0 : (CY0 - y0) / s; y < h && y0 + y * s < CY1; y++) {
        const char* r = rows[y];
        int w = 0;
        while (r[w]) w++;
        for (int x = 0; x < w; x++) {
            char c = r[flip ? w - 1 - x : x];
            for (int k = 0; c != '.' && keys[k]; k++)
                if (keys[k] == c) { fill(x0 + x * s, y0 + y * s, x0 + (x + 1) * s, y0 + (y + 1) * s, cols[k]); break; }
        }
    }
}

// sky things, drawn on a 3-5 px grid like everything else
static const char* const SUN[17] = {
    "........r........",
    "........r........",
    "..r...........r..",
    "...r.........r...",
    "......yyyyy......",
    ".....yhhyyyo.....",
    "....yhhhhyyyo....",
    "....yhhhhyyyo....",
    "rr..yyhhyyyyo..rr",
    "....yyyyyyyoo....",
    "....yyyyyyooo....",
    ".....oyyyooo.....",
    "......ooooo......",
    "...r.........r...",
    "..r...........r..",
    "........r........",
    "........r........",
};
static const char* const MOON_ART[13] = {
    "............",
    "...mmm......",
    "..mmm.......",
    ".mmmm.......",
    ".mmmm.......",
    "mmmmm.......",
    "mmmmm.......",
    "mmddmm......",
    ".dddmmm.....",
    ".mdmmmmmmmm.",
    "..mmdddddd..",
    "...dddddd...",
    "............",
};
static const char* const CLOUD[11] = {
    "...........www..........",
    ".........wwwwwww........",
    "........wwwwwwwww.......",
    "........wwwwwwwww.......",
    "....wwwwwwwwwwwwwwwww...",
    "...wwwwwwwwwwwwwwwwwww..",
    "...wwwwwwwwwwwwwwwwwww..",
    "...wwwwwwwwwwwwwwwwwww..",
    "...wwwwwwwwwwwwwwwwwww..",
    "...swwwwwwwwwwwwwwwwws..",
    "....sssssssssssssssss...",
};

static const char* const FOAM[9] = {
    "......www.....",
    "....wwwwww....",
    "....wwhhwww...",
    ".wwwwhwwwwwww.",
    ".whwwwwwwwwww.",
    ".wwwwwwwwwwwww",
    ".wwwwwwwwwwwws",
    ".ssssssssssss.",
    "...ssssss.ss..",
};
static const char* const BUBBLE_ART[8] = {
    "..cccc..",
    ".c....c.",
    "c.hh...c",
    "c.h....c",
    "c......c",
    "c......c",
    ".c....c.",
    "..cccc..",
};
static const char* const CAKE[17] = {
    ".......ooooooooooo.......",
    "......occccccccccco......",
    "......occccccccccco......",
    "......oaccaaccabcco......",
    "......oaacaagaabbco......",
    "......oaayaaaaabbbo......",
    "...ooooaaaaaaaubbboooo...",
    "..occccccccccccccccccco..",
    "..occccccccccccccccccco..",
    "..oaccraccaaccaaccabcco..",
    "..oaacaaaaayacaaauabbco..",
    "..oaagaaaaaaaaaaaaabbbo..",
    "..oaaaaaaaaaaaraaaabbbo..",
    "..oaaaaauaaaaaaaaaabbbo..",
    "..oaaaaaaaaaaaaaaaabbbo..",
    "..pooooooooooooooooooop..",
    ".ppppppppppppppppppppppp.",
};
static const char* const FLAME[2][4] = {{".o.", "oyo", "oyo", ".o."}, {"..o", ".oo", "oyo", ".o."}};

// a curled little rainbow mess in three sizes
static const char* const MESS1[5] = {
    "...oo...",
    "..owpo..",
    ".opppgo.",
    "obbbbbo.",
    ".ooooo..",
};
static const char* const MESS2[7] = {
    ".....oo....",
    "....owgo...",
    "...oggggo..",
    "..oppggggo.",
    ".opwppppo.",
    "obbbbbbbbo.",
    ".oooooooo..",
};
static const char* const MESS3[10] = {
    "......oo......",
    ".....owyo.....",
    "....oyyyyo....",
    "...oggggyo....",
    "..ogwggggoo...",
    "..opppgggggo..",
    ".oppwpppppggo.",
    "opppppppppppo.",
    "obbbbbbbbbbbo.",
    ".oooooooooooo.",
};

// the music garden's flowers (closed, open, bursting), a stem with two leaves, the sun lounger
static const char* const FLOWER_BUD[17] = {
    ".................",
    ".................",
    ".................",
    ".................",
    ".......ooo.......",
    "......oppqo......",
    ".....opppqqo.....",
    ".....opppqqo.....",
    "....oppppqqqo....",
    "....oppppqqqo....",
    "....oppppqqqo....",
    ".....opppqqo.....",
    "......olllo......",
    ".......ooo.......",
    ".................",
    ".................",
    ".................",
};
static const char* const FLOWER_OPEN[17] = {
    ".................",
    "........o........",
    "......oopoo......",
    ".....opppppo.....",
    "...ooopppppooo...",
    "..opppppppppppo..",
    "..oppppcccppppo..",
    ".oppppcccccppppo.",
    "..opppcceecpppo..",
    "...oppcceecppo...",
    "...opppcccpppo...",
    "...oqqqqqqqqqo...",
    "...oqqqqqqqqqo...",
    "....oqqqoqqqo....",
    ".....ooo.ooo.....",
    ".................",
    ".................",
};
static const char* const FLOWER_BURST[17] = {
    "........o........",
    "......oopoo......",
    ".....opppppo.....",
    "....oopppppoo....",
    "..oopopppppopoo..",
    ".opppppppppppppo.",
    ".opppppcccpppppo.",
    ".oppppcccccppppo.",
    ".oppppcceecppppo.",
    "..opppcceecpppo..",
    "...opppcccpppo...",
    "..oqqqqqqqqqqqo..",
    "..oqqqqqqqqqqqo..",
    "...oqqqqoqqqqo...",
    "....oqqo.oqqo....",
    ".....oo...oo.....",
    ".................",
};
static const char* const* const FLOWER[3] = {FLOWER_BUD, FLOWER_OPEN, FLOWER_BURST};
static const char* const STEM[14] = {
    "...ll....",
    "...lm....",
    "...lm....",
    ".llllm...",
    "lllolm...",
    ".oo.lm...",
    "....lmll.",
    "....lmlll",
    "....lm.oo",
    "....lm...",
    "....lm...",
    "....lm...",
    "....lm...",
    "....lm...",
};
static const char* const LOUNGER[32] = {
    "....................................bbbbbbbbbb.",
    "....................................baaaaaaaab.",
    "....................................baaaaaaaab.",
    "...................................bwwwwwwwwb..",
    "...................................bwwwwwwwwb..",
    "...................................bwwwwwwwwb..",
    "...................................baaaaaaaab..",
    "..................................baaaaaaaab...",
    "..................................baaaaaaaab...",
    "..................................bwwwwwwwwb...",
    ".................................bwwwwwwwwb....",
    ".................................bwwwwwwwwb....",
    ".................................baaaaaaaab....",
    ".................................baaaaaaaab....",
    "................................baaaaaaaab.....",
    "................................bwwwwwwwwb.....",
    "................................bwwwwwwwwb.....",
    "...............................bwwwwwwwwb......",
    "...............................baaaaaaaab......",
    "...............................baaaaaaaab......",
    "...............................baaaaaaaab......",
    "..............................bwwwwwwwwb.......",
    "..............................bwwwwwwwwb.......",
    "..............................bwwwwwwwwb.......",
    ".............................baaaaaaaab........",
    ".............................baaaaaaaab........",
    ".............................baaaaaaaab........",
    ".............................bwwwwwwwwb........",
    "aaawwwaaawwwaaawwwaaawwwaaawwwaaawwwaaawww.....",
    "aaawwwaaawwwaaawwwaaawwwaaawwwaaawwwaaawww.....",
    "ssssssssssssssssssssssssssssssssssssssssss.....",
    "bbbbddbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbddbbbb.....",
};

static void icon(int i, int scale, int x, int y) {
    blit(UP_ICONS + i * UP_ICON_W * UP_ICON_H, UP_ICON_W, UP_ICON_H, scale, x, y, PAL);
}

// ---- text (Press Start 2P, 8 px cells, UTF-8 in) -----------------------------------------

static const char* next_cp(const char* s, u32* cp) {
    u8 c = (u8)*s;
    int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 1) { *cp = c; return s + 1; }
    u32 v = len == 2 ? c & 0x1F : len == 3 ? c & 0x0F : c & 0x07;
    for (int i = 1; i < len; i++) {        // stop at a terminator or a broken sequence
        if (((u8)s[i] & 0xC0) != 0x80) { *cp = '?'; return s + i; }
        v = (v << 6) | ((u8)s[i] & 0x3F);
    }
    *cp = len ? v : '?';
    return s + (len ? len : 1);
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
static int overflows;                      // text that ran off the screen (a test checks every language)
int text_overflows() { int n = overflows; overflows = 0; return n; }

static void text(const char* s, int x, int y, int scale, u16 c, int align = 1) {
    int w = text_w(s, scale);
    while (scale > 1) {                    // too wide for the screen (a long word in some language): smaller
        int x0 = align == 1 ? x - w / 2 : x;
        if (x0 >= 0 && x0 + w <= SCREEN_W) break;
        scale--;
        y += UP_FONT_H / 2;                // keep the line roughly where it was
        w = text_w(s, scale);
    }
    if (align == 1) x -= w / 2;
    if ((x < 0 || x + w > SCREEN_W) && CY0 == 0) overflows++;   // (once per frame, not once per band)
    if (off_band(y, y + UP_FONT_H * scale)) return;
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

// centred text that drops to the small size if it would not fit the given width
static void text_fit(const char* s, int cx, int y, int scale, u16 c, int max_w = SCREEN_W - 24) {
    int want = scale;
    while (scale > 1 && text_w(s, scale) > max_w) scale--;
    text(s, cx, y + (want - scale) * 4, scale, c);   // a smaller size stays centred on the line
}

// left-aligned text cut to max_w pixels, ".." marking the cut (long network names)
static void text_clip(const char* s, int x, int y, int scale, u16 c, int max_w) {
    if (text_w(s, scale) <= max_w) { text(s, x, y, scale, c, 0); return; }
    char buf[100];
    int k = 0;
    for (const char* p = s; *p;) {         // whole characters only, while they still fit with ".."
        u32 cp;
        const char* q = next_cp(p, &cp);
        int keep = k;
        while (p < q && k < 94) buf[k++] = *p++;
        buf[k] = '.'; buf[k + 1] = '.'; buf[k + 2] = 0;
        if (text_w(buf, scale) > max_w) { k = keep; break; }
    }
    buf[k] = '.'; buf[k + 1] = '.'; buf[k + 2] = 0;
    text(buf, x, y, scale, c, 0);
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

static int ease(int p) { return p * p * (768 - 2 * p) / 65536; }   // smoothstep, 0..256 -> 0..256
// how far p has got through [a, b): 0 before, 256 after, eased in between (slow in, slow out)
static int seg(u32 p, u32 a, u32 b) { return p <= a ? 0 : p >= b ? 256 : ease((int)((p - a) * 256 / (b - a))); }
static int lerp(int a, int b, int q) { return a + (b - a) * q / 256; }

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

// what she is thinking of: the apple (hungry), soap bubbles (dirty) or a little rain cloud (sad)
enum { THINK_APPLE, THINK_SOAP, THINK_RAIN };
static void thought(int x, int y, int what = THINK_APPLE) {
    ellipse(x + 20, y + 20, 20, 20, BUBBLE);
    ellipse(x - 4, y + 42, 6, 6, BUBBLE);
    ellipse(x - 16, y + 56, 3, 3, BUBBLE);
    if (what == THINK_APPLE) { icon(IC_APPLE, 4, x + 2, y + 4); return; }
    if (what == THINK_SOAP) {              // the bath icon on a darker disc, so it shows on the white bubble
        ellipse(x + 20, y + 20, 15, 15, rgb(0x4C, 0x7A, 0xC8));
        icon(IC_BUBBLE, 3, x + 20 - UP_ICON_W * 3 / 2, y + 20 - UP_ICON_H * 3 / 2);
        return;
    }
    static const u16 RAIN_C = rgb(0x7A, 0x84, 0xA8), DROP = rgb(0x3A, 0x9C, 0xF0);
    ellipse(x + 13, y + 16, 7, 6, RAIN_C);
    ellipse(x + 23, y + 12, 9, 8, RAIN_C);
    ellipse(x + 30, y + 17, 6, 5, RAIN_C);
    fill(x + 7, y + 16, x + 36, y + 22, RAIN_C);
    for (int k = 0; k < 3; k++) fill(x + 11 + k * 9, y + 26 + (k & 1) * 4, x + 14 + k * 9, y + 32 + (k & 1) * 4, DROP);
}

static void stars() {
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
}

static void sky() {
    stars();
    static const u16 MOON_COLS[2] = {MOON, rgb(0xE0, 0xC0, 0x70)};
    art(MOON_ART, 13, "md", MOON_COLS, 40, 100, 4);   // top left: her Zz and dreams rise on the right
}

// Day and evening skies for the home screens (night uses sky()). Kept dim: it is a black AMOLED.
static void backdrop(const Game& g) {
    int ph = g.phase();
    if (ph == PH_NIGHT) { sky(); return; }
    u16 cloud[2] = {rgb(0x3A, 0x34, 0x5E), rgb(0x2E, 0x29, 0x4C)};
    if (ph == PH_DAY) {
        static const u16 SUN_COLS[4] = {YELLOW, rgb(0xFF, 0xB3, 0x47), rgb(0xFF, 0xF6, 0xC8), rgb(0xFF, 0xC8, 0x4D)};
        art(SUN, 17, "yohr", SUN_COLS, SCREEN_W - 62 - 34, 128 - 34, 4);
    } else {                               // evening: a big low orange sun, no rays, a first star
        static const u16 DUSK[3] = {rgb(0xFF, 0x9F, 0x43), rgb(0xE8, 0x6A, 0x3A), rgb(0xFF, 0xC8, 0x7A)};
        art(SUN, 17, "yoh", DUSK, SCREEN_W - 60 - 43, 150 - 43, 5);
        cloud[0] = rgb(0x4A, 0x2E, 0x52);
        cloud[1] = rgb(0x3A, 0x26, 0x46);
        sparkle(70, 180, 2, rgb(0xC9, 0xCC, 0xFF));
    }
    art(CLOUD, 11, "ws", cloud, 70 - 36, 124 - 16, 3);   // a soft cloud
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
    if (!g.batt_known) return;
    int pct = g.battery, bars = (pct + 24) / 25;
    u16 c = pct > 50 ? rgb(0x5E, 0xE0, 0x7A) : pct > 20 ? YELLOW : rgb(0xFF, 0x5C, 0x6E);
    bool blink = pct < 10 && !g.plugged && ((g.clock_ms + g.acc_ms) / 500) & 1;
    u16 edge = blink ? rgb(0xFF, 0x5C, 0x6E) : DIM;
    rrect(x0, y0, x0 + 30, y0 + 16, 4, BLACK, edge, 2);
    fill(x0 + 30, y0 + 5, x0 + 33, y0 + 11, edge);   // the nub
    for (int i = 0; i < bars && i < 4; i++) fill(x0 + 4 + i * 6, y0 + 4, x0 + 8 + i * 6, y0 + 12, c);
    if (g.plugged) {                       // a lightning bolt just left of the battery
        static const char* BOLT[8] = {"...##", "..##.", ".##..", "#####", "..##.", ".##..", "##...", "#...."};
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 5; c++)
                if (BOLT[r][c] == '#') fill(x0 - 15 + c * 2, y0 + r * 2, x0 - 13 + c * 2, y0 + r * 2 + 2, YELLOW);
    }
}

// Her name and what she is doing, big enough for a child to see at arm's length (8 px text is 0.6 mm on
// this panel). Long ones step down a size: clear of the battery and bolt (from x 298), or of the house
// button (from x 288) in the toy modes.
static void header(const Game& g, const char* sub, bool house = false) {
    int w = house ? 2 * (288 - SCREEN_W / 2) - 8 : 2 * (298 - SCREEN_W / 2) - 4;
    text_fit(T(S_NAME_0 + g.s.character), SCREEN_W / 2, 38, 3, PAL[UP_ACCENT[g.s.character]], w);
    text_fit(sub, SCREEN_W / 2, 68, 2, DIM, house ? w : SCREEN_W - 24);
}

static int arc(u32 p, u32 len, int height);
static int wave(u32 t, u32 period, int amp);

static void menu(const Game& g) {
    static const int icons[MENU_COUNT] = {IC_APPLE, IC_BALL, IC_MOON, IC_BUBBLE};
    static const short labels[MENU_COUNT] = {S_MENU_FEED, S_MENU_PLAY, S_MENU_SLEEP, S_MENU_WASH};
    u32 t = g.clock_ms + g.acc_ms;
    int rec = g.recommended();
    for (int i = 0; i < MENU_COUNT; i++) {
        int cx = MENU_STEP / 2 + i * MENU_STEP, scale = 5, dy = 0;
        if (i == g.menu)
            rrect(cx - 42, MENU_Y0, cx + 42, MENU_Y1, 14, SEL, PAL[UP_ACCENT[g.s.character]], 2);
        if (i == g.press_item && (int)(g.press_end - t) > 0) {      // dip, then pop past and settle
            int left = (int)(g.press_end - t);
            scale = left > PRESS_MS * 2 / 3 ? 4 : left > PRESS_MS / 3 ? 6 : 5;
        } else if (i == rec && !g.calm()) {                          // her need gently bounces
            dy = arc(t % 1100, 500, 6);
        }
        // the word on top, the picture below: the panel's rounded bottom corners hid BESLE and YIKA (on the board)
        if (text_w(T(labels[i]), 1) <= 80) text_fit(T(labels[i]), cx, 366, 2, i == g.menu ? INK : DIM, 80);
        icon(icons[i], scale, cx - UP_ICON_W * scale / 2, 410 - UP_ICON_H * scale / 2 + dy);
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
static int nudge;                           // a sideways sway (drowsy at bedtime, asleep on her feet)
static int side_x(const Game& g) {
    return (SCREEN_W - UP_SIDE_W * SC) / 2 + SC + burn_x(g) + g.pet_x + ahead(g.vel_x, g) + nudge;
}
static int side_y(const Game& g) { return 346 - UP_SIDE_FEET * SC + burn_y(g); }

// ---- a unicorn in parts: the head nods and reaches, the tail flicks -------------------------
// Every standing pose shares one layout (checked over all characters, states and frames): the head is
// rows 0-25 from column 10 on, the tail's tuft rows 15-25 up to column 10, the chest starts at row 25
// or 26. Rows 21-25 of the neck are drawn fixed as well, so a lifted head leaves no hole over the chest.
static bool standing(int state) { return state <= M_SAD || state == ST_WALK || state == ST_BLINK || state == ST_WALK2; }

// sprite pixels [x0,x1) x [y0,y1) of a side sprite whose pixel (0,0) sits at (px, py), scale s
static void part(const u8* spr, int x0, int y0, int x1, int y1, int s, int px, int py, const u16* pal, bool flip) {
    if (off_band(py + y0 * s, py + y1 * s)) return;
    for (int y = y0; y < y1; y++) {
        if (off_band(py + y * s, py + (y + 1) * s)) continue;
        for (int x = x0; x < x1; x++) {
            u8 i = spr[y * UP_SIDE_W + x];
            if (!i) continue;
            int dx = flip ? UP_SIDE_W - 1 - x : x;
            fill(px + dx * s, py + y * s, px + (dx + 1) * s, py + (y + 1) * s, pal[i]);
        }
    }
}
// hdx, hdy: the head's offset in sprite pixels (|hdx| <= 1: further, the neck seam shows); tdx: the tuft's
static void rig(const u8* spr, int s, int px, int py, const u16* pal, bool flip, int hdx, int hdy, int tdx) {
    if (flip) { hdx = -hdx; tdx = -tdx; }
    part(spr, 0, 26, UP_SIDE_W, UP_SIDE_H, s, px, py, pal, flip);          // body and legs
    part(spr, 11, 21, 26, 26, s, px, py, pal, flip);                       // the neck, fixed
    part(spr, 0, 15, 11, 26, s, px + tdx * s, py, pal, flip);              // the tail's tuft
    part(spr, 10, 0, UP_SIDE_W, 26, s, px + hdx * s, py + hdy * s, pal, flip);   // the head
}

// squash > 0: wider and lower (landing), < 0: taller and thinner (take-off); feet stay planted
static int squash_now;
static int turn = 1;                       // -1: drawn facing the other way (a twirl or dance)
static int acc_draw = -1;                  // dress-up preview; -1 = what she is wearing
static int head_dy;                        // a nod: her head drawn this many sprite rows lower (standing poses)

static void unicorn_side(const Game& g, int state, bool night, int frame, int dy) {
    const int sz = UP_SIDE_W * UP_SIDE_H;
    const u8* spr = UP_SIDE + ((g.s.character * UP_SIDE_STATES + state) * 2 + frame) * sz;
    int sx = SC + squash_now, sy = SC - squash_now;
    bool flip = g.facing * turn < 0;
    int x0 = side_x(g) - (sx - SC) * UP_SIDE_W / 2, top = side_y(g) + UP_SIDE_FEET * SC + dy - UP_SIDE_FEET * sy;
    if (state != ST_SLEEP && dy < 0 && top < HEADER_Y1 - 3 * SC) top = HEADER_Y1 - 3 * SC;   // a high jump stops short of the name
    bool rigged = head_dy && !squash_now && standing(state);
    if (rigged) rig(spr, SC, x0, top, night ? NIGHT : PAL, flip, 0, head_dy, 0);
    else blit2(spr, UP_SIDE_W, UP_SIDE_H, sx, sy, x0, top, night ? NIGHT : PAL, flip);
    if (g.stage() >= STAGE_GROWN && state != ST_SLEEP && g.scene != SC_SLEEP) {   // her grown-up marks
        static const i8 FX[3] = {15, 11, 19}, FY[3] = {29, 32, 33};
        static const u16* const FC[3] = {&YELLOW, &PINK, &LILAC};
        int n = g.stage() == STAGE_STORYKEEPER ? 3 : 1;
        for (int k = 0; k < n; k++) {      // on the same squashed/stretched, mirrored grid as her body
            int fx = flip ? UP_SIDE_W - 1 - FX[k] : FX[k];
            sparkle(x0 + fx * sx, top + FY[k] * sy, 3, *FC[k]);
        }
    }
    int acc = acc_draw >= 0 ? acc_draw : g.s.accessory;
    if (acc > ACC_NONE && acc < ACC_COUNT) {   // the accessory follows her head in every pose
        int ax = UP_HEAD[state][frame][0], ay = UP_HEAD[state][frame][1] + (rigged ? head_dy : 0);
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
    if (size <= 0) return;
    static const u16 COLS[5] = {rgb(0x5B, 0x2A, 0x86), RAINBOW[0], YELLOW, RAINBOW[3], rgb(0xC0, 0x3A, 0x6A)};
    static const u16 SHINE[1] = {INK};
    const char* const* m = size >= 3 ? MESS3 : size == 2 ? MESS2 : MESS1;
    int h = size >= 3 ? 10 : size == 2 ? 7 : 5, w = size >= 3 ? 14 : size == 2 ? 11 : 8, s = 4;
    art(m, h, "opygb", COLS, cx - w * s / 2, base - h * s, s);
    art(m, h, "w", SHINE, cx - w * s / 2, base - h * s, s);
}

// top: a scene's header ends there; hearts and notes rising into it vanish instead of crossing her name
static void particles(const Game& g, int top = 0) {
    for (int i = 0; i < NPART; i++) {
        const Part& p = g.part[i];
        if (p.kind == P_NONE) continue;
        int x = p.x + ahead(p.vx * 2, g), y = p.y + ahead(p.vy * 2, g);
        if (y - 14 < top) continue;
        switch (p.kind) {
        case P_HEART:    x += wave(now(g) + i * 97, 900, 4); icon(IC_HEART, 3, x - 13, y - 12); break;
        case P_STAR:     icon(IC_STAR, 2, x - 9, y - 8); break;
        case P_NOTE:     x += wave(now(g) + i * 131, 700, 5); icon(IC_NOTE, 3, x - 13, y - 12); break;
        case P_SPARKLE:  sparkle(x, y, 3, p.col & 1 ? YELLOW : PINK); break;
        case P_CRUMB:    fill(x, y, x + 5, y + 5, RAINBOW[p.col % 3]); break;
        case P_CONFETTI: fill(x, y, x + 6, y + 6, RAINBOW[p.col % 7]); break;
        case P_RING: {                     // an expanding ring where a star popped, on a 4 px grid
            int rr = 10 + (6 - p.life) * 6 + ahead(6, g), in = rr - 4, c = 4;
            for (int yy = -rr - c; yy <= rr; yy += c)
                for (int xx = -rr - c; xx <= rr; xx += c) {
                    int mx = xx + c / 2, my = yy + c / 2, d2 = mx * mx + my * my;
                    if (d2 <= rr * rr && d2 > in * in) fill(x + xx, y + yy, x + xx + c, y + yy + c, YELLOW);
                }
            break;
        }
        case P_BUBBLE: {                   // a ring, so she shows through it
            static const u16 RING[2] = {rgb(0x6F, 0xD6, 0xFF), INK};
            art(BUBBLE_ART, 8, "ch", RING, x - 8, y - 8, 2);
            break;
        }
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
static void wifi_glyph(int cx, int cy, int s, u16 c);

static void home_like(const Game& g, int state, const char* sub, bool night, int frame = -1, int dy = 0,
                      bool show_menu = true) {
    if (frame < 0) frame = g.frame;
    if (night) sky(); else backdrop(g);
    status(g);
    battery_icon(g, SCREEN_W - 52, 46);
    if (g.wifi_busy()) wifi_glyph(SCREEN_W - 84, 60, 2, g.wifi_stage >= WST_OK ? rgb(0x5E, 0xE0, 0x7A) : LILAC);   // setup under way
    ground(g, night);
    // messes sit on the floor beside her; during a bath they shrink away
    int n = g.scene == SC_WASH ? g.wash_messes : g.scene == SC_SLEEP ? 0 : g.messes();
    int size = 4;
    if (g.scene == SC_WASH) size = g.scene_ms >= 1600 ? 0 : 4 - (int)(g.scene_ms / 400);
    static const int mx[3] = {58, 312, 98};
    for (int i = 0; i < n; i++) mess(mx[i] + burn_x(g), 344 + burn_y(g), size);
    if (under) under(g, side_x(g), side_y(g) + dy);
    unicorn_side(g, state, night, frame, dy);
    if (!g.party) header(g, sub);          // over a high jump's crown; a party's bubble takes its place
    int sx = side_x(g), sy = side_y(g) + dy;
    if (state == M_HAPPY) { sparkle(sx + P(34), sy + P(52), 4, YELLOW); sparkle(sx + P(230), sy + P(42), 3, PINK); }
    if (state == M_HUNGRY || state == M_SAD)
        thought(sx + P(g.facing < 0 ? 10 : 196), sy + P(6),
                state == M_HUNGRY ? THINK_APPLE : g.stat(CLEAN) < NEED_LOW ? THINK_SOAP : THINK_RAIN);
    if (state == M_SLEEPY) zz(sx + P(220) + wave(now(g), 2000, 4), sy + P(32) - (int)(now(g) / 40 % 24));
    if (state == ST_SLEEP && !g.dream() && (g.scene != SC_LOUNGE || night)) zz(sx + P(220) + wave(now(g), 2600, 6), sy + P(118) - (int)(now(g) / 45 % 40));
    if (state == ST_EAT) {                 // today's treat in the bowl, eaten bite by bite
        static const int treats[TREAT_COUNT] = {IC_APPLE, IC_STRAWBERRY, IC_CARROT};
        int left = 3 - (int)(g.scene_ms * 3 / ACTION_MS);
        if (left > 0) icon(treats[g.treat], left, sx + 34 * SC + 3 - UP_ICON_W * left / 2,
                           sy + (36 + 4) * SC - UP_ICON_H * left);
    }
    particles(g, g.party ? 0 : HEADER_Y1);
    if (show_menu) menu(g);
}

// ---- eggs: one shell per character, in her coat colours with her own pattern ---------------

static void cracks(int x0, int y0, int scale, int stage, u16 ink);

// The line the shell breaks along: a jagged zigzag round the middle, the row per column where
// the lower half starts. The cracks that come before the hatch trace this same line.
static const u8 EGG_CUT[UP_EGG_W] = {12, 12, 13, 12, 11, 12, 13, 14, 13, 12, 11, 10, 11, 12, 13, 12, 11, 12, 13, 12};
enum { EGG_WHOLE, EGG_TOP, EGG_BOTTOM };

static void egg_draw(int c, int x0, int y0, int scale, int crack_stage, int part = EGG_WHOLE) {
    const u8* e = UP_EGG + c * UP_EGG_W * UP_EGG_H;   // all four shells share one silhouette
    auto in = [&](int x, int y) {          // this pixel is shell, and in the part being drawn
        if (x < 0 || y < 0 || x >= UP_EGG_W || y >= UP_EGG_H || !e[y * UP_EGG_W + x]) return false;
        return part == EGG_WHOLE || (part == EGG_TOP) == (y < EGG_CUT[x]);
    };
    u8 rim = e[1 * UP_EGG_W + UP_EGG_W / 2];   // the outline's colour, for the broken edge too
    for (int y = 0; y < UP_EGG_H; y++)
        for (int x = 0; x < UP_EGG_W; x++) {
            if (!in(x, y)) continue;
            u8 i = e[y * UP_EGG_W + x];
            bool edge = !in(x - 1, y) || !in(x + 1, y) || !in(x, y - 1) || !in(x, y + 1);
            bool broken = edge && e[y * UP_EGG_W + x] != rim && part != EGG_WHOLE;
            fill(x0 + x * scale, y0 + y * scale, x0 + (x + 1) * scale, y0 + (y + 1) * scale,
                 broken ? PAL[rim] : PAL[i]);
        }
    if (crack_stage > 0 && part == EGG_WHOLE) cracks(x0, y0, scale, crack_stage, PAL[UP_EGG_CRACK[c]]);
}

static void house_button() {                // top-right: back home (it answers from HOUSE_X0 / HOUSE_Y1)
    rrect(288, 10, 360, 82, 14, SEL, LILAC, 3);
    icon(IC_HOUSE, 5, 324 - UP_ICON_W * 5 / 2, 46 - UP_ICON_H * 5 / 2);
}

static void scene_egg_pick(const Game& g) {
    static const short EX[4] = {100, 268, 100, 268}, EY[4] = {170, 170, 310, 310};
    text(T(S_EGG_PICK), SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < 4; i++) {
        bool on = g.preview && g.sel == i;
        int lift = on && !g.calm() ? -4 + wave(now(g), 900, 3) : 0;
        if (on) rrect(EX[i] - 56, EY[i] - 64, EX[i] + 56, EY[i] + 64, 18, SEL, PAL[UP_ACCENT[i]], 3);
        egg_draw(i, EX[i] - UP_EGG_W * 2, EY[i] - UP_EGG_H * 2 + lift, 4, 0);
    }
    if (g.preview) {                       // the heart: "this one!"
        const u8* face = UP_FRONT + ((g.sel * UP_MOODS + M_HAPPY) * 2 + g.frame) * UP_FRONT_W * UP_FRONT_H;
        blit(face, UP_FRONT_W, UP_FRONT_H, 1, (SCREEN_W - UP_FRONT_W) / 2, 330, PAL);   // who is inside
        rrect(SCREEN_W / 2 - 36, 382, SCREEN_W / 2 + 36, 438, 14, SEL, PINK, 2);
        icon(IC_HEART, 5, SCREEN_W / 2 - UP_ICON_W * 5 / 2, 390);
        if (g.idle_ms > EGG_HINT_MS) hand_hint(g, SCREEN_W / 2 + 40, 420);
    } else if (g.idle_ms > EGG_HINT_MS) {
        // the fingertip (4 px left of x) on the first egg's lower right, tapping it as on the single-egg
        // screen. Under the egg it read as pointing at the egg below (44 px gap, 32 px hand, 14 px tap)
        hand_hint(g, EX[0] + 24, EY[0] + 26);
    }
}

// ---- bath foam -------------------------------------------------------------------------------

static void foam_blob(int cx, int cy, int r) {   // three lobes of suds, about 3r wide
    static const u16 SUDS[3] = {rgb(0xF0, 0xF8, 0xFF), rgb(0xC8, 0xE8, 0xFF), rgb(0xFF, 0xFF, 0xFF)};
    int s = r * 2 / 9 < 2 ? 2 : r * 2 / 9;
    art(FOAM, 9, "wsh", SUDS, cx - 7 * s, cy - 5 * s, s);
}

static void bath_foam(const Game& g) {
    static const short FX[3] = {104, 184, 264}, FY[3] = {190, 150, 190};
    if (g.foam_window())
        for (int i = 0; i < 3; i++) {
            if (g.foam_mask & (1 << i)) continue;
            int bob = g.calm() ? 0 : wave(now(g) + i * 300, 1400, 3);
            foam_blob(FX[i], FY[i] + bob, 22);
            if (i == g.foam_sel) {         // which one PWR would pop
                for (int a = 0; a < 64; a += 2) px(FX[i] + 34 * sin64(a) / 256, FY[i] + bob + 34 * sin64(a + 16) / 256, LILAC);
            }
        }
    if (g.foam_mask && g.scene_ms < CROWN_TO_MS) {   // the first popped puff became a silly crown
        int ax = UP_HEAD[ST_WASH][g.frame][0], ay = UP_HEAD[ST_WASH][g.frame][1];
        int cx = side_x(g) + ax * SC, cy = side_y(g) + ay * SC - SC;
        foam_blob(cx, cy, 16);
        foam_blob(cx + 14, cy - 10, 10);
    }
}

// ---- butterfly neighbour --------------------------------------------------------------------

static const char* const BFLY_OPEN[16] = {
    "........oo.oo........",
    "..........o..........",
    "..oooo....k....oooo..",
    ".owowoooo.k.oooowowo.",
    "oowaaaaaookooaaaaawoo",
    "owaabbbaavkvaabbbaawo",
    "ooaabbaavakavaabbaaoo",
    ".owaaaavaakaavaaaawo.",
    ".ooaaavaaakaaavaaaoo.",
    "..oooaaaaokoaaaaooo..",
    ".owoovaaaokoaaavoowo.",
    "oowaaavaaokoaavaaawoo",
    "owaaaaaaao.oaaaaaaawo",
    ".oowaaaao...oaaaawoo.",
    "..owoooo.....oooowo..",
    "...oo...........oo...",
};
static const char* const BFLY_UP[16] = {
    "........oo.oo........",
    "..........o..........",
    "..........k..........",
    "......ooo.k.ooo......",
    ".....owwookoowwo.....",
    ".....oabbakabbao.....",
    ".....oabbvkvbbao.....",
    "......oaavkvaao......",
    "......oavakavao......",
    "......ooaakaaoo......",
    ".......ovakavo.......",
    "......owaakaawo......",
    "......oaao.oaao......",
    ".......oo...oo.......",
    ".....................",
    ".....................",
};
static const char* const FEATHER[16] = {
    ".......oo.",
    "......oabo",
    ".....oaabo",
    "....oaaqbo",
    "....oaqbbo",
    "...oaaqbo.",
    "..o.oqbbo.",
    "..oaaqbo..",
    ".oaaqbbo..",
    ".oaaqbo...",
    ".o.oqbo...",
    "oaaqbbo...",
    "ooaqbo....",
    ".oqoo.....",
    ".q........",
    "q.........",
};

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
    // a monarch: orange cells between dark veins, a dark rim spotted white
    static const u16 COLS[6] = {rgb(0x2B, 0x18, 0x40), rgb(0x4A, 0x2E, 0x6B), rgb(0xFF, 0x8C, 0x2E),
                                rgb(0xFF, 0xC0, 0x4D), rgb(0x7A, 0x3A, 0x1A), rgb(0xF4, 0xEE, 0xFF)};
    art(open ? BFLY_OPEN : BFLY_UP, 16, "okabvw", COLS, x - 31, y - 24, 3);
}

// ---- Oyna's toy box and its modes -------------------------------------------------------------

static const char* const BALLOON[16] = {   // her hot-air balloon: gores in her colour, light and dark, and a basket
    "....waaaw....",
    "..awwaaawwa..",
    ".aawwaaawwaa.",
    "waawwaaawwaaw",
    "waawwaaawwaaw",
    "waawwaaawwaaw",
    "waawwaaawwaaw",
    ".aawwaaawwaa.",
    "..awwaaawwa..",
    "...wwaaaww...",
    "....waaaw....",
    "....r...r....",
    "....r...r....",
    "....bbbbb....",
    "....bdbdb....",
    ".....bbb.....",
};
static void balloon(const Game& g, int cx, int top, int s) {
    const u16 acc = PAL[UP_ACCENT[g.s.character]];
    const u16 cols[5] = {acc, (u16)((acc >> 1) & 0x7BEF), DIM, rgb(0xC8, 0x8A, 0x4A), rgb(0x8B, 0x5A, 0x3C)};
    art(BALLOON, 16, "awrbd", cols, cx - 13 * s / 2, top, s);
}

// Three to a row (the tilt games share the second), the way home below: 104 px tiles, clear of the round corners.
static void scene_toys(const Game& g) {
    static const int icons[TOY_COUNT] = {IC_STAR, IC_NOTE, IC_MIRROR, IC_CLOUD, IC_BALL, -1, IC_HOUSE};
    header(g, T(S_TOYS), true);
    for (int i = 0; i < TOY_COUNT; i++) {
        int cx = i == TOY_HOME ? SCREEN_W / 2 : 68 + (i % 3) * 116, cy = 148 + (i / 3) * 116;
        rrect(cx - 52, cy - 52, cx + 52, cy + 52, 18, SEL, i == g.sel ? PAL[UP_ACCENT[g.s.character]] : TRACK, 3);
        int scale = 6;
        if (i == g.press_item && (int)(g.press_end - (g.clock_ms + g.acc_ms)) > 0) scale = 7;
        int bob = (i == g.sel && !g.calm()) ? wave(now(g), 1200, 3) : 0;
        if (icons[i] < 0) balloon(g, cx, cy - 8 * (scale - 2) + bob, scale - 2);
        else icon(icons[i], scale, cx - UP_ICON_W * scale / 2, cy - UP_ICON_H * scale / 2 + bob);
    }
}

static void scene_music(const Game& g) {
    header(g, T(S_MUSIC), true);
    house_button();
    unicorn_front(g, g.echo_i ? M_HAPPY : M_IDLE, 22, 2);
    static const int petal_k[3] = {0, 2, 4};
    for (int i = 0; i < 3; i++) {
        int cx = 64 + i * 120, cy = 270;
        if (i == g.sel) rrect(cx - 48, cy - 56, cx + 48, cy + 74, 16, BLACK, LILAC, 2);   // under the flower
        u32 since = g.flower_at[i] == 0xFFFF ? 100000 : g.scene_ms - g.flower_at[i];
        int open = since < 100 ? 10 : since < 500 ? 22 : 18;          // closes, bursts open, settles
        if (g.calm()) open = since < 500 ? 20 : 18;
        static const u16 GREENS[3] = {rgb(0x5E, 0xE0, 0x7A), rgb(0x3A, 0xA8, 0x5A), rgb(0x5B, 0x2A, 0x86)};
        art(STEM, 14, "lmo", GREENS, cx - 16, cy + 6, 4);
        static const u16 SHADE[3] = {rgb(0xD0, 0x3A, 0x6A), rgb(0xE8, 0xB0, 0x30), rgb(0x2E, 0x9C, 0xC8)};
        bool yellow = petal_k[i] == 2;     // a yellow flower gets an orange heart, so it still shows
        u16 cols[5] = {rgb(0x5B, 0x2A, 0x86), RAINBOW[petal_k[i]], SHADE[i],
                       yellow ? rgb(0xFF, 0x9F, 0x43) : YELLOW, yellow ? rgb(0xE8, 0x6A, 0x3A) : rgb(0xFF, 0x9F, 0x43)};
        art(FLOWER[open <= 10 ? 0 : open >= 22 ? 2 : 1], 17, "opqce", cols, cx - 34, cy - 34, 4);
    }
    particles(g, HEADER_Y1);
    // dots for the notes she will sing back
    for (int i = 0; i < g.note_count; i++)
        ellipse(SCREEN_W / 2 - (g.note_count - 1) * 12 + i * 24, 360, 6, 6, RAINBOW[petal_k[g.notes[i]]]);
}

static void scene_cloud(const Game& g) {
    header(g, T(S_CLOUD), true);
    house_button();
    for (int i = 0; i < 3; i++) ellipse(64 + i * 120, CLOUD_Y + 50, 4, 4, TRACK);   // lane guides
    // the cloud, sliding smoothly toward where it is going
    int cx = g.cloud_x + ahead(g.cloud_vx, g);   // between steps too: 10 moves a second looked jerky
    if (cx < 64) cx = 64;
    if (cx > 304) cx = 304;
    static const u16 WHITE_CLOUD[2] = {rgb(0xF4, 0xF8, 0xFF), rgb(0xC9, 0xD8, 0xF0)};
    art(CLOUD, 11, "ws", WHITE_CLOUD, cx - 48, CLOUD_Y - 26, 4);
    int stacked = 0;
    for (int i = 0; i < FEATHERS; i++) {
        const Game::Feather& f = g.fea[i];
        int fx = f.x, fy = f.y;
        if (f.state == 0) continue;
        if (f.state == 1) { fy += ahead(FEATHER_FALL, g); fx += g.calm() ? 0 : wave(now(g) + i * 200, 1000, 8); }
        if (f.state == 2) { fx = cx - 30 + stacked * 12; fy = CLOUD_Y - 26; stacked++; }
        if (f.state == 3) fy = 382;
        static const u16 VANE[5][3] = {    // light, shade, outline: pink, sky, lemon, mint, lilac
            {rgb(0xFF, 0x9F, 0xC8), rgb(0xE0, 0x5C, 0x9A), rgb(0x7A, 0x2A, 0x5C)},
            {rgb(0x9E, 0xEC, 0xFF), rgb(0x4C, 0xC9, 0xF0), rgb(0x1E, 0x4A, 0x8A)},
            {rgb(0xFF, 0xEA, 0x88), rgb(0xFF, 0xB3, 0x47), rgb(0x8A, 0x4A, 0x10)},
            {rgb(0xA8, 0xF0, 0xB8), rgb(0x5E, 0xC8, 0x7A), rgb(0x1F, 0x5E, 0x3A)},
            {rgb(0xD8, 0xC8, 0xFF), rgb(0x9B, 0x6B, 0xFF), rgb(0x3A, 0x2C, 0x9C)}};
        const u16* v = VANE[i % 5];
        u16 cols[4] = {v[2], v[0], v[1], INK};
        art(FEATHER, 16, "oabq", cols, fx - 15, fy - 24, 3, i & 1);
    }
    particles(g, HEADER_Y1);
}

static void star_row(const Game& g) {       // what this round gathered: a row of little stars, never a number
    int n = g.landed > 8 ? 8 : g.landed;
    for (int i = 0; i < n; i++) icon(IC_STAR, 3, SCREEN_W / 2 - n * 16 + i * 32 + 2, 396);
}

static const char* const MARBLE[3][8] = {  // her ball's stripe upright, leaning and lying: it turns as it rolls
    {"..oooo..", ".oassao.", "oaassaao", "oaassaao", "oaassaao", "oaassaao", ".oassao.", "..oooo.."},
    {"..oooo..", ".oaaaso.", "oaaassao", "oaassaao", "oassaaao", "ossaaaao", ".osaaao.", "..oooo.."},
    {"..oooo..", ".oaaaao.", "oaaaaaao", "osssssso", "osssssso", "oaaaaaao", ".oaaaao.", "..oooo.."},
};

// Roll the ball: the plank leans as the toy does (or by itself, to the touched third), the ball rolls down it
// into the glowing hole, and the hole moves on. She watches from above and cheers.
static void scene_marble(const Game& g) {
    bool night = g.phase() == PH_NIGHT;
    if (night) stars();
    header(g, T(S_MARBLE), true);
    house_button();
    unicorn_front(g, g.hole_rest ? M_HAPPY : M_IDLE, 22, 2);
    ground(g, night);
    const int mid = SCREEN_W / 2, lean = g.lean;
    int bx = g.cloud_x + ahead(g.cloud_vx, g);   // gliding between steps, as the cloud does
    if (bx < MARBLE_X0) bx = MARBLE_X0;
    if (bx > MARBLE_X1) bx = MARBLE_X1;
    int by = MARBLE_Y + (bx - mid) * lean / 600 - 32, hy = MARBLE_Y + (g.hole_x - mid) * lean / 600;
    static const u16 cols[3] = {rgb(0x1E, 0x4A, 0x8A), rgb(0x4C, 0xC9, 0xF0), YELLOW};   // the toy box's ball
    int turn = (bx * 2 / 25) & 3;          // a quarter of the stripe's turn every 12 px rolled
    for (int pass = 0; pass < 2; pass++) {
        if (pass == (g.hole_rest ? 0 : 1)) {   // in the hole she sinks behind the plank; else she rolls in front
            art(MARBLE[turn == 3 ? 1 : turn], 8, "oas", cols, bx - 16, by + (g.hole_rest ? 12 : 0), 4, turn == 3);
            continue;
        }
        const u16 top = night ? rgb(0x8A, 0x6A, 0x5E) : rgb(0xD8, 0xA0, 0x6A), wood = night ? rgb(0x4E, 0x38, 0x3A) : rgb(0x8B, 0x5A, 0x3C);
        for (int k = 0; k < 7; k++) fill(mid - 4 - k * 4, MARBLE_Y + 12 + k * 4, mid + 4 + k * 4, MARBLE_Y + 16 + k * 4, wood);   // the stand
        for (int x = 16; x < 352; x += 8) {    // the plank in 8 px steps on the 4 px grid, a bumper at each end
            int y = MARBLE_Y + (x + 4 - mid) * lean / 600 / 4 * 4;
            bool end = x == 16 || x == 344;
            fill(x, y - (end ? 16 : 0), x + 8, y + 4, end ? wood : top);
            fill(x, y + 4, x + 8, y + 12, wood);
        }
        bool blink = !g.calm() && (now(g) / 300 & 1);
        ellipse(g.hole_x, hy + 2, 24, 8, blink ? rgb(0xFF, 0xB3, 0x47) : YELLOW);   // the glowing hole
        ellipse(g.hole_x, hy + 2, 18, 5, rgb(0x3A, 0x1E, 0x5A));
        if (!g.hole_rest) icon(IC_STAR, 3, g.hole_x - 13, hy - 66 + (g.calm() ? 0 : wave(now(g), 900, 4)));   // "in here"
    }
    star_row(g);
    particles(g, HEADER_Y1);
}

// An ellipse's rim only, w thick at the sides and h at the top, so the sky shows through: two spans a row.
static void hoop(int cx, int cy, int rx, int ry, int w, int h, u16 c) {
    if (off_band(cy - ry, cy + ry + 1)) return;
    const int ix = rx - w, iy = ry - h;
    for (int y = -ry; y <= ry; y++) {
        int xo = rx, xi = y < -iy || y > iy ? -1 : ix;
        while (xo >= 0 && xo * xo * ry * ry + y * y * rx * rx > rx * rx * ry * ry) xo--;
        while (xi >= 0 && xi * xi * iy * iy + y * y * ix * ix > ix * ix * iy * iy) xi--;
        if (xi < 0) { fill(cx - xo, cy + y, cx + xo + 1, cy + y + 1, c); continue; }
        fill(cx - xo, cy + y, cx - xi, cy + y + 1, c);
        fill(cx + xi + 1, cy + y, cx + xo + 1, cy + y + 1, c);
    }
}

// Balloon ride: the sky drifts by, rings with a star in them come down, and the tilt is the wind.
static void scene_balloon(const Game& g) {
    backdrop(g);
    static const u16 FAR[2] = {rgb(0x2E, 0x29, 0x4C), rgb(0x24, 0x20, 0x3E)};
    for (int k = 0; k < 2; k++)            // two far clouds sinking past: she is going up
        art(CLOUD, 11, "ws", FAR, k ? 236 : 20, 60 + (int)(((g.calm() ? 0 : now(g) / 25) + k * 200 + 90) % 400), 3);
    header(g, T(S_BALLOON), true);
    house_button();
    for (int i = 0; i < FEATHERS; i++) {
        const Game::Feather& f = g.fea[i];
        if (f.state != 1 && f.state != 3) continue;
        int y = f.y + ahead(FEATHER_FALL, g);
        if (y < HEADER_Y1 + 14) continue;
        u16 c = RAINBOW[i % 7];
        hoop(f.x, y, 44, 13, 8, 5, f.state == 3 ? (u16)((c >> 1) & 0x7BEF) : c);   // missed: it dims and drifts on
        icon(IC_STAR, 3, f.x - 13, y - 12 + (g.calm() ? 0 : wave(now(g) + i * 300, 800, 3)));
    }
    int cx = g.cloud_x + ahead(g.cloud_vx, g);
    if (cx < 64) cx = 64;
    if (cx > 304) cx = 304;
    balloon(g, cx, BALLOON_Y + (g.calm() ? 0 : wave(now(g), 2000, 3)), 4);
    star_row(g);
    particles(g, HEADER_Y1);
}

static void scene_dress(const Game& g) {
    static const int icons[ACC_COUNT] = {-1, IC_BOW, IC_FLOWER, IC_TIARA};
    header(g, T(S_MIRROR), true);
    house_button();
    ground(g, false);
    bool happy = g.dress_at && (int)(g.clock_ms - g.dress_at) < 700;
    acc_draw = g.preview;
    unicorn_side(g, happy ? M_HAPPY : M_IDLE, false, 0, 0);
    acc_draw = -1;
    for (int i = 0; i < ACC_COUNT; i++) {
        int x0 = 16 + i * 86;               // 4 x 78 + 3 x 8 = 336: centred, 16 px each side
        u16 line = i == g.s.accessory ? PAL[UP_ACCENT[g.s.character]] : i == g.preview ? LILAC : TRACK;
        rrect(x0, 352, x0 + 78, 430, 14, SEL, line, 3);
        if (icons[i] < 0) {                // "nothing": a ring with a slash, on the icons' 5 px grid
            static const char* const NONE[9] = {"..lllll..", ".l.....l.", "l.....l.l", "l....l..l", "l...l...l",
                                                "l..l....l", "l.l.....l", ".l.....l.", "..lllll.."};
            static const u16 NONE_COL[1] = {DIM};
            art(NONE, 9, "l", NONE_COL, x0 + 39 - 22, 391 - 22, 5);
        } else {
            icon(icons[i], 5, x0 + 39 - UP_ICON_W * 5 / 2, 391 - UP_ICON_H * 5 / 2);
        }
    }
    particles(g, HEADER_Y1);
}

// birthday cake: two tiers, icing, a candle per birthday (up to five), flickering flames
static void cake(const Game& g, int cx, int base) {
    u32 t = g.clock_ms + g.acc_ms;
    static const u16 COLS[9] = {rgb(0x5B, 0x2A, 0x86), rgb(0xFF, 0x8F, 0xB8), rgb(0xE8, 0x6A, 0x9A),
                                rgb(0xFF, 0xF0, 0xF6), rgb(0xC9, 0xB8, 0xFF), YELLOW, RAINBOW[3], RAINBOW[4], RAINBOW[0]};
    int x0 = cx - 50, y0 = base - 64;      // 25 x 17 on a 4 px grid, the plate on the table
    art(CAKE, 17, "oabcpygur", COLS, x0, y0, 4);
    int n = g.years();
    n = n < 1 ? 1 : n > 5 ? 5 : n;
    static const u16 FIRE[2] = {rgb(0xFF, 0x9F, 0x43), YELLOW};
    for (int i = 0; i < n; i++) {          // candles on the top tier, a flickering flame each
        int x = cx - 4 + (i * 2 - (n - 1)) * 8;
        fill(x - 4, y0 - 16, x, y0, RAINBOW[(i * 2) % 7]);   // two-tone, like a twisted candle
        fill(x, y0 - 16, x + 4, y0, INK);
        art(FLAME[(t / 120 + i) & 1], 4, "oy", FIRE, x - 6, y0 - 32, 4);
    }
}

// one picture per day she was cared for, in this order
static const int ALBUM_ICONS[ALBUM_PICS] = {IC_HEART, IC_APPLE, IC_STAR, IC_BUBBLE, IC_MOON, IC_BALL,
                                           IC_STRAWBERRY, IC_NOTE, IC_CLOUD, IC_FLOWER, IC_BOW, IC_TIARA};

// every book's picture for day i of that book: the same twelve, shuffled differently per book
static int album_icon(int book, int i) { return ALBUM_ICONS[(i + book * 5) % ALBUM_PICS]; }

// Things a trick conjures around her. The rainbow stands behind her (drawn through `under`);
// a star is tossed up in front of her nose, a ball bounces on her nose, magic rings her horn.
static int rainbow_grow(const Game& g, u32 t) {   // 0..256, or -1 when no rainbow shows
    if (g.idle_act != IDLE_RAINBOW && g.idle_act != IDLE_SHOW) return -1;
    int len = TRICK_MS[g.idle_act - IDLE_HUM];
    u32 q = t - (g.idle_end - len);
    if (g.idle_act == IDLE_SHOW) { if (q < (u32)len / 2) return -1; q -= len / 2; }
    return q > 600 ? 256 : (int)q * 256 / 600;
}

static u32 trick_t;                        // the frame's time, for the `under` hook
static const char* const PUFF[5] = {         // a little cloud at each foot of the rainbow
    "....www.....",
    "..wwwwww.ww.",
    ".wwwwwwwwwww",
    "wwwwwwwwwwww",
    ".ssssssssss.",
};
static void rainbow_behind(const Game& g, int sx, int sy) {
    (void)sy;
    int grow = rainbow_grow(g, trick_t);
    if (grow <= 0) return;
    // on her own pixel grid: six bands two pixels wide, swept in from the left like a brush stroke
    int c = SC, cx = sx + UP_SIDE_W * SC / 2, base = 340;
    int rx = P(176), ry = P(236), band = 2 * c;
    int th = grow * 32 / 256;              // the sweep so far, in sin64 phases (32 = half a turn)
    int ex = -sin64((u32)th + 16), ey = -sin64((u32)th);   // the sweep's leading edge, x256
    int left = cx - (rx + c - 1) / c * c;
    for (int y = base - c; y + c > HEADER_Y1 && y >= base - ry - c; y -= c)
        for (int x = left; x < cx + rx; x += c) {
            long long dx = x + c / 2 - cx, dy = y + c / 2 - base;
            if (ex * dy - ey * dx > 0) continue;               // not swept yet
            long long r2 = dx * dx + dy * dy * rx * rx / ((long long)ry * ry);
            for (int k = 0; k < 6; k++) {
                long long out = rx - k * band, in = out - band;
                if (r2 <= out * out && r2 > in * in) { fill(x, y < HEADER_Y1 ? HEADER_Y1 : y, x + c, y + c, RAINBOW[k]); break; }
            }
        }
    static const u16 PUFF_COLS[2] = {rgb(0xF4, 0xF8, 0xFF), rgb(0xC9, 0xD8, 0xF0)};
    int foot = rx - 3 * band;              // the middle of the bands
    art(PUFF, 5, "ws", PUFF_COLS, cx - foot - 6 * c, base - 4 * c, c);
    if (th >= 32) art(PUFF, 5, "ws", PUFF_COLS, cx + foot - 6 * c, base - 4 * c, c, true);
}

static void trick_props(const Game& g, int dy, u32 t) {
    int len = TRICK_MS[g.idle_act - IDLE_HUM];
    u32 q = t - (g.idle_end - len);
    bool flip = g.facing * turn < 0;
    auto at_x = [&](int col) { return side_x(g) + (flip ? UP_SIDE_W - 1 - col : col) * SC; };
    int nx = at_x(38), ny = side_y(g) + dy + 17 * SC;   // the tip of her nose
    int hx = at_x(34), hy = side_y(g) + dy + 4 * SC;    // the tip of her horn
    int ahead_x = flip ? -1 : 1;
    switch (g.idle_act) {
    case IDLE_JUGGLE: {                    // a star tossed up in front of her and caught, twice
        int toss = (int)(q % (len / 2));
        icon(IC_STAR, 3, nx + ahead_x * P(22) - 13, ny - P(20) + arc(toss, len / 2, P(46)));
        break;
    }
    case IDLE_BALANCE:                     // a ball bouncing on the tip of her nose
        icon(IC_BALL, 3, nx + ahead_x * P(4) - 13, ny - 24 + arc(q % 600, 600, P(40)));
        break;
    case IDLE_MAGIC: {                     // her horn glows and sparkles circle around it
        ellipse(hx, hy, P(9), P(9), YELLOW);
        for (int k = 0; k < 6; k++) {
            int a = (int)(q / 30) + k * 64 / 6;
            sparkle(hx + P(46) * sin64(a + 16) / 256, hy + P(26) * sin64(a) / 256 + P(10), 2, k & 1 ? PINK : INK);
        }
        break;
    }
    }
}

static void scene_home(const Game& g) {
    char day[64];                           // days she was cared for, not powered hours
    fmt(day, S_DAYS, g.s.care_days ? g.s.care_days : 1);
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
    } else if (g.idle_act >= IDLE_HUM) {                   // a trick she has learned
        int len = TRICK_MS[g.idle_act - IDLE_HUM];
        u32 q = t - (g.idle_end - len);
        switch (g.idle_act) {
        case IDLE_HUM:                                     // eyes closed, swaying to her own tune
        case IDLE_BUBBLES:
            state = g.idle_act == IDLE_HUM ? ST_BLINK : M_HAPPY;
            dy = wave(now(g), 600, 2) + 2;
            break;
        case IDLE_BOW:                                     // a deep bow, head down, and up again
            state = q > 150 && q < (u32)len - 200 ? ST_EAT : M_HAPPY;
            dy = 0;
            break;
        case IDLE_TWIRL:                                   // little hops, turning each time
            state = ST_HOP;
            dy = arc(q % 300, 300 - 60, 10);
            turn = (q / 300) & 1 ? -1 : 1;
            break;
        case IDLE_DANCE:                                   // two big hops, a turn in between
            state = ST_HOP;
            dy = arc(q % (len / 2), len / 2 - SQUASH_MS, 24);
            turn = q >= (u32)len / 2 ? -1 : 1;
            squash_now = squash_for(q % (len / 2), len / 2 - SQUASH_MS);
            break;
        case IDLE_RAINBOW:                                 // a hop, and a rainbow grows over her
            state = q < 500 ? ST_HOP : M_HAPPY;
            dy = q < 500 ? arc(q, 500 - SQUASH_MS, 14) : 0;
            break;
        case IDLE_SHOW:                                    // the finale: a twirl, then a big hop
            state = ST_HOP;
            if (q < (u32)len / 2) { dy = arc(q % 300, 300 - 60, 10); turn = (q / 300) & 1 ? -1 : 1; }
            else dy = arc(q - len / 2, len / 2 - SQUASH_MS, 30);
            break;
        default:                                           // juggle, balance, magic: she watches it
            state = M_HAPPY;
            dy = wave(now(g), 1200, 2) + 2;
            break;
        }
    }
    if (g.bedtime() == BED_DROWSY && !g.petting && !g.party && g.idle_act <= IDLE_BLINK) {
        state = (now(g) / 3000) % 3 == 2 ? ST_BLINK : M_SLEEPY;   // heavy eyes, now and then closed
        nudge = g.calm() ? 0 : wave(now(g), 2600, P(7));            // swaying on her feet
        dy = 2;
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
    bool trick = g.idle_act >= IDLE_HUM && !g.petting && !g.greeting && !g.party;
    trick_t = t;
    if (trick) under = rainbow_behind;
    home_like(g, state, day, false, frame, dy);
    under = 0;
    nudge = 0;
    if (trick) trick_props(g, dy, t);
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
        static const short MILE[MILESTONES] = {S_GREW, S_GREW, S_GREW, S_ONE_YEAR};
        const char* says = g.party == PARTY_REUNION ? T(S_MISSED) : g.party == PARTY_BIRTHDAY ? T(S_BIRTHDAY)
                         : T(MILE[g.party_arg]);
        bubble(side_x(g) + P(g.facing < 0 ? 40 : 200), 48, says);   // up top, clear of her head
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
        while (w > 0 && (long long)w * w * ry * ry + (long long)y * y * rx * rx > (long long)rx * rx * ry * ry) w--;
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
    if (g.standing) {                      // fell asleep on her feet: eyes shut, a slow sway, Zz
        nudge = g.calm() ? 0 : wave(now(g), 4200, P(4));
        home_like(g, ST_BLINK, T(S_ASLEEP_STANDING), true, 0, 2, !g.bed_locked());   // no menu that cannot answer
        int sx = side_x(g), sy = side_y(g);
        zz(sx + P(g.facing < 0 ? 0 : 220) + wave(now(g), 2600, 6), sy + P(40) - (int)(now(g) / 45 % 40));
        nudge = 0;
        return;
    }
    int dy = g.calm() ? 2 : wave(now(g), 3200, 2) + 2;
    home_like(g, ST_SLEEP, T(S_SLEEPING), true, 0, dy, !g.bed_locked());   // at bedtime no buttons that do nothing
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
    // on her own pixel grid: striped canvas, a stepped backrest at her head end, a wooden frame
    u16 cols[5] = {PAL[UP_ACCENT[g.s.character]], rgb(0xF4, 0xEE, 0xFF), rgb(0xC9, 0xBE, 0xFF),
                   rgb(0xB0, 0x78, 0x50), rgb(0x8B, 0x5A, 0x3C)};
    art(LOUNGER, 32, "awsbd", cols, sx - SC - (g.facing < 0 ? 5 * SC : 0), sy + 43 * SC - 28 * SC, SC, g.facing < 0);
}

static void scene_lounge(const Game& g) {
    int lift = P(30), dy = -lift + (g.calm() ? 0 : wave(now(g), 3600, 1));
    bool night = g.phase() == PH_NIGHT;
    under = lounger;
    home_like(g, ST_SLEEP, night ? T(S_LOUNGE_NIGHT) : T(S_LOUNGE_DAY), night, 0, dy);
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
    header(g, T(S_FEED_ASK));
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
    home_like(g, M_SLEEPY, T(S_TOO_TIRED), false, 0, g.calm() ? 0 : wave(now(g), 2400, 3) + 2, false);
    int pulse = g.calm() ? 0 : wave(now(g), 1200, 3);
    rrect(40 - pulse, 360 - pulse, 328 + pulse, 436 + pulse, 18, rgb(0x2A, 0x24, 0x5E), LILAC, 3);
    icon(IC_MOON, 5, SCREEN_W / 2 - UP_ICON_W * 5 / 2 - 40, 378);
    text(T(S_NAP), SCREEN_W / 2 + 40, 390, 2, INK);
}

// "07" style two-digit number into out (no libc)
static void two(int v, char* out) { out[0] = (char)('0' + v / 10 % 10); out[1] = (char)('0' + v % 10); out[2] = 0; }
// append, bounded by the destination array (a long word in some language is cut, never overflows)
template <int N> static void cat(char (&dst)[N], const char* src) {
    int k = 0;
    while (k < N - 1 && dst[k]) k++;
    while (k < N - 1 && *src) dst[k++] = *src++;
    dst[k] = 0;
}

// One row of a settings list (rows on the other half of a long list are skipped). The name is always big;
// the value sits on its right when both fit, else on a second line under it.
static void settings_row(const Game& g, int row, const char* name, const char* val, bool accent) {
    int n = g.set_rows();
    if (row != n - 1 && set_half(n, row) != set_half(n, g.set_row)) return;
    int y = SET_Y0 + set_row_slot(n, row) * SET_ROW_H, room = SCREEN_W - 64, vw = *val ? text_w(val, 2) : 0;
    if (row == g.set_row) rrect(16, y, SCREEN_W - 16, y + SET_ROW_H - 4, 10, SEL, LILAC, 2);
    u16 c = accent ? YELLOW : INK;
    int ns = text_w(name, 2) <= room ? 2 : 1;   // only a name too long for any line (a WiFi network's) is small
    if (text_w(name, ns) + (vw ? vw + 16 : 0) <= room || !vw) {
        text_clip(name, 32, y + (ns == 2 ? 13 : 18), ns, c, room);
        if (vw) text(val, SCREEN_W - 32 - vw, y + 13, 2, DIM, 0);
        return;
    }
    text_clip(name, 32, y + (ns == 2 ? 3 : 8), ns, c, room);
    int vs = vw <= room ? 2 : 1;           // only a value too long for a line of its own gets the small size
    text(val, SCREEN_W - 32 - text_w(val, vs), y + (vs == 2 ? 23 : 28), vs, DIM, 0);
}

// The "more" / "up" slot of a long list: a big arrow to the other half, and which half this is.
static void settings_pager(const Game& g) {
    int n = g.set_rows();
    if (n <= SET_SLOTS) return;
    int half = set_half(n, g.set_row), y = SET_Y0 + (half ? 0 : SET_SPLIT) * SET_ROW_H;
    rrect(16, y, SCREEN_W - 16, y + SET_ROW_H - 4, 10, BLACK, TRACK, 2);
    for (int i = 0; i < 12; i++) {         // a chevron: down to the rest, up to the start
        int w = half ? i : 11 - i;
        fill(SCREEN_W / 2 - w - 2, y + 16 + i, SCREEN_W / 2 + w + 2, y + 17 + i, LILAC);
    }
    text(half ? "2/2" : "1/2", SCREEN_W - 32 - text_w("1/2", 2), y + 13, 2, DIM, 0);
}

// SC_SETTINGS page 1: the date and time, and her birthday. Left half of a row: -1, right half: +1.
static void scene_clock(const Game& g) {
    static const short names[CLK_ROWS] = {S_YEAR, S_MONTH, S_DAY, S_HOUR, S_MINUTE, S_BMONTH, S_BDAY,
                                          S_SAVE, S_BACK};
    text(T(S_CLOCK_TITLE), SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < CLK_ROWS; i++) {
        char v[96] = "";
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
            cat(v, T(S_AFTER_HATCH));
        }
        settings_row(g, i, T(names[i]), v, i == CLK_SAVE);
    }
    settings_pager(g);
    text(T(S_HINT_ROWS), SCREEN_W / 2, 420, 1, DIM);
}

static void hhmm(int minute, char (&out)[8]) {   // minutes after midnight -> "19:30"
    char t[4];
    out[0] = 0;
    two(minute / 60, t); cat(out, t); cat(out, ":"); two(minute % 60, t); cat(out, t);
}

// SC_SETTINGS page 2: bedtime and wake-up time. Left half of a row: -10 min, right half: +10 min.
static void scene_bedtime(const Game& g) {
    static const short names[BEDR_ROWS] = {S_BED_RULE, S_BED_AT, S_WAKE_AT, S_WAKE_WE, S_BACK};
    text(T(S_BED_TITLE), SCREEN_W / 2, 30, 2, YELLOW);
    bool on = !(g.s.opts & OPT_NO_BEDTIME);
    for (int i = 0; i < BEDR_ROWS; i++) {
        char v[96] = "", t[8];
        if (i == BEDR_ON) cat(v, on ? T(S_ON) : T(S_OFF));
        if (i == BEDR_BED || i == BEDR_WAKE || i == BEDR_WAKE_WE) {
            hhmm(g.clock_min(i == BEDR_BED ? T_BED : i == BEDR_WAKE ? T_WAKE : T_WAKE_WE), t);
            cat(v, "-  "); cat(v, t); cat(v, "  +");
        }
        settings_row(g, i, T(names[i]), v, false);
    }
    const char* help[4] = {T(S_BED_HELP_1), T(S_BED_HELP_2),
                           T(S_BED_HELP_3), g.today().known ? "" : T(S_BED_NO_CLOCK)};
    for (int k = 0; k < 4; k++) text(help[k], SCREEN_W / 2, SET_Y0 + BEDR_ROWS * SET_ROW_H + 16 + k * 18, 1, DIM);
    text(T(S_HINT_BED), SCREEN_W / 2, 420, 1, DIM);
}

// ---- WiFi setup pages ------------------------------------------------------------------------

static const u16 OK_GREEN = rgb(0x5E, 0xE0, 0x7A), BAD_RED = rgb(0xFF, 0x5C, 0x6E), AMBER = rgb(0xFF, 0xB3, 0x47);

static void lines(const char* const* l, int n, int y, u16 c = DIM, int gap = 18) {
    for (int i = 0; i < n; i++) text(l[i], SCREEN_W / 2, y + i * gap, 1, c);
}

// two big buttons; sel: the highlighted one (-1: Game::wifi_sel)
static void wifi_buttons(const Game& g, const char* left, const char* right, int sel = -1) {
    const char* label[2] = {left, right};
    if (sel < 0) sel = g.wifi_sel;
    for (int i = 0; i < 2; i++) {
        if (!label[i]) continue;
        int x0 = i ? SCREEN_W / 2 + 6 : 16, x1 = i ? SCREEN_W - 16 : SCREEN_W / 2 - 6;
        rrect(x0, WBTN_Y0, x1, WBTN_Y1, 12, SEL, sel == i ? YELLOW : LILAC, sel == i ? 3 : 2);
        text_fit(label[i], (x0 + x1) / 2, (WBTN_Y0 + WBTN_Y1) / 2 - 8, 2, INK, x1 - x0);   // a long word: small
    }
}

static void wifi_glyph(int cx, int cy, int s, u16 c) {   // three arcs and a dot
    for (int k = 3; k >= 1; k--) {
        upper_half(cx, cy, k * 4 * s, k * 3 * s, c);
        upper_half(cx, cy, k * 4 * s - 2 * s, k * 3 * s - 2 * s, BLACK);
    }
    ellipse(cx, cy, s, s, c);
}

static void big_mark(int cx, int cy, int kind) {   // 0 tick (green), 1 cross (red), 2 clock (amber)
    u16 c = kind == 0 ? OK_GREEN : kind == 1 ? BAD_RED : AMBER;
    ellipse(cx, cy, 34, 34, c);
    ellipse(cx, cy, 28, 28, BLACK);
    for (int t = -2; t <= 2; t++) {
        if (kind == 0) { fill(cx - 16 + t, cy + t, cx - 6 + t, cy + 4 + t, c); for (int k = 0; k < 18; k++) fill(cx - 6 + k, cy + 6 - k + t, cx - 2 + k, cy + 10 - k + t, c); }
        if (kind == 1) for (int k = -12; k <= 12; k++) { fill(cx + k + t, cy + k, cx + k + t + 3, cy + k + 3, c); fill(cx + k + t, cy - k, cx + k + t + 3, cy - k + 3, c); }
    }
    if (kind == 2) { fill(cx - 2, cy - 18, cx + 2, cy + 2, c); fill(cx - 2, cy - 2, cx + 14, cy + 2, c); }
}

static void qr_draw(const Game& g, int cx, int top, int max_px) {   // black on white, 4-module quiet zone
    int n = g.qr_side, m = n + 8, px = max_px / m;
    if (!n || px < 1) return;
    int x0 = cx - m * px / 2;
    fill(x0, top, x0 + m * px, top + m * px, rgb(0xFF, 0xFF, 0xFF));
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (g.qr[y * n + x]) fill(x0 + (x + 4) * px, top + (y + 4) * px, x0 + (x + 5) * px, top + (y + 5) * px, BLACK);
}

// A newer firmware is waiting: a yellow dot just before a row's value (where settings_row draws it).
static void update_dot(const Game& g, int row, const char* name, const char* val) {
    int n = g.set_rows();
    if (!g.update_waiting() || (row != n - 1 && set_half(n, row) != set_half(n, g.set_row))) return;
    int y = SET_Y0 + set_row_slot(n, row) * SET_ROW_H, room = SCREEN_W - 64, vw = text_w(val, 2);
    bool one = text_w(name, 2) + vw + 16 <= room;
    ellipse(SCREEN_W - 32 - vw - 12, y + (one ? 20 : 30), 5, 5, YELLOW);
}

static void scene_hub(const Game& g) {
    text(T(S_HUB_TITLE), SCREEN_W / 2, 30, 2, YELLOW);
    static const short names[HUB_ROWS] = {S_DATE_TIME, S_WIFI, S_UPDATE, S_BACK};
    for (int i = 0; i < HUB_ROWS; i++) {
        char v[96] = "";
        if (i == HUB_CLOCK) {
            Cal c = g.today();
            if (!c.known) cat(v, T(S_SET_IT));
            else { char t[8]; two(c.mday, t); cat(v, t); cat(v, "."); two(c.month, t); cat(v, t); cat(v, " ");
                   two(c.minute / 60, t); cat(v, t); cat(v, ":"); two(c.minute % 60, t); cat(v, t); cat(v, " >"); }
        }
        if (i == HUB_WIFI) fmt(v, S_N_NETWORKS, g.wifi_count);
        if (i == HUB_UPDATE) fmt(v, S_V_N, g.fw_version);   // the running version; a dot: a newer one
        settings_row(g, i, T(names[i]), v, i == HUB_UPDATE && g.update_waiting());
        if (i == HUB_UPDATE) update_dot(g, i, T(names[i]), v);
    }
}

// The firmware update page: the running version, then what the platform reports (see Game::update_view).
static void scene_update(const Game& g) {
    text(T(S_UPDATE), SCREEN_W / 2, 30, 2, YELLOW);
    char line[96];
    fmt(line, S_VERSION_N, g.fw_version);
    text(line, SCREEN_W / 2, 64, 2, INK);
    int v = g.update_view();
    const char *right = 0, *title = "", *a = "", *b = "";
    int mark = -1;                         // big_mark kind, -1: none
    u16 tc = INK;
    switch (v) {
    case UST_IDLE:
        wifi_glyph(SCREEN_W / 2, 170, 6, LILAC);
        a = T(S_UPD_INFO); b = T(S_UPD_KEEPS); right = T(S_CHECK);
        break;
    case UST_CHECKING:
        wifi_glyph(SCREEN_W / 2, 170, 6, (g.clock_ms / 400) & 1 ? LILAC : DIM);
        title = T(S_UPD_CHECKING); tc = YELLOW;
        break;
    case UST_LATEST:
        mark = 0; title = T(S_UPD_LATEST); tc = OK_GREEN; right = T(S_CHECK);
        break;
    case UST_AVAILABLE:
        mark = 2; fmt(line, S_UPD_NEW, g.update_ver); title = line; tc = AMBER; b = T(S_UPD_KEEPS);
        if (g.update_allowed()) right = T(S_INSTALL);
        else {                             // the battery rule, explained
            text(T(S_UPD_CHARGE_1), SCREEN_W / 2, 296, 1, AMBER);
            text(T(S_UPD_CHARGE_2), SCREEN_W / 2, 316, 1, AMBER);
        }
        break;
    case UST_DOWNLOADING: case UST_VERIFYING: {
        int pct = v == UST_VERIFYING ? 100 : g.update_pct;
        title = T(v == UST_VERIFYING ? S_UPD_VERIFYING : S_UPD_DOWNLOADING); tc = YELLOW;
        rrect(40, 230, SCREEN_W - 40, 266, 10, BLACK, LILAC, 2);
        if (pct) fill(46, 236, 46 + (SCREEN_W - 92) * pct / 100, 260, YELLOW);
        char p[8];
        utoa(pct, p); cat(p, "%");
        text(p, SCREEN_W / 2, 280, 2, INK);
        text(T(S_UPD_DONT_OFF), SCREEN_W / 2, 330, 1, AMBER);
        break;
    }
    case UST_RESTARTING:
        mark = 0; title = T(S_UPD_RESTARTING); tc = OK_GREEN; b = T(S_UPD_KEEPS);
        break;
    case UST_NO_WIFI: mark = 1; title = T(S_NO_WIFI); tc = BAD_RED; b = T(S_UPD_CHECK_NETS); right = T(S_RETRY); break;
    case UST_FAILED:  mark = 1; title = T(S_UPD_FAILED); tc = BAD_RED; b = T(S_TIMEOUT_2); right = T(S_RETRY); break;
    default:          mark = 1; title = T(S_UPD_BAD_SIG); tc = BAD_RED; b = T(S_UPD_OLD_KEPT); right = T(S_RETRY); break;
    }
    if (mark >= 0) big_mark(SCREEN_W / 2, 150, mark);
    if (*title) text(title, SCREEN_W / 2, v == UST_DOWNLOADING || v == UST_VERIFYING ? 196 : 200, 2, tc);
    if (*a) text(a, SCREEN_W / 2, 236, 1, INK);
    if (*b) text(b, SCREEN_W / 2, 260, 1, DIM);
    if (v >= UST_DOWNLOADING && v <= UST_RESTARTING) return;   // nothing to press until it restarts
    wifi_buttons(g, T(S_BACK), right, right ? g.wifi_sel : 0);
}

static void scene_wifi(const Game& g) {
    text(T(S_WIFI), SCREEN_W / 2, 30, 2, YELLOW);
    int saved = 0;
    for (int i = 0; i < g.wifi_count; i++) saved += !(g.wifi_fixed & (1 << i));
    for (int i = 0; i < WF_ROWS; i++) {
        char v[96] = "";
        if (i == WF_LIST) { char t[8]; utoa(saved, t); cat(v, t); cat(v, "/5 >"); }
        settings_row(g, i, i == WF_ADD ? (g.wifi_busy() ? T(S_BACK_TO_SETUP) : T(S_ADD_NET)) : i == WF_LIST ? T(S_SAVED_NETS) : T(S_BACK),
                     v, i == WF_ADD);
    }
    wifi_glyph(SCREEN_W / 2, 270, 5, LILAC);
    const char* l[3] = {T(S_WIFI_INFO_1), T(S_WIFI_INFO_2),
                               T(S_WIFI_INFO_3)};
    lines(l, 3, 300);
}

static void scene_wstart(const Game& g) {
    text(T(S_PHONE_ADD), SCREEN_W / 2, 30, 2, YELLOW);
    rrect(SCREEN_W / 2 - 46, 64, SCREEN_W / 2 + 46, 210, 14, SEL, LILAC, 3);   // a phone, with a QR on it
    fill(SCREEN_W / 2 - 12, 72, SCREEN_W / 2 + 12, 75, LILAC);
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 7; x++)
            if (((x * 7 + y * 3) % 5) < 2 || (x < 2 && y < 2) || (x > 4 && y < 2) || (x < 2 && y > 4))
                fill(SCREEN_W / 2 - 28 + x * 8, 100 + y * 8, SCREEN_W / 2 - 21 + x * 8, 107 + y * 8, INK);
    const char* l[6] = {T(S_STEP_1), T(S_STEP_2A), T(S_STEP_2B),
                               T(S_STEP_3A), T(S_STEP_3B), T(S_ONLY_24)};
    for (int i = 0; i < 6; i++) text(l[i], 40, 232 + i * 22, 1, i == 5 ? AMBER : INK, 0);
    wifi_buttons(g, T(S_BACK), T(S_START));
}

static void scene_wsession(const Game& g) {
    int st = g.wifi_stage;
    if (st <= WST_STARTING) {
        text(T(S_PREPARING), SCREEN_W / 2, 30, 2, YELLOW);
        wifi_glyph(SCREEN_W / 2, 230, 8, (g.clock_ms / 400) & 1 ? LILAC : DIM);
        wifi_buttons(g, 0, T(S_BACK_TO_PLAY));
        return;
    }
    if (st == WST_WAIT || st == WST_PHONE) {
        text(T(S_SCAN_PHONE), SCREEN_W / 2, 26, 2, YELLOW);
        qr_draw(g, SCREEN_W / 2, 50, 236);
        char name[96] = "";
        cat(name, T(S_NET_IS));
        cat(name, g.wifi_ap);
        text(name, SCREEN_W / 2, 296, 1, INK);
        if (st == WST_PHONE) {
            text(T(S_PHONE_JOINED), SCREEN_W / 2, 320, 2, OK_GREEN);
            text(T(S_PICK_ON_PAGE), SCREEN_W / 2, 346, 1, DIM);
        } else {
            const char* l[2] = {T(S_NO_INTERNET_1), T(S_NO_INTERNET_2)};
            lines(l, 2, 322);
        }
        wifi_buttons(g, T(S_HELP), T(S_BACK_TO_PLAY));
        return;
    }
    if (st < WST_OK) {                     // three steps, the current one blinking
        text(T(S_CONNECTING), SCREEN_W / 2, 30, 2, YELLOW);
        text(g.wifi_net, SCREEN_W / 2, 64, 2, INK);
        const char* steps[3] = {T(S_JOINING), T(S_ADDRESS), T(S_SETTING_CLOCK)};
        for (int i = 0; i < 3; i++) {
            int y = 130 + i * 56, cur = st - WST_JOIN;
            u16 c = i < cur ? OK_GREEN : i == cur ? ((g.clock_ms / 300) & 1 ? YELLOW : INK) : TRACK;
            ellipse(70, y + 8, 12, 12, c);
            if (i < cur) { ellipse(70, y + 8, 7, 7, BLACK); ellipse(70, y + 8, 4, 4, c); }
            text(steps[i], 100, y, 2, i <= cur ? INK : DIM, 0);
        }
        wifi_buttons(g, 0, T(S_BACK_TO_PLAY));
        return;
    }
    const char *title = "", *a = "", *b = "";
    int mark = 1;
    switch (st) {
    case WST_OK:         title = T(S_CONNECTED); a = g.wifi_net; b = T(S_CLOCK_SET); mark = 0; break;
    case WST_OK_NOCLOCK: title = T(S_NET_SAVED); a = g.wifi_net; b = T(S_CLOCK_LATER); mark = 2; break;
    case WST_BAD_PASS:   title = T(S_CANT_CONNECT); a = T(S_CHECK_PASS_1); b = T(S_CHECK_PASS_2); break;
    case WST_NOT_FOUND:  title = T(S_NOT_FOUND); a = T(S_CHECK_RANGE_1); b = T(S_CHECK_RANGE_2); break;
    case WST_FULL:       title = T(S_LIST_FULL); a = T(S_FULL_1); b = T(S_FULL_2); mark = 2; break;
    default:             title = T(S_TIMED_OUT); a = T(S_TIMEOUT_1); b = T(S_TIMEOUT_2); mark = 2; break;
    }
    big_mark(SCREEN_W / 2, 140, mark);
    text(title, SCREEN_W / 2, 196, 2, mark == 0 ? OK_GREEN : mark == 1 ? BAD_RED : AMBER);
    text(a, SCREEN_W / 2, 236, mark == 1 || st >= WST_FULL ? 1 : 2, INK);
    text(b, SCREEN_W / 2, 264, 1, DIM);
    bool ok = st == WST_OK || st == WST_OK_NOCLOCK || st == WST_FULL;
    wifi_buttons(g, ok ? 0 : T(S_FINISH), ok ? T(S_OK) : T(S_RETRY));
}

static void scene_whelp(const Game& g) {
    text(T(S_BY_HAND), SCREEN_W / 2, 30, 2, YELLOW);
    char ap[96] = "", pw[96] = "";
    cat(ap, T(S_NET_IS)); cat(pw, T(S_PASS_IS));
    cat(ap, g.wifi_ap); cat(pw, g.wifi_pass);
    text(T(S_HAND_1A), 32, 80, 1, DIM, 0);
    text(T(S_HAND_1B), 32, 100, 1, DIM, 0);
    text(ap, 32, 128, 2, INK, 0);
    text(pw, 32, 156, 2, INK, 0);
    text(T(S_HAND_2), 32, 206, 1, DIM, 0);
    text("192.168.4.1", 32, 232, 3, YELLOW, 0);
    text(T(S_HAND_3), 32, 290, 1, DIM, 0);
    wifi_buttons(g, 0, T(S_BACK));
}

static void scene_wlist(const Game& g) {
    text(T(S_SAVED_NETS), SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < g.wifi_count; i++)
        settings_row(g, i, g.wifi_names[i], (g.wifi_fixed & (1 << i)) ? T(S_FIXED) : T(S_FORGET_MORE), false);
    settings_row(g, g.wifi_count, T(S_BACK), "", false);
    settings_pager(g);
    if (!g.wifi_count) text(T(S_NO_NETS), SCREEN_W / 2, 140, 1, DIM);
    text(T(S_FIXED_NOTE), SCREEN_W / 2, 420, 1, DIM);
}

static void scene_wforget(const Game& g) {
    text(T(S_FORGET_ASK), SCREEN_W / 2, 30, 2, YELLOW);
    wifi_glyph(SCREEN_W / 2, 170, 6, LILAC);
    if (g.wifi_arg < g.wifi_count) text_fit(g.wifi_names[g.wifi_arg], SCREEN_W / 2, 210, 2, INK);   // the whole name
    text(T(S_FORGET_NOTE), SCREEN_W / 2, 250, 1, DIM);
    wifi_buttons(g, T(S_BACK), T(S_FORGET));
}

// SC_SETTINGS: for grown-ups (BOOT: 3 quick presses, hold the last). Every value cycles on tap / PWR.
// ---- her daily routine -------------------------------------------------------------------------------
// Wordless picture cards, one per question (tools/routines.py order), on her coarse pixel grid.
static const char* const ART_BRUSH[13] = {
    "..........lll.", ".........lwwwl", "........lwwwl.", ".......lwwwl..", "......lwwwl...", ".....bbbbb....",
    "....bb........", "...bb.........", "..bb..........", ".bb...........", "bb............", "b.............",
    ".............."};
static const char* const ART_SHIRT[12] = {
    "...ppp..ppp...", ".pppppppppppp.", "pppppppppppppp", "ppp.pppppp.ppp", "pp..pppppp..pp", "....ppwwpp....",
    "....pwwwwp....", "....ppwwpp....", "....pppppp....", "....pppppp....", "....pppppp....", "....pppppp...."};
static const char* const ART_UNIFORM[12] = {
    "...bbw..wbb...", ".bbbbwwwwbbbb.", "bbbbbbrrbbbbbb", "bbb.bbrrbb.bbb", "bb..bbrrbb..bb", "....bbrrbb....",
    "....bbbrbb....", "....bbbbbb....", "....bbbbbb....", "....bbbbbb....", "....bbbbbb....", "....bbbbbb...."};
static const char* const ART_COMB[9] = {
    ".oooooooooooo.", "oooooooooooooo", "oooooooooooooo", "o.o.o.o.o.o.o.", "o.o.o.o.o.o.o.", "o.o.o.o.o.o.o.",
    "o.o.o.o.o.o.o.", "o.o.o.o.o.o.o.", ".............."};
static const char* const ART_PILL[8] = {
    "...rrrrwwww...", "..rrrrrwwwww..", ".rrhrrrwwwwww.", ".rhrrrrwwwwww.", ".rrrrrrwwwwww.", ".rrrrrrwwwwww.",
    "..rrrrrwwwww..", "...rrrrwwww..."};
static const char* const ART_EGG_PLATE[9] = {
    "....llllll....", "..llllllllll..", ".llllwwwwllll.", "lllwwwyywwwlll", "lllwwyyyywwlll", "lllwwwyywwwlll",
    ".llllwwwwllll.", "..llllllllll..", "....llllll...."};
static const char* const ART_GLASS[12] = {
    ".l..........l.", ".l..........l.", ".l..........l.", ".lbbbbbbbbbbl.", ".lbwbbbbbbbbl.", "..lbwbbbbbbl..",
    "..lbbbbbbbbl..", "..lbbbbbbbbl..", "..lbbbbbbbbl..", "...lbbbbbbl...", "...lbbbbbbl...", "....llllll...."};
static const char* const ART_DINNER[10] = {
    "k...llllll....", "k.llllllllll..", "kllnnnnllggll.", "kllnnnnlgggll.", "kllnnnnllglll.", "klllllloooll.",
    ".llllloooolll.", "..llllllllll..", "....llllll....", ".............."};
static const char* const ART_TOYBOX[12] = {
    "..rr.....y....", ".rrrr...yyy...", ".rhrr.yyyyyyy.", ".rrrr..yyyyy..", "..rr...yy.yy..", "nnnnnnnnnnnnnn",
    "noooooooooooon", "noooooooooooon", "noooonnnnooooon", "noooooooooooon", "noooooooooooon", "nnnnnnnnnnnnnn"};
static const char* const ART_TOILET[12] = {
    "wwwww.........", "wwwww.........", "wwwww.........", "wwwwwwwwwwwww.", "kkkkkkkkkkkkk.", ".wwwwwwwwwww..",
    "..wwwwwwwwww..", "...wwwwwwww...", "....wwwwww....", "....wwwwww....", "...wwwwwwww...", "..............."};
static const char* const ART_BATH[10] = {   // a tub of water, bubbles rising
    "..l....l...l..", ".l.l..l.l.l.l.", "..l....l...l..", "wwwwwwwwwwwwww", "wbbbbbbbbbbbbw", "wbbbbbbbbbbbbw",
    ".wbbbbbbbbbbw.", ".wwwwwwwwwwww.", "..k........k..", ".kk........kk."};
static const char* const ART_CRAYONS[8] = {   // the suggestions: crayons, bricks, a book, a doll
    "..r...b...y...", ".rrr.bbb.yyy..", ".rrr.bbb.yyy..", ".rrr.bbb.yyy..", ".rrr.bbb.yyy..", ".www.www.www..",
    ".rrr.bbb.yyy..", ".rrr.bbb.yyy.."};
static const char* const ART_BRICKS[9] = {
    ".y.y.y.y......", "yyyyyyyyy.....", "yyyyyyyyy.....", "..r.r.r.r.r...", ".rrrrrrrrrrr..", ".rrrrrrrrrrr..",
    "g.g.g.g.g.g.g.", "gggggggggggggg", "gggggggggggggg"};
static const char* const ART_BOOK[7] = {
    "bbbbbbbbbbbbbb", "bwwwwwbbwwwwwb", "bwkkkwbbwkkkwb", "bwwwwwbbwwwwwb", "bwkkkwbbwkkkwb", "bwwwwwbbwwwwwb",
    "bbbbbbbbbbbbbb"};
static const char* const ART_DOLL[12] = {
    "....yyyyyy....", "...yyyyyyyy...", "...ywwwwwwy...", "...wwkwwkww...", "...wwwwwwww...", "....wwppww....",
    ".....pppp.....", "...pppppppp...", "..pppppppppp..", "..pppppppppp..", "....ww..ww....", "....ww..ww...."};
static const char* const ART_EAR[12] = {
    "..pppppp..", ".pppppppp.", "ppp....ppp", "pp..pp..pp", "pp.p..p.pp", "....p..ppp", "....p.ppp.", "...pp.pp..",
    "...p..pp..", "..pp.pp...", ".ppppp....", "..ppp....."};

static const u16 QCOL[] = {INK, rgb(0x55, 0x4E, 0x7A), PINK, rgb(0xFF, 0x5C, 0x6E), rgb(0x5C, 0x9C, 0xFF), YELLOW,
                           rgb(0x5E, 0xE0, 0x7A), rgb(0xA8, 0xD8, 0xFF), rgb(0xFF, 0x9F, 0x43), rgb(0x9A, 0x5B, 0x34),
                           rgb(0xFF, 0xFF, 0xFF)};
static const char QKEYS[] = "wkprbyglonh";

// question q's picture centred on (cx, cy), s screen pixels per art pixel (the heart for the open one)
static void q_picture(int q, int cx, int cy, int s) {
    static const char* const* const ART[Q_HAPPY] = {ART_BRUSH, ART_BRUSH, ART_SHIRT, ART_UNIFORM, ART_COMB, ART_PILL,
                                                    ART_EGG_PLATE, ART_GLASS, ART_DINNER, ART_TOYBOX, ART_TOILET, ART_BATH};
    static const u8 H[Q_HAPPY] = {13, 13, 12, 12, 9, 8, 9, 12, 10, 12, 12, 10};
    static const char* const* const ACT_ART[ACT_COUNT] = {0, ART_CRAYONS, ART_BRICKS, ART_BOOK, ART_DOLL};
    static const u8 ACT_H[ACT_COUNT] = {0, 8, 9, 7, 12};
    if (q >= 0 && q < Q_HAPPY) { art(ART[q], H[q], QKEYS, QCOL, cx - 7 * s, cy - H[q] * s / 2, s); return; }
    if (int a = q >= 0 && q < Q_COUNT ? q_activity(q) : 0) { art(ACT_ART[a], ACT_H[a], QKEYS, QCOL, cx - 7 * s, cy - ACT_H[a] * s / 2, s); return; }
    int ic = q == Q_BASKETBALL || q == Q_VOLLEYBALL ? IC_BALL : IC_HEART;   // the sports: a ball; the open ones: a heart
    icon(ic, s * 14 / UP_ICON_W, cx - s * 7, cy - s * 6);
}

// SC_ASK: the card says what she asks, an ear while she listens, the big tick and cross when nobody can
// understand her, and a star for a yes.
static void scene_ask(const Game& g) {
    // an evening question under the stars, a day one on black; never the moon: it peeked out from behind
    // the card's left edge as a sliver (seen on the board), and the card's corner already says day or evening
    if (g.phase() != PH_DAY) stars();
    u32 t = now(g);
    int q = g.r_q;
    rrect(56, 24, SCREEN_W - 56, 264, 24, SEL, PAL[UP_ACCENT[g.s.character]], 4);
    q_picture(q, SCREEN_W / 2, 144, 11);
    icon(g.phase() == PH_DAY ? IC_STAR : IC_MOON, 3, SCREEN_W - 56 - 40, 36);   // a day or an evening question
    switch (g.r_stage) {
    case RS_ASK:                           // she is asking: three dots, one after another
        for (int k = 0; k < 3; k++) {
            int up = g.calm() ? 0 : arc((t + 1200 - k * 200) % 1200, 400, 10);
            ellipse(SCREEN_W / 2 - 40 + k * 40, 360 - up, 10, 10, k == (int)(t / 400 % 3) ? INK : DIM);
        }
        break;
    case RS_LISTEN: {                      // she listens: an ear, and the sound coming in
        art(ART_EAR, 12, QKEYS, QCOL, SCREEN_W / 2 + 4, 320, 6);
        for (int k = 0; k < 4; k++) {
            int h = 12 + (g.calm() ? 16 : wave(t + k * 230, 900, 18) + 18);
            fill(SCREEN_W / 2 - 80 + k * 18, 356 - h, SCREEN_W / 2 - 70 + k * 18, 356 + h, LILAC);
        }
        break;
    }
    case RS_BUTTONS:                       // yes | no
        for (int i = 0; i < 2; i++) {
            int x0 = i ? SCREEN_W / 2 + 6 : 16, x1 = i ? SCREEN_W - 16 : SCREEN_W / 2 - 6;
            rrect(x0, ASK_BTN_Y0, x1, SCREEN_H - 8, 18, SEL, g.r_sel == i ? YELLOW : LILAC, g.r_sel == i ? 4 : 2);
            big_mark((x0 + x1) / 2, (ASK_BTN_Y0 + SCREEN_H - 8) / 2, i);
        }
        break;
    case RS_REPLY: {
        int a = g.ans(q < Q_COUNT ? q : 0) >> 11;
        bool yes = a == ANS_YES || a == ANS_TAP_YES || (q < Q_COUNT && q_open(q) && a == ANS_SAID);
        if (yes && q < Q_COUNT && q_star(q)) {   // a star for today, and how many so far
            int pop = g.calm() ? 0 : arc(t % 1000, 1000, 10);
            icon(IC_STAR, 10, SCREEN_W / 2 - UP_ICON_W * 5, 318 - pop);
            char line[48];
            fmt(line, S_STARS_TODAY, g.stars_today());
            text_fit(line, SCREEN_W / 2, 412, 2, YELLOW);
        } else {
            icon(IC_HEART, 6, SCREEN_W / 2 - UP_ICON_W * 3, 340);
        }
        break;
    }
    }
    particles(g);
}

static void routine_help(const Game& g, int rows) {
    const char* help[3] = {T(S_RT_HELP_1), T(S_RT_HELP_2), T(S_RT_HELP_3)};
    (void)g;
    for (int k = 0; k < 3; k++) text_fit(help[k], SCREEN_W / 2, SET_Y0 + rows * SET_ROW_H + 16 + k * 18, 1, DIM);
}

// Settings > GÜNLÜK RUTİN
static void scene_routine(const Game& g) {
    static const short names[RT_ROWS] = {S_QUESTIONS, S_HOLIDAY, S_TV_TEST, S_HABITS, S_BACK};
    text(T(S_ROUTINE_ROW), SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < RT_ROWS; i++) {
        char v[96] = "";
        switch (i) {
        case RT_ON:      cat(v, !g.has_routines ? T(S_NO_PACK) : (g.s.opts & OPT_NO_ROUTINES) ? T(S_OFF) : T(S_ON)); break;
        case RT_HOLIDAY: cat(v, g.s.opts & OPT_HOLIDAY ? T(S_ON) : T(S_OFF)); break;
        case RT_TV:      cat(v, g.tv_on() ? T(S_ON) : T(S_OFF)); break;
        }
        settings_row(g, i, T(names[i]), v, false);
    }
    routine_help(g, RT_ROWS);
    text(T(S_HINT_MAIN), SCREEN_W / 2, 420, 1, DIM);
}

// Settings > GÜNLÜK RUTİN > BU HAFTA: a star for every yes, the last seven days (today on the right).
static void scene_habits(const Game& g) {
    text(T(S_HABITS_TITLE), SCREEN_W / 2, 24, 2, YELLOW);
    Cal c = g.today();
    for (int d = 0; d < 7; d++) {
        int x = 84 + d * 40;
        u32 day = c.known ? c.day - 6 + d : 0;
        char n[4] = "";
        if (c.known) two(calendar(day * 86400u).mday, n);
        text(n, x + 12, 50, 1, d == 6 ? INK : DIM);
        bool kept = c.known && day <= g.s.r_day && day + 7 > g.s.r_day;   // only days the week still holds
        for (int q = 0, row = 0; q < Q_COUNT; q++) {   // the habits only (a star is a yes to one of them)
            if (!q_star(q)) continue;
            int y = 66 + row++ * 30;
            if (d == 0) { q_picture(q, 32, y + 13, 2); fill(60, y + 28, SCREEN_W - 16, y + 29, TRACK); }
            if (kept && (g.s.week[day % 7] >> q & 1)) icon(IC_STAR, 2, x + 3, y + 6);
        }
    }
    char line[48];
    fmt(line, S_STARS_TODAY, g.stars_today());
    text_fit(line, SCREEN_W / 2, 404, 2, INK);
    text(T(S_BACK), SCREEN_W / 2, 428, 1, DIM);
}

static void scene_settings(const Game& g) {
    switch (g.set_page) {
    case PG_CLOCK:    scene_clock(g); return;
    case PG_BED:      scene_bedtime(g); return;
    case PG_HUB:      scene_hub(g); return;
    case PG_WIFI:     scene_wifi(g); return;
    case PG_WSTART:   scene_wstart(g); return;
    case PG_WSESSION: scene_wsession(g); return;
    case PG_WHELP:    scene_whelp(g); return;
    case PG_WLIST:    scene_wlist(g); return;
    case PG_WFORGET:  scene_wforget(g); return;
    case PG_UPDATE:   scene_update(g); return;
    case PG_ROUTINE:  scene_routine(g); return;
    case PG_HABITS:   scene_habits(g); return;
    }
    static const short names[SET_ROWS] = {S_VOLUME, S_SESSION, S_BREAK, S_LANGUAGE, S_CLOCK_ROW, S_SLEEP_ROW,
                                          S_BRIGHTNESS, S_CALM, S_SHAKE, S_ROUTINE_ROW, S_EXIT};
    static const short vols[3] = {S_NORMAL, S_LOW, S_OFF};
    text(T(S_SETTINGS), SCREEN_W / 2, 30, 2, YELLOW);
    for (int i = 0; i < SET_ROWS; i++) {
        char v[96] = "";
        switch (i) {
        case SET_VOLUME: cat(v, T(vols[g.volume()])); break;
        case SET_LIMIT:  fmt(v, S_MIN, g.play_limit_ms() / 60000); break;
        case SET_BREAK:  fmt(v, S_MIN, g.break_ms() / 60000); break;
        case SET_BRIGHT: utoa(g.brightness() * 100 / 255, v); cat(v, "%"); break;
        case SET_CALM:   cat(v, g.calm() ? T(S_ON) : T(S_OFF)); break;
        case SET_SHAKE:  cat(v, (g.s.opts & OPT_NO_SHAKE) ? T(S_OFF) : T(S_ON)); break;
        case SET_LANG:   cat(v, UP_LANG_NAME[LANG]); break;
        case SET_ROUTINE:                  // "AÇIK >", or no routines_tr.bin on the toy
            cat(v, !g.has_routines ? T(S_NO_PACK) : (g.s.opts & OPT_NO_ROUTINES) ? T(S_OFF) : T(S_ON)); cat(v, " >");
            break;
        case SET_SLEEP: {                  // "19:30 - 07:00 >"
            if (g.s.opts & OPT_NO_BEDTIME) { cat(v, T(S_OFF_MORE)); break; }
            char t[8];
            hhmm(g.clock_min(T_BED), t); cat(v, t); cat(v, " - "); hhmm(g.clock_min(T_WAKE), t); cat(v, t); cat(v, " >");
            break;
        }
        case SET_CLOCK: {                  // "31.10.2026 14:05 >" or a nudge to set it (and WiFi, behind it)
            Cal c = g.today();
            if (!c.known) { cat(v, T(S_SET_IT)); break; }
            char t[8];
            two(c.mday, t); cat(v, t); cat(v, ".");
            two(c.month, t); cat(v, t); cat(v, ".");
            utoa(c.year, t); cat(v, t); cat(v, " ");
            two(c.minute / 60, t); cat(v, t); cat(v, ":");
            two(c.minute % 60, t); cat(v, t); cat(v, " >");
            break;
        }
        }
        bool clock_row = i == SET_CLOCK && (!g.today().known || g.update_waiting());   // the update lives behind it
        settings_row(g, i, T(names[i]), v, i == SET_EXIT || clock_row);
        if (i == SET_CLOCK) update_dot(g, i, T(names[i]), v);
    }
    settings_pager(g);
    char line[64];
    if (g.batt_known) { fmt(line, S_BATTERY, g.battery); cat(line, "%"); }
    else { fmt(line, S_BATTERY, 0); for (int k = 0; line[k]; k++) if (line[k] == '0') { line[k] = '?'; break; } }
    text(line, SCREEN_W / 2, 412, 1, DIM);
    text(T(S_HINT_MAIN), SCREEN_W / 2, 428, 1, DIM);
}

// LEAVE / AWAY / RETURN: she goes out to play with friends; the away screen shows them at it.
static void act_yes(const Game& g, int t, int dy);   // or off to her own activity (below)
static void walker(const Game& g, const char* sub, const char* says) {
    bool walking = g.vel_x != 0, yes = g.scene == SC_LEAVE && g.s.away_act;
    u32 ph = now(g) / 120 % 4;
    int t = (int)now(g), state = walking ? (ph < 2 ? ST_WALK : ST_WALK2) : M_HAPPY, dy = 0;
    if (yes && !g.calm() && t >= 1200 && t < 1700) { state = ST_HOP; dy = arc(t - 1200, 500, 30); }   // "yes!"
    if (yes && !g.calm() && t >= 1200 && t < 1700 + SQUASH_MS) squash_now = squash_for(t - 1200, 500);
    if (yes) head_dy = t < 1200 ? 2 * seg(t, 600, 1000) / 256 : t < 1200 + FRIEND_MS ? wave(t, 700, 1) : 0;   // eyes them, then nods with her prize
    home_like(g, state, sub, false, walking ? (int)(ph & 1) : 0, dy, false);
    squash_now = 0;
    if (yes) act_yes(g, t, dy);
    head_dy = 0;
    if (!yes && g.scene == SC_LEAVE) {        // a friend comes to pick her up (drawn smaller: further back)
        int fc = (g.s.character + 1) % UP_CHARS, arrive = 1200 + FRIEND_MS;
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
        if (t >= arrive - 400 && t < arrive + 600) bubble(fx, 150, T(S_COME_ON));
    }
    if (says && side_x(g) < SCREEN_W - 120 && now(g) < 1200) bubble(side_x(g) + P(200), side_y(g) - P(6), says);
}

// ---- out with friends: short looping vignettes -------------------------------------------------
// Each one reads in a second and loops in 1-2.4 s: the screen is lit for 5 s only, and nothing here
// is a reason to keep tapping. All motion comes from now(g), so every band draws the same picture.

// a unicorn at play: character ch, side sprite, feet on y `feet`; she keeps her bow on out there.
// hdx/hdy/tdx move her head and tail tuft (rig, above); ignored in poses that are one piece.
static void playmate(const Game& g, int ch, int state, int frame, int x, int feet, int s, bool flip, bool night,
                     int hdx = 0, int hdy = 0, int tdx = 0) {
    const u8* spr = UP_SIDE + ((ch * UP_SIDE_STATES + state) * 2 + frame) * UP_SIDE_W * UP_SIDE_H;
    const u16* pal = night ? NIGHT : PAL;
    int top = feet - UP_SIDE_FEET * s;
    if (!standing(state)) hdx = hdy = tdx = 0;
    if (hdx | hdy | tdx) rig(spr, s, x, top, pal, flip, hdx, hdy, tdx);
    else blit(spr, UP_SIDE_W, UP_SIDE_H, s, x, top, pal, flip);
    int acc = g.s.accessory;
    if (ch != g.s.character || acc <= ACC_NONE || acc >= ACC_COUNT) return;
    int ax = UP_HEAD[state][frame][0] + hdx, ay = UP_HEAD[state][frame][1] + hdy, s4 = s * 2 / 3;
    if (flip) ax = UP_SIDE_W - 1 - ax;
    blit2(UP_ICONS + (IC_BOW + acc - 1) * UP_ICON_W * UP_ICON_H, UP_ICON_W, UP_ICON_H, s4, s4,
          x + ax * s - UP_ICON_W * s4 / 2, top + ay * s - UP_ICON_H * s4 + s * 8 / 6, pal, flip);
}
// her muzzle in a standing pose (sprite pixel 35, 21): where she holds and touches things
static int muzzle_x(int x, int s, int hdx, bool flip) { return x + (flip ? 4 - hdx : 35 + hdx) * s + s / 2; }
static int muzzle_y(int feet, int s, int hdy) { return feet - (UP_SIDE_FEET - 21 - hdy) * s; }
// a 160 ms blink every 3.3 s, out of step between friends (seed); only over a plain standing pose
static int blink(int state, u32 t, u32 seed) {
    return (state == M_IDLE || state == M_HAPPY) && (t + seed) % 3300 < 160 ? ST_BLINK : state;
}

// a friend watching from a step further back (smaller, feet a little higher), blinking now and then
static void onlooker(const Game& g, int ch, int x, int gy, u32 t) {
    playmate(g, ch, blink(M_IDLE, t, 1100), 0, x, gy - 16, 2, false, false);
}

static const u16 WOOD = rgb(0x8B, 0x5A, 0x3C), FRAME = rgb(0x4A, 0x30, 0x22);

// 0: catch! the ball flies nose to nose (1 s each way): the hitter dips, then flicks it up off her nose
// on the hit frame; the catcher watches it come, hops up and takes it on hers
static void away_ball(const Game& g, int me, int f1, int f2, u32 t, int gy) {
    u32 p = t % 2400;
    bool back = p >= 1200;                 // back: from the right to the left
    u32 q = back ? p - 1200 : p;
    int hop = g.calm() ? 0 : 22, bx = burn_x(g);
    int catching = q >= 800 ? arc(q - 800, 400, hop) : 0;
    int hit = 2 - 4 * seg(q, 150, 260) / 256 + 2 * seg(q, 400, 700) / 256;   // the hitter's head: dip, flick, settle
    int look = q >= 500 ? -2 + 2 * seg(q, 1000, 1200) / 256 : 0;             // the catcher's: up at the ball
    int lx = 24 + bx, rx = 224 + bx, lh = back ? look : hit, rh = back ? hit : look;
    onlooker(g, f2, 144 + bx, gy, t);
    playmate(g, me, back && catching ? ST_HOP : blink(M_HAPPY, t, 0), (t / 300) & 1, lx, gy + (back ? catching : 0),
             3, false, false, 0, lh);
    playmate(g, f1, !back && catching ? ST_HOP : blink(M_HAPPY, t, 1700), (t / 300) & 1, rx, gy + (back ? 0 : catching),
             3, true, false, 0, rh);
    int lnx = muzzle_x(lx, 3, 0, false) + 12, rnx = muzzle_x(rx, 3, 0, true) - 12, ny = muzzle_y(gy, 3, 0) - 6;   // just off each nose
    int x0 = back ? rnx : lnx, x1 = back ? lnx : rnx, x, y;
    if (q < 200) { x = x0; y = muzzle_y(gy, 3, hit) - 6; }      // on the nose through the wind-up
    else { int f = (int)((q - 200) * 256 / 1000); x = lerp(x0, x1, f); y = ny + arc(q - 200, 1000, g.calm() ? 40 : 70); }
    icon(IC_BALL, 4, x - UP_ICON_W * 2, y - UP_ICON_H * 2);
}

// 1: see-saw: a plank on a block tips over 2.4 s; the rider coming down sinks into the plank and ducks
// (her weight lands), the one going up lifts off it a little
static void away_seesaw(const Game& g, int me, int f1, int f2, u32 t, int gy) {
    int px = SCREEN_W / 2 + burn_x(g), py = gy - 50;
    u32 ph = t % 2400;
    int dy = wave(t, 2400, g.calm() ? 12 : 30);          // left end up by dy, right end down
    onlooker(g, f2, px - 40, gy, t);
    for (int k = 0; k < 9; k++) fill(px - 6 - k * 5, py + 6 + k * 5, px + 6 + k * 5, py + 11 + k * 5, FRAME);
    for (int i = 0; i <= 40; i++) {
        int x = px - 120 + i * 6, y = py - dy + 2 * dy * i / 40;
        fill(x, y - 3, x + 6, y + 3, WOOD);
    }
    int sinkR = ph >= 300 && ph < 900 ? -arc(ph - 300, 600, 3) : 0, liftL = ph >= 300 && ph < 900 ? arc(ph - 300, 600, 6) : 0;
    int sinkL = ph >= 1500 && ph < 2100 ? -arc(ph - 1500, 600, 3) : 0, liftR = ph >= 1500 && ph < 2100 ? arc(ph - 1500, 600, 6) : 0;
    bool up = dy > 6, down = dy < -6;
    playmate(g, me, up ? ST_HOP : blink(M_HAPPY, t, 0), (t / 400) & 1, px - 160, py - dy - 3 + sinkL + liftL, 2, false, false,
             0, sinkL > 2 ? 2 : sinkL);
    playmate(g, f1, down ? ST_HOP : blink(M_HAPPY, t, 1500), (t / 400) & 1, px + 80, py + dy - 3 + sinkR + liftR, 2, true, false,
             0, sinkR > 2 ? 2 : sinkR);
}

// 2: soap bubbles: a friend blows them, they drift up and grow, and she hops after them
static void away_bubbles(const Game& g, int me, int f1, int f2, u32 t, int gy) {
    int bx = burn_x(g);
    onlooker(g, f2, 150 + bx, gy, t);
    int fr = (t / 400) & 1;
    playmate(g, f1, blink(M_HAPPY, t, 900), fr, 20 + bx, gy, 3, false, false);
    int hop = g.calm() ? 0 : arc(t % 1200, 600, 24);
    playmate(g, me, hop ? ST_HOP : blink(M_HAPPY, t, 0), fr, 228 + bx, gy + hop, 3, true, false);
    static const u16 RING[2] = {rgb(0x6F, 0xD6, 0xFF), INK};
    int mx = 20 + bx + (UP_HEAD[M_HAPPY][fr][0] + 6) * 3, my = gy - UP_SIDE_FEET * 3 + (UP_HEAD[M_HAPPY][fr][1] + 8) * 3;
    for (int k = 0; k < 5; k++) {
        u32 p = (t + k * 480) % 2400;
        int s = 1 + (int)(p * 3 / 2400);
        int x = mx + (int)(p * 150 / 2400) + wave(t + k * 500, 1100, 7), y = my - (int)(p * 170 / 2400);
        art(BUBBLE_ART, 8, "ch", RING, x - 4 * s, y - 4 * s, s);
    }
}

// 3: skipping rope: two friends swing it (1 s a turn), heads bobbing with it; she crouches as it comes
// round, jumps as it passes under her feet, and lands with a dip
static void away_rope(const Game& g, int me, int f1, int f2, u32 t, int gy) {
    int bx = burn_x(g), fr = (t / 500) & 1;
    u32 ph = (t + 250) % 1000;             // 0..500: in the air
    int v = sin64(ph * 64 / 1000);         // +256: the rope is down in front, -256: up behind
    int sag = v > 0 ? 56 * v / 256 : 112 * v / 256;
    int hop = arc(ph, 500, g.calm() ? 10 : 44), swing = v > 0 ? 1 : 0;
    playmate(g, f1, blink(M_HAPPY, t, 0), fr, 16 + bx, gy, 2, false, false, 0, swing);
    playmate(g, f2, blink(M_HAPPY, t, 1600), fr, 272 + bx, gy, 2, true, false, 0, swing);
    auto rope = [&]() {
        for (int i = 0; i <= 52; i++) {
            int x = 80 + bx + i * 4, y = gy - 58 + sag * 4 * i * (52 - i) / (52 * 52);
            fill(x, y - 2, x + 4, y + 2, INK);
        }
    };
    int crouch = ph >= 850 ? 3 * seg(ph, 850, 1000) / 256 : ph < 650 ? 2 - 2 * seg(ph, 500, 650) / 256 : 0;
    if (g.calm()) crouch = (crouch + 1) / 2;
    if (v <= 0) rope();
    playmate(g, me, hop ? ST_HOP : M_HAPPY, fr, 124 + bx, gy + hop, 3, false, false, 0, crouch);
    if (v > 0) rope();
}

// 4: a butterfly flits between them in a figure of eight; both follow it with their heads (straight up
// when it is over them, forward when it is across), and each hops when it comes near
static void away_butterfly(const Game& g, int me, int f1, int f2, u32 t, int gy) {
    int bx = burn_x(g), hop = g.calm() ? 0 : 26;
    int lh = arc(t % 2400, 500, hop), rh = arc((t + 1200) % 2400, 500, hop);
    int x = SCREEN_W / 2 + bx - wave(t + 600, 2400, 110), y = 170 + wave(t, 1200, 24) + wave(t, 400, 6);
    onlooker(g, f2, 144 + bx, gy, t);
    playmate(g, me, lh ? ST_HOP : blink(M_HAPPY, t, 0), (t / 400) & 1, 24 + bx, gy + lh, 3, false, false,
             x > 150 + bx, x > 220 + bx ? -1 : -2);
    playmate(g, f1, rh ? ST_HOP : blink(M_HAPPY, t, 1900), (t / 400) & 1, 224 + bx, gy + rh, 3, true, false,
             x < 210 + bx, x < 140 + bx ? -1 : -2);
    bool open = g.calm() || ((t / 120) & 1);
    static const u16 COLS[6] = {rgb(0x2B, 0x18, 0x40), rgb(0x4A, 0x2E, 0x6B), rgb(0xFF, 0x8C, 0x2E),
                                rgb(0xFF, 0xC0, 0x4D), rgb(0x7A, 0x3A, 0x1A), rgb(0xF4, 0xEE, 0xFF)};
    art(open ? BFLY_OPEN : BFLY_UP, 16, "okabvw", COLS, x - 31, y - 24, 3);
}

// night: fireflies drift and blink over the meadow; they watch, one friend has dozed off. No hops.
static void away_fireflies(const Game& g, int me, int f1, int f2, u32 t, int gy) {
    int bx = burn_x(g);
    playmate(g, f2, ST_SLEEP, 0, 144 + bx, gy - 12, 2, false, true);
    playmate(g, me, (t / 1500) % 4 == 3 ? ST_BLINK : M_IDLE, 0, 24 + bx, gy, 3, false, true);
    playmate(g, f1, (t / 1500 + 2) % 4 == 3 ? ST_BLINK : M_IDLE, 0, 224 + bx, gy, 3, true, true);
    static const u16 GLOW = rgb(0x7A, 0x6E, 0x22), DARK = rgb(0x4A, 0x44, 0x1E);
    for (int i = 0; i < 7; i++) {
        int x = 44 + i * 44 + wave(t + i * 700, 2600 + i * 200, 24), y = 150 + (i % 3) * 36 + wave(t + i * 900, 1900 + i * 150, 14);
        x -= x % 3; y -= y % 3;
        if ((t / 300 + i * 2) % 5 < 3) { fill(x - 3, y - 3, x + 6, y + 6, GLOW); fill(x, y, x + 3, y + 3, YELLOW); }
        else fill(x, y, x + 3, y + 3, DARK);
    }
}

// ---- her own break: she said yes to colouring, bricks, a book or her dolls --------------------
// The same props serve the "yes!" moment (walker: she grabs one and walks off with it) and the away
// vignette (scene_away: her at it, instead of the friends' games). away_pick varies each look.

static const u16 PAGE_INK = rgb(0x55, 0x4E, 0x7A), SKIN = rgb(0xFF, 0xD9, 0xB8), HAIR = rgb(0x5A, 0x33, 0x1E);
static inline u16 shade(u16 c) { return (c >> 1) & 0x7BEF; }   // the same colour, half as bright

static const char* const CRAYON[10] = {".c.", "ccc", "ccc", "www", "ccc", "ccc", "ccc", "ccc", "www", "ccc"};
static const char* const DOLL[13] = {
    ".hhhhh.", "hhhhhhh", "hfffffh", "hfefefh", ".fffff.", "..ddd..", ".ddddd.", ".ddddd.", "ddddddd",
    "ddddddd", "..f.f..", "..f.f..", ".ss.ss."};

static void crayon(int x, int y, int s, u16 c) {   // top-centre at (x, y)
    const u16 cols[2] = {c, INK};
    art(CRAYON, 10, "cw", cols, x - 3 * s / 2, y, s);
}
static void crayon_box(int x, int gy, int v) {     // four crayons standing in a box, bottom-centre at (x, gy)
    for (int k = 0; k < 4; k++) crayon(x - 18 + k * 12, gy - 36, 3, RAINBOW[(k * 2 + v) % 7]);
    fill(x - 27, gy - 18, x + 27, gy, WOOD);
    fill(x - 27, gy - 18, x + 27, gy - 15, FRAME);
}
static void brick(int x, int y, int w, u16 c) {    // a toy brick, top-left (x, y) under two studs
    fill(x, y - 3, x + w / 2 - 6, y, c); fill(x + w / 2 + 3, y - 3, x + w - 3, y, c);   // studs
    fill(x, y, x + w, y + 12, c);
    fill(x, y + 9, x + w, y + 12, shade(c));
}
static void doll(int x, int gy, u16 dress) {       // a little doll, bottom-centre at (x, gy)
    const u16 cols[5] = {HAIR, SKIN, PAGE_INK, dress, shade(dress)};
    art(DOLL, 13, "hfeds", cols, x - 10, gy - 39, 3);
}
static void book_closed(int x, int y, u16 cover) { // top-left (x, y), 36 x 27
    fill(x, y, x + 36, y + 27, cover);
    fill(x + 30, y + 3, x + 36, y + 24, INK);       // the page edges
    fill(x, y, x + 3, y + 27, shade(cover));        // the spine
}

// each held thing hangs from its grab point (x, y): what she carries in her mouth
static void held(int act, int x, int y, int v) {
    switch (act) {
    case ACT_COLOR: crayon(x, y, 3, RAINBOW[(v * 2) % 7]); break;
    case ACT_LEGO:  brick(x - 18, y + 3, 36, RAINBOW[(v + 1) % 7]); break;
    case ACT_READ:  book_closed(x - 18, y, RAINBOW[(v * 3 + 4) % 7]); break;
    default:        doll(x, y + 39, RAINBOW[(v * 2) % 7]); break;
    }
}
// what waits on the floor (the yes moment), bottom-centre at (x, gy); taken: she has her piece of it
static void act_set(int act, int x, int gy, int v, bool taken) {
    switch (act) {
    case ACT_COLOR: crayon_box(x, gy, v); break;
    case ACT_LEGO:
        brick(x - 18, gy - 12, 36, RAINBOW[(v + 3) % 7]); brick(x + 6, gy - 12, 30, RAINBOW[(v + 5) % 7]);
        if (!taken) brick(x - 12, gy - 27, 36, RAINBOW[(v + 1) % 7]);
        break;
    case ACT_READ:  if (!taken) book_closed(x - 18, gy - 27, RAINBOW[(v * 3 + 4) % 7]); break;
    default:        if (!taken) doll(x - 14, gy, RAINBOW[(v * 2) % 7]); doll(x + 14, gy, RAINBOW[(v * 2 + 3) % 7]); break;
    }
}

// SC_LEAVE, "yes!": the things wait at the right; at 1.2 s she hops and takes one up in her mouth,
// sparkles, and from 1200 + FRIEND_MS step() walks her off with it. (A hug of hearts comes from step().)
static void act_yes(const Game& g, int t, int dy) {
    int v = g.away_pick % 3, gx = 330 + burn_x(g), gy = 346 + burn_y(g);
    int mx = side_x(g) + P(210), my = side_y(g) + dy + P(126) + (squash_now ? 0 : head_dy) * SC;   // her mouth (sprite px 35, 21)
    act_set(g.s.away_act, gx, gy, v, t >= 1200);
    if (t < 1200) return;
    int p = seg(t, 1200, 1700);
    int x = lerp(gx, mx, p), y = lerp(gy - 39, my, p) + arc(p, 256, 40);
    held(g.s.away_act, x, y, v);
    if (t >= 1700 && t < 1200 + FRIEND_MS) {
        sparkle(mx + 30 + wave(t, 700, 6), my - 20, 3, YELLOW);
        sparkle(mx - 10, my + 40 + wave(t + 350, 700, 6), 3, PINK);
    }
}

// a crayon held in the mouth: a thick stick from (x0, y0) to its tip at (x1, y1)
static void stick(int x0, int y0, int x1, int y1, u16 c) {
    for (int k = 0; k <= 20; k++) { int x = lerp(x0, x1, k * 256 / 20), y = lerp(y0, y1, k * 256 / 20); fill(x - 2, y - 2, x + 3, y + 3, c); }
}

// SC_AWAY, colouring: she stands at a low easel, the crayon in her mouth, and a drawing (sun, house or
// flower) comes stroke by stroke: the crayon's tip runs along each line as it appears, her head dipping
// into every stroke. Eight strokes at 300 ms, then she lifts her head and the picture sparkles (1.5 s).
static void act_color(const Game& g, int me, u32 t, int gy, bool night) {
    struct Stroke { i8 x0, y0, x1, y1; u8 c; };   // on the sheet, c: RAINBOW index
    static const Stroke SUN[8] = {{21, 15, 39, 33, 2}, {27, 3, 33, 12, 2}, {27, 36, 33, 45, 2}, {6, 21, 18, 27, 2},
                                  {42, 21, 54, 27, 2}, {12, 9, 18, 15, 1}, {42, 9, 48, 15, 1}, {12, 33, 18, 39, 1}};
    static const Stroke HOUSE[8] = {{15, 24, 45, 48, 3}, {24, 15, 36, 24, 0}, {18, 21, 42, 24, 0}, {12, 24, 48, 27, 0},
                                    {27, 36, 33, 48, 4}, {18, 30, 24, 36, 5}, {36, 30, 42, 36, 5}, {0, 48, 60, 54, 3}};
    static const Stroke FLOWER[8] = {{27, 27, 33, 51, 3}, {33, 36, 42, 42, 3}, {24, 9, 36, 15, 0}, {15, 18, 24, 30, 0},
                                     {36, 18, 45, 30, 0}, {24, 33, 36, 39, 0}, {27, 18, 33, 30, 2}, {0, 48, 60, 54, 3}};
    static const Stroke* const PICS[3] = {SUN, HOUSE, FLOWER};
    const Stroke* pic = PICS[g.away_pick % 3];
    int bx = burn_x(g), ex = 196 + bx, ey = gy - 100;             // the easel board's top-left
    for (int k = 0; k < 2; k++) fill(ex + 6 + k * 54, ey + 60, ex + 12 + k * 54, gy, FRAME);   // legs
    fill(ex, ey, ex + 72, ey + 66, WOOD);
    fill(ex + 6, ey + 6, ex + 66, ey + 60, INK);                  // the sheet
    u32 p = t % 3900;
    int n = (int)(p / 300), q = seg(p % 300, 30, 270);            // strokes done, and how far the next has got
    int tx = 0, ty = 0;                                           // the crayon's tip on the sheet
    for (int k = 0; k < 8 && k <= n; k++) {
        const Stroke& st = pic[k];
        bool along = st.x1 - st.x0 >= st.y1 - st.y0;              // a stroke grows along its longer side
        int x1 = st.x1, y1 = st.y1;
        if (k == n) {
            if (along) x1 = st.x0 + (st.x1 - st.x0) * q / 256; else y1 = st.y0 + (st.y1 - st.y0) * q / 256;
            tx = ex + 6 + (along ? x1 : (st.x0 + st.x1) / 2); ty = ey + 6 + (along ? (st.y0 + st.y1) / 2 : y1);
        }
        fill(ex + 6 + st.x0, ey + 6 + st.y0, ex + 6 + x1, ey + 6 + y1, RAINBOW[st.c]);
    }
    crayon_box(300 + bx, gy, g.away_pick % 3);
    bool done = n >= 8;
    int ux = 76 + bx, hdx = done ? 0 : 1, hdy = done ? -1 : 2 - 2 * seg(p % 300, 40, 260) / 256;   // dips into each stroke
    playmate(g, me, blink(done ? M_HAPPY : M_IDLE, t, 0), (t / 500) & 1, ux, gy, 3, false, night, hdx, hdy);
    int mx = muzzle_x(ux, 3, hdx, false), my = muzzle_y(gy, 3, hdy);
    if (!done) stick(mx, my, tx, ty, RAINBOW[pic[n].c]);
    else {                                                        // head up, crayon up: it is finished
        stick(mx, my, mx + 24, my - 22, RAINBOW[pic[7].c]);
        sparkle(ex + 18 + wave(t, 900, 8), ey + 14, 3, YELLOW);
        sparkle(ex + 54, ey + 40 + wave(t + 400, 900, 6), 3, PINK);
    }
}

// SC_AWAY, bricks: the tower stands under her nose, the pile beyond it. She dips to the pile and a
// brick comes up to her mouth, she lifts it over and sets it down: the tower grows the moment it lands.
// One brick every 2.4 s; after the fourth the tower is done (sparkles, a hop unless calm or at night)
// and a new one starts. anim_ms runs for the whole break, so the start height changes with each look.
static void act_lego(const Game& g, int me, u32 t, int gy, bool night) {
    int bx = burn_x(g), v = g.away_pick % 3, ux = 40 + bx, tx = 150 + bx, px = 196 + bx;
    int round = (int)(t / 2400 % 4), n = 2 + v + round;         // bricks standing as this one starts
    u32 p = t % 2400;
    bool placed = p >= 1500, quiet = g.calm() || night;
    for (int k = 0; k < n + placed; k++) brick(tx, gy - 12 * (k + 1), 36, RAINBOW[(k + v) % 7]);
    int top = gy - 12 * (n + 1), reach = (60 - 12 * (n + 1)) / 8;   // the head follows the brick down or up to the top
    if (reach > 3) reach = 3; else if (reach < -3) reach = -3;
    int hdy;
    if (p < 700) hdy = 2 * seg(p, 0, 350) / 256 - 2 * seg(p, 350, 700) / 256;
    else if (p < 1000) hdy = -seg(p, 700, 900) / 256;
    else if (p < 1500) hdy = -1 + (reach + 1) * seg(p, 1000, 1500) / 256;
    else hdy = reach - reach * seg(p, 1500, 1800) / 256;
    int hdx = p >= 150 && p < 1700 ? 1 : 0;
    bool done = round == 3 && placed;
    int hop = done && !quiet && p < 1900 ? arc(p - 1500, 400, 20) : 0;
    playmate(g, me, hop ? ST_HOP : blink(M_HAPPY, t, 0), (t / 400) & 1, ux, gy + hop, 3, false, night, hdx, hdy);
    brick(px, gy - 12, 30, RAINBOW[(n + v + 1) % 7]); brick(px + 34, gy - 12, 30, RAINBOW[(n + v + 2) % 7]);   // the pile
    int mx = muzzle_x(ux, 3, hdx, false), my = muzzle_y(gy, 3, hdy);
    u16 c = RAINBOW[(n + v) % 7];
    if (p < 350) brick(px + 14, gy - 24, 36, c);                  // the next one waits on the pile
    else if (p < 1000) { int f = seg(p, 350, 700); brick(lerp(px + 32, mx, f) - 18, lerp(gy - 27, my, f) + 3, 36, c); }   // up to her mouth
    else if (!placed) { int f = seg(p, 1000, 1500); brick(lerp(mx, tx + 18, f) - 18, lerp(my + 3, top, f), 36, c); }     // set on top
    if (done) {
        sparkle(tx + 6 + wave(t, 700, 6), top - 14, 3, YELLOW);
        sparkle(tx + 34, top - 4 + wave(t + 350, 700, 5), 3, PINK);
    }
}

// SC_AWAY, a book: she sits on her cushion with the book open on a stand under her nose, head low,
// scanning two lines left to right (1.1 s each); then she comes forward and nudges the next page over
// with her nose. The picture on the left page is this look's story.
static void act_read(const Game& g, int me, u32 t, int gy, bool night) {
    int bx = burn_x(g), v = g.away_pick % 3, ux = 196 + bx;
    static const u16 CUSHION[3] = {PINK, rgb(0xE0, 0x6A, 0x98), rgb(0x9F, 0xD8, 0xFF)};
    ellipse(256 + bx, gy - 6, 72, 14, CUSHION[v]);
    ellipse(256 + bx, gy - 9, 60, 8, CUSHION[(v + 2) % 3]);
    int bkx = 128 + bx, bky = gy - 98;                            // the open book, two pages, on its stand
    fill(bkx + 33, bky + 36, bkx + 45, gy, FRAME); fill(bkx + 15, gy - 6, bkx + 63, gy, FRAME);
    fill(bkx, bky, bkx + 78, bky + 36, PAGE_INK);
    fill(bkx + 3, bky + 3, bkx + 39, bky + 33, INK); fill(bkx + 42, bky + 3, bkx + 78, bky + 33, INK);
    static const int STORY[3] = {IC_STAR, IC_HEART, IC_FLOWER};
    icon(STORY[v], 2, bkx + 12, bky + 9);                         // a picture on the left page
    for (int k = 0; k < 4; k++) fill(bkx + 48, bky + 8 + k * 6, bkx + 72 - (k & 1) * 6, bky + 11 + k * 6, DIM);
    u32 p = t % 3000, f = p >= 2300 && p < 2700 ? p - 2300 : 400;
    if (f < 400) {                                                // a page flaps over, right to left
        int w = f < 200 ? 36 * (200 - (int)f) / 200 : 36 * ((int)f - 200) / 200;
        int x0 = f < 200 ? bkx + 42 : bkx + 39 - w;
        fill(x0, bky + 2, x0 + w, bky + 33, INK);
        fill(f < 200 ? x0 + w - 2 : x0, bky + 2, f < 200 ? x0 + w : x0 + 2, bky + 33, PAGE_INK);
    }
    int hdx = p < 2200 ? -1 + 2 * seg(p % 1100, 100, 1000) / 256 : 1 - 2 * seg(p, 2600, 2950) / 256;
    int hdy = p < 2200 ? 2 : p < 2700 ? 1 : 1 + seg(p, 2700, 2900) / 256;
    int tdx = p >= 1150 && p < 1350 ? 1 : 0;                      // a flick of the tail between the lines
    playmate(g, me, blink(M_IDLE, t, 0), (t / 700) & 1, ux, gy - 12, 3, true, night, hdx, hdy, tdx);
}

// SC_AWAY, her dolls: one sits up on the toy box at her nose, one on the floor. She nuzzles the near
// one (it bounces, a heart), rocks them both with a sway of her head, and the far one hops up to
// answer while she looks down at it. Calm or night: no hops, the dolls only sway.
static void act_dolls(const Game& g, int me, u32 t, int gy, bool night) {
    int bx = burn_x(g), v = g.away_pick % 3, ux = 30 + bx;
    bool quiet = g.calm() || night;
    u32 p = t % 3000;
    fill(150 + bx, gy - 24, 190 + bx, gy, WOOD); fill(150 + bx, gy - 24, 190 + bx, gy - 21, FRAME);   // the toy box
    int nuzzle = seg(p, 0, 300) - seg(p, 600, 900);              // 0..256: the nose to the near doll and back
    int sway = p >= 900 && p < 2100 ? wave(p - 900, 600, 3) : 0; // rocking, her head and the dolls together
    int hdx = nuzzle > 128 ? 1 : sway > 1 ? 1 : sway < -1 ? -1 : 0, hdy = nuzzle > 128 || p >= 2100 ? 1 : 0;
    playmate(g, me, blink(M_HAPPY, t, 0), (t / 600) & 1, ux, gy, 3, false, night, hdx, hdy);
    int bounce = p >= 250 && p < 650 ? arc(p - 250, 400, 8) : 0;   // the near doll, nudged
    int hop = !quiet && p >= 2100 && p < 2900 ? arc((p - 2100) % 400, 400, 16) : 0;
    doll(164 + bx + sway, gy - 24 + bounce, RAINBOW[(v * 2) % 7]);
    doll(232 + bx + sway + (quiet ? wave(t, 1200, 2) : 0), gy + hop, RAINBOW[(v * 2 + 3) % 7]);
    if (p >= 300 && p < 900) icon(IC_HEART, 2, muzzle_x(ux, 3, 1, false) + 2, muzzle_y(gy, 3, 1) - 30 - (int)(p - 300) / 30);
}

static void act_away(const Game& g, int me, u32 t, int gy, bool night) {
    switch (g.s.away_act) {
    case ACT_COLOR: act_color(g, me, t, gy, night); break;
    case ACT_LEGO:  act_lego(g, me, t, gy, night); break;
    case ACT_READ:  act_read(g, me, t, gy, night); break;
    default:        act_dolls(g, me, t, gy, night); break;
    }
}

static void scene_away(const Game& g) {
    bool night = g.phase() == PH_NIGHT;
    int me = g.s.character, pick = night ? AWAY_PICKS : g.away_pick % AWAY_PICKS;
    // the friends take turns at the front: never her own character
    int f1 = (me + 1 + pick % 3) % UP_CHARS, f2 = (me + 1 + (pick + 1) % 3) % UP_CHARS;
    u32 t = now(g);
    int gy = 346 + burn_y(g);
    text(T(S_NAME_0 + me), SCREEN_W / 2, 26, 2, PAL[UP_ACCENT[me]]);
    if (!g.s.away_act) text(T(S_OUT_PLAYING), SCREEN_W / 2, 48, 1, DIM);
    backdrop(g);                           // the day sun, the low evening sun, or the night sky
    ground(g, night);
    if (g.s.away_act) pick = -1;           // her own activity, by day and by night
    switch (pick) {
    case -1: act_away(g, me, t, gy, night); break;
    case 0: away_ball(g, me, f1, f2, t, gy); break;
    case 1: away_seesaw(g, me, f1, f2, t, gy); break;
    case 2: away_bubbles(g, me, f1, f2, t, gy); break;
    case 3: away_rope(g, me, f1, f2, t, gy); break;
    case 4: away_butterfly(g, me, f1, f2, t, gy); break;
    default: away_fireflies(g, me, f1, f2, t, gy); break;
    }
    particles(g, HEADER_Y1);               // hearts when someone taps: the friends wave back
    text(T(S_REST_TOO), SCREEN_W / 2, 422, 1, DIM);
}

static void scene_game(const Game& g) {
    char line[24];
    // title, time bar and score share the strip left of the house button (288..360): centred on 140
    const int cx = 140;
    bool big = text_w(T(S_STARS), 2) <= 264;   // a long translation drops to the small font, clear of the house
    text(T(S_STARS), cx, big ? 18 : 22, big ? 2 : 1, YELLOW);
    // rainbow time bar shrinking from both sides
    int left = (int)(GAME_MS - (g.scene_ms < GAME_MS ? g.scene_ms : GAME_MS));
    int half = 120 * left / GAME_MS;
    for (int x = cx - half; x < cx + half; x += 6)
        fill(x, 44, x + 6, 52, RAINBOW[((x + 2000) / 6) % 7]);
    icon(IC_STAR, 3, cx - 52, 62);
    utoa(g.score, line);
    text(line, cx - 12, 64, 3, INK, 0);
    // a small unicorn cheering at the bottom, behind the stars so they always stay tappable
    const int sz = UP_SIDE_W * UP_SIDE_H;
    const u8* spr = UP_SIDE + ((g.s.character * UP_SIDE_STATES + ST_HOP) * 2 + g.frame) * sz;
    blit(spr, UP_SIDE_W, UP_SIDE_H, 3, SCREEN_W / 2 - UP_SIDE_W * 3 / 2,
         SCREEN_H - UP_SIDE_H * 3 - 4 + (g.calm() ? 0 : arc(now(g) % 600, 600, 18)), PAL);
    for (int i = 0; i < GAME_TARGETS; i++) {
        const Target& t = g.tgt[i];
        int ty = t.y + ahead(t.vy, g), tx = t.x + wave(now(g) + i * 300, 1600, 3);   // drifting up
        if (!t.alive || ty > SCREEN_H + 30) continue;
        if (i == g.sel) rrect(tx - 40, ty - 37, tx + 40, ty + 37, 16, BLACK, LILAC, 2);   // buttons
        icon(IC_STAR, 7, tx - UP_ICON_W * 7 / 2, ty - UP_ICON_H * 7 / 2);
    }
    house_button();                        // the way home, as in every other mode
    particles(g);
}

// Each crack stage runs the break line further out from the middle, with a small fork or two.
static void cracks(int x0, int y0, int scale, int stage, u16 ink) {
    static const signed char REACH[4] = {2, 5, 8, 10};          // columns either side of the middle
    int r = REACH[(stage > 4 ? 4 : stage) - 1];
    auto dot = [&](int x, int y) {
        if (x < 1 || x >= UP_EGG_W - 1 || !UP_EGG[y * UP_EGG_W + x - 1] || !UP_EGG[y * UP_EGG_W + x + 1]) return;
        fill(x0 + x * scale, y0 + y * scale, x0 + (x + 1) * scale, y0 + (y + 1) * scale, ink);
    };
    for (int x = UP_EGG_W / 2 - r; x < UP_EGG_W / 2 + r; x++) {
        int y = EGG_CUT[x] - 1;                // the last row of the top half
        dot(x, y);
        if (x > 0 && EGG_CUT[x - 1] - 1 < y) dot(x, y - 1);   // keep the zigzag joined
    }
    if (stage >= 2) { dot(7, 14); dot(6, 15); }               // little forks off the main line
    if (stage >= 3) { dot(12, 9); dot(13, 8); }
    if (stage >= 4) { dot(3, 13); dot(16, 10); dot(16, 9); }
}

static void scene_egg(const Game& g) {
    text(T(S_EGG), SCREEN_W / 2, 52, 2, YELLOW);
    text(T(S_EGG_STIRS), SCREEN_W / 2, 74, 1, DIM);
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
    text(T(S_EGG_TAP), SCREEN_W / 2, 360, 2, INK);
    if (g.taps == 0) hand_hint(g, SCREEN_W / 2 + 70, 250);     // wordless: tap here
    for (int i = 0; i < EGG_TAPS; i++)
        ellipse(SCREEN_W / 2 - 48 + i * 24, 404, 6, 6, i < g.taps ? YELLOW : TRACK);
}

static void scene_hatch(const Game& g) {
    text(T(S_NAME_0 + g.s.character), SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);
    u32 t = g.scene_ms;
    if (t < 700) {                         // the shell breaks along its crack: the cap pops off, shards fly
        int x0 = (SCREEN_W - 160) / 2, y0 = 118;
        int lift = (int)(t * 90 / 700), drop = (int)(t * 12 / 700);
        int up = t < 300 ? (int)t * 44 / 300 : 44 - (int)(t - 300) * 8 / 400;   // up, never into her name
        ellipse(SCREEN_W / 2, 318, 90, 9, GROUND1);
        egg_draw(g.s.character, x0, y0 + drop, 8, 0, EGG_BOTTOM);
        egg_draw(g.s.character, x0 - lift / 2, y0 - up, 8, 0, EGG_TOP);
        static const signed char SHARD[3][3] = {{3, -3, -7}, {10, 1, -9}, {17, 4, -5}};
        static const char* const CHIP[3][2] = {{"ss.", ".sd"}, {"s..", "ssd"}, {".s", "sd"}};   // sliver, wedge, bit
        const u8* shell = UP_EGG + g.s.character * UP_EGG_W * UP_EGG_H;
        u16 bits[2] = {PAL[shell[12 * UP_EGG_W + 8]], PAL[shell[3 * UP_EGG_W + 12]]};
        for (int k = 0; k < 3; k++) {      // chips of shell from the break, falling away
            int sx = x0 + SHARD[k][0] * 8 + SHARD[k][1] * (int)t / 40;
            int sy = y0 + EGG_CUT[SHARD[k][0]] * 8 + SHARD[k][2] * (int)t / 40 + (int)(t * t / 6000);
            art(CHIP[k], 2, "sd", bits, sx - sx % 6, sy - sy % 6, 6, k == 0);
        }
        text(T(S_NAME_0 + g.s.character), SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);   // over the cap
    } else {                               // and out she pops, growing in
        particles(g);                      // confetti behind her, not on her face
        int scale = t < 820 ? 3 : t < 940 ? 4 : 5;
        int bounce = t < 940 ? 0 : arc(t - 940, 360, 14);
        unicorn_front(g, M_HAPPY, 90 + bounce, scale);
        sparkle(70, 140 + 8 * g.frame, 5, YELLOW);
        sparkle(300, 120 - 8 * g.frame, 4, PINK);
        sparkle(84, 300, 3, LILAC);
        if (t >= 940) text(T(S_HELLO), SCREEN_W / 2, 372 + arc(t - 940, 360, 8), 3, YELLOW);
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
    int sway = calm ? 0 : wave(t + cx * 7, 2600, 2);
    u16 greens[3] = {RAINBOW[3], rgb(0x3A, 0xA8, 0x5A), rgb(0x2E, 0x6A, 0x3A)};
    if (style == 2) { greens[0] = rgb(0xC9, 0x8A, 0x3C); greens[1] = rgb(0x9A, 0x62, 0x2A); }   // autumn
    if (stage == 1) {                      // a seed peeking out of the soil
        static const char* const SEED[2] = {".ll.", "lmml"};
        art(SEED, 2, "lm", greens, cx - 6, base - 6, 3);
    }
    if (stage < 2) return;
    int rows = stage == 2 ? 8 : 14, h = rows * 3;   // a sprout first, then the whole stem with its leaves
    art(STEM + 14 - rows, rows, "lmo", greens, cx - 13 + sway, base - h, 3);
    static const u16 BUD[4] = {PINK, rgb(0xFF, 0x5C, 0x9C), rgb(0xFF, 0x9F, 0x43), YELLOW};
    static const u16 BUD_SHADE[4] = {rgb(0xD0, 0x3A, 0x6A), rgb(0xC0, 0x2A, 0x6A), rgb(0xE8, 0x6A, 0x3A), rgb(0xE8, 0xB0, 0x30)};
    u16 cols[5] = {rgb(0x5B, 0x2A, 0x86), BUD[style], BUD_SHADE[style], YELLOW, rgb(0xFF, 0x9F, 0x43)};
    if (stage == 3) art(FLOWER_BUD, 17, "opqce", cols, cx - 17 + sway, base - h - 28, 2);   // a bud
    if (stage == 4) {
        if (style == 2) {                  // autumn: a round orange bloom with a red heart
            cols[3] = rgb(0xFF, 0x5C, 0x6E); cols[4] = rgb(0xC0, 0x2A, 0x4A);
            art(FLOWER_OPEN, 17, "opqce", cols, cx - 17 + sway, base - h - 28, 2);
        } else {
            static const int BLOOM[4] = {IC_FLOWER, IC_HEART, IC_FLOWER, IC_STAR};
            icon(BLOOM[style], 5, cx + sway - UP_ICON_W * 5 / 2, base - h - UP_ICON_H * 5 + 8);
        }
    }
}

// SC_ALBUM: a picture for every day she was cared for, and the garden those days grew
static void scene_album(const Game& g) {
    char title[48] = "";
    cat(title, T(S_ALBUM));
    if (g.books() > 1) {                   // "OUR MEMORIES 3": which book this is
        char n[8];
        utoa(g.book + 1, n);
        cat(title, " ");
        cat(title, n);
    }
    text(title, 32, 30 + (text_w(title, 2) > 248 ? 4 : 0), text_w(title, 2) > 248 ? 1 : 2, YELLOW, 0);   // clear of the house
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
    // a wooden planter on a 4 px grid: dark rim, lit top edge, shaded foot, soil on top
    fill(40, 400, 328, 436, rgb(0x4A, 0x30, 0x22));
    fill(44, 404, 324, 432, rgb(0x8B, 0x5A, 0x3C));
    fill(44, 404, 324, 408, rgb(0xB0, 0x78, 0x50));
    fill(44, 428, 324, 432, rgb(0x74, 0x48, 0x30));
    fill(48, 396, 320, 404, rgb(0x5A, 0x3A, 0x2A));
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
    static const short says[] = {S_SAY_FINE, S_SAY_HAPPY, S_SAY_HUNGRY, S_SAY_SLEEPY, S_SAY_HUG};
    static const int icons[NSTATS] = {IC_APPLE, IC_HEART, IC_STAR, IC_BUBBLE};
    Mood m = g.mood();
    int lift = g.album_unseen && !g.calm() ? wave(now(g), 900, 3) : 0;   // something new inside
    book(36, 14 + lift, g.album_unseen ? YELLOW : TRACK);
    if (g.album_unseen && g.scene_ms > 1500) hand_hint(g, 84, 54);
    text(T(S_NAME_0 + g.s.character), SCREEN_W / 2, 52, 2, PAL[UP_ACCENT[g.s.character]]);
    battery_icon(g, SCREEN_W - 52, 54);
    unicorn_front(g, m, 72);
    static const int STARS[4] = {0, 1, 2, 4};               // grown-up stars around her portrait
    static const short SX[4] = {58, 282, 58, 282}, SY[4] = {92, 92, 270, 270};
    for (int k = 0; k < STARS[g.stage()]; k++) icon(IC_STAR, 3, SX[k], SY[k]);
    text(T(says[m]), SCREEN_W / 2, 326, 2, INK);
    for (int i = 0; i < NSTATS; i++) {
        int y = 352 + i * 22;
        icon(icons[i], 2, 64, y);
        fill(96, y + 3, 304, y + 13, TRACK);
        fill(96, y + 3, 96 + 208 * g.s.stat[i] / MAX_STAT, y + 13, METER[i]);
    }
}

void render(const Game& g, u16* fb) { render_band(g, fb, 0, SCREEN_H); }

void render_band(const Game& g, u16* buf, int y0, int y1) {
    FB = buf;
    CY0 = y0 < 0 ? 0 : y0;
    CY1 = y1 > SCREEN_H ? SCREEN_H : y1;
    LANG = g.s.lang < STR_LANGS ? g.s.lang : 0;
    SC = g.stage() == STAGE_FOAL ? 4 : 6;
    palettes();
    fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
    char line[32];
    switch (g.scene) {
    case SC_EGG:   scene_egg(g); break;
    case SC_HATCH: scene_hatch(g); break;
    case SC_INFO:  scene_info(g); break;
    case SC_GAME:  scene_game(g); break;
    case SC_EAT:   home_like(g, ST_EAT, T(S_EATING), false, -1, 0, false); break;
    case SC_FEED:  scene_feed(g); break;
    case SC_SETTINGS: scene_settings(g); break;
    case SC_PLAY: {                        // celebration after the mini-game
        fmt(line, S_STARS_DONE, g.score);
        squash_now = g.calm() ? 0 : squash_for(now(g) % 700, 700 - SQUASH_MS);
        home_like(g, ST_PLAY, line, false, 0, g.calm() ? 0 : arc(now(g) % 700, 700 - SQUASH_MS, 30), false);
        squash_now = 0;
        break;
    }
    case SC_WASH:  home_like(g, ST_WASH, T(S_BATH), false, -1, 0, false); bath_foam(g); break;
    case SC_EGG_PICK: scene_egg_pick(g); break;
    case SC_TOYS:  scene_toys(g); break;
    case SC_MUSIC: scene_music(g); break;
    case SC_CLOUD: scene_cloud(g); break;
    case SC_MARBLE: scene_marble(g); break;
    case SC_BALLOON: scene_balloon(g); break;
    case SC_DRESS: scene_dress(g); break;
    case SC_ALBUM: scene_album(g); break;
    case SC_LOUNGE: scene_lounge(g); break;
    case SC_SLEEP: scene_sleep(g); break;
    case SC_LEAVE: walker(g, g.s.away_act ? "" : T(S_FRIEND_CALLS), T(S_BUSY)); break;   // her own break: just her name
    case SC_RETURN: walker(g, T(S_BACK_HOME), g.vel_x ? (const char*)0 : T(S_IM_BACK)); break;
    case SC_AWAY:  scene_away(g); break;
    case SC_REFUSE: scene_refuse(g); break;
    case SC_ASK:   scene_ask(g); break;
    default:       scene_home(g); break;
    }
}

}  // namespace up
