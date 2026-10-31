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

}  // namespace up
