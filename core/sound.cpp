// Melodies for each sound cue: short square-wave tunes, played by the ES8311 on the board
// and by Web Audio in the emulator. Kept under ~1.2 s so they never get in the way.
#include "unicorn.h"

namespace up {

enum { C4 = 262, E4 = 330, G4 = 392, A4 = 440, C5 = 523, D5 = 587, E5 = 659, G5 = 784, A5 = 880,
       C6 = 1047, D6 = 1175, E6 = 1319, G6 = 1568, C7 = 2093, REST = 0 };

static const Note TAP[] = {{C6, 40}};
static const Note HATCH[] = {{C5, 90}, {E5, 90}, {G5, 90}, {C6, 90}, {REST, 40}, {G5, 80}, {C6, 220}};
static const Note EAT[] = {{G4, 60}, {REST, 50}, {G4, 60}, {REST, 50}, {C5, 80}};
static const Note PLAY[] = {{C5, 70}, {E5, 70}, {G5, 70}, {C6, 70}, {G5, 70}, {C6, 160}};
static const Note WASH[] = {{E6, 40}, {REST, 30}, {G6, 40}, {REST, 40}, {D6, 40}, {REST, 30}, {G6, 60}};
static const Note SLEEP[] = {{G5, 180}, {E5, 180}, {C5, 180}, {G4, 360}};
static const Note WAKE[] = {{G4, 90}, {C5, 90}, {E5, 90}, {G5, 200}};
static const Note REFUSE[] = {{E4, 160}, {C4, 280}};
static const Note INFO[] = {{A5, 60}, {D6, 90}};
static const Note PET[] = {{C6, 60}, {E6, 110}};
static const Note POP[] = {{G6, 25}, {C7, 45}};
static const Note BYE[] = {{G5, 120}, {E5, 120}, {REST, 60}, {C5, 120}, {G4, 260}};          // "bye-bye!"
static const Note KNOCK[] = {{G4, 40}, {REST, 90}, {G4, 40}};                                // knock knock
static const Note HELLO[] = {{C5, 80}, {G5, 80}, {REST, 40}, {E5, 80}, {C6, 240}};
static const Note NOTE1[] = {{C5, 120}};                                   // music garden flowers
static const Note NOTE2[] = {{E5, 120}};
static const Note NOTE3[] = {{G5, 120}};
static const Note CHOOSE[] = {{C5, 60}, {E5, 60}, {G5, 60}};                 // "this egg is mine"
static const Note LAND[] = {{G5, 60}};                                       // a feather lands on the cloud
static const Note DRESS[] = {{E5, 60}, {G5, 100}};
static const Note CHARGE[] = {{C5, 60}, {E5, 60}, {G5, 60}, {C6, 140}};
static const Note CHEER[] = {{C5, 70}, {E5, 70}, {G5, 70}, {C6, 70}, {E6, 70}, {REST, 40}, {C6, 90}, {E6, 220}};

static Note ECHO[2 * NOTES];                 // filled by set_echo(): note, rest, note, rest...
static int echo_len;

void set_echo(const u8* flowers, int n) {
    static const u16 HZ[3] = {C5, E5, G5};
    echo_len = 0;
    for (int i = 0; i < n && i < NOTES; i++) {
        ECHO[echo_len++] = Note{HZ[flowers[i] % 3], 120};
        if (i + 1 < n) ECHO[echo_len++] = Note{REST, ECHO_GAP_MS - 120};
    }
}

#define TUNE(t) {t, (int)(sizeof(t) / sizeof(t[0]))}
static const struct { const Note* notes; int count; } TUNES[SFX_COUNT] = {
    {0, 0}, TUNE(TAP), TUNE(HATCH), TUNE(EAT), TUNE(PLAY), TUNE(WASH), TUNE(SLEEP), TUNE(WAKE),
    TUNE(REFUSE), TUNE(INFO), TUNE(PET), TUNE(POP), TUNE(CHEER), TUNE(BYE), TUNE(KNOCK), TUNE(HELLO),
    TUNE(NOTE1), TUNE(NOTE2), TUNE(NOTE3), TUNE(CHOOSE), TUNE(LAND), TUNE(DRESS), {0, 0}, TUNE(CHARGE),
};

const Note* melody(int sfx, int* count) {
    if (sfx == SFX_ECHO) { *count = echo_len; return echo_len ? ECHO : 0; }
    if (sfx <= SFX_NONE || sfx >= SFX_COUNT) { *count = 0; return 0; }
    *count = TUNES[sfx].count;
    return TUNES[sfx].notes;
}

// ---- her voice packs (format: tools/voice.py) ----------------------------------------------------
static u32 le32(const u8* p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u32 le16(const u8* p) { return p[0] | p[1] << 8; }

// Clip `index` of language `lang`. The packs come off an SD card too, so everything is checked before a
// single byte is played: the table, every Ogg page header and segment table against the clip's end, a mono
// OpusHead first, and no packet running on into a next page (tools/voice.py never writes one).
bool pack_clip(const u8* bin, u32 len, int lang, int index, Clip* c) {
    if (!bin || len < 8 || bin[0] != 'U' || bin[1] != 'P' || bin[2] != 'O' || bin[3] != '1') return false;
    u32 n = le16(bin + 4), per = le16(bin + 6);
    if (lang < 0 || index < 0 || (u32)index >= per) return false;
    unsigned long long e = (unsigned long long)lang * per + (u32)index;
    if (e >= n || 8 + (e + 1) * 8 > len) return false;
    u32 off = le32(bin + 8 + e * 8), size = le32(bin + 12 + e * 8);
    if ((unsigned long long)off + size > len || size < 27) return false;
    const u8* d = bin + off;
    u32 at = 0, pages = 0, start = 0, gran = 0;
    u16 pre = 0;
    while (at < size) {
        if (size - at < 27) return false;
        const u8* p = d + at;
        if (p[0] != 'O' || p[1] != 'g' || p[2] != 'g' || p[3] != 'S' || p[4] != 0) return false;
        u32 nseg = p[26], body = 0;
        if (!nseg || size - at - 27 < nseg) return false;
        for (u32 k = 0; k < nseg; k++) body += p[27 + k];
        if (p[27 + nseg - 1] == 255 || size - at - 27 - nseg < body) return false;
        const u8* b = p + 27 + nseg;
        if (pages == 0) {
            if (body < 19 || b[0] != 'O' || b[1] != 'p' || b[2] != 'u' || b[3] != 's' || b[4] != 'H' || b[8] != 1 || b[9] != 1)
                return false;              // OpusHead, version 1, one channel
            pre = (u16)le16(b + 10);
        }
        if (pages == 2) start = at;
        gran = le32(p + 6);                // (the high half is 0 for any clip shorter than a day)
        at += 27 + nseg + body;
        pages++;
    }
    if (pages < 3) return false;
    c->data = d;
    c->len = size;
    c->page = start;
    c->body = start + 27 + d[start + 26];
    c->seg = 0;
    c->pre_skip = pre;
    c->samples = gran > pre ? (gran - pre) / 3 : 0;   // the stream counts at 48 kHz
    return true;
}

int ogg_packet(Clip* c, const u8** packet) {
    while (c->page < c->len) {
        const u8* p = c->data + c->page;
        int nseg = p[26];
        if (c->seg >= nseg) {              // on to the next page (pack_clip checked them all)
            c->page = c->body;
            c->seg = 0;
            if (c->page < c->len) c->body = c->page + 27 + c->data[c->page + 26];
            continue;
        }
        u32 n = 0;
        u8 v;
        do { v = p[27 + c->seg++]; n += v; } while (v == 255 && c->seg < nseg);
        *packet = c->data + c->body;
        c->body += n;
        if (n) return (int)n;              // (an empty packet: nothing to decode)
    }
    return 0;
}

}  // namespace up
