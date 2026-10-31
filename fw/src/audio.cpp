// Square-wave synth for the core's melodies -> I2S -> ES8311 -> speaker amp.
// Codec and I2S setup follow Waveshare's 15_ES8311 example for this board.
#include <Arduino.h>
#include <ESP_I2S.h>
#include "es8311.h"
#include "audio.h"
#include "../../core/unicorn.h"

static const int I2S_MCK = 16, I2S_BCK = 9, I2S_WS = 45, I2S_DO = 8, I2S_DI = 10, PA_EN = 46;
static const int RATE = 16000;
static const int VOLUME = 70;              // codec volume 0-100: a toy, not a party speaker
static const int16_t AMP = 2500;           // square-wave amplitude out of 32767

static I2SClass i2s;
static QueueHandle_t cues;
static volatile bool playing;
static es8311_handle_t codec;
static int level = -1;

static void play_note(int hz, int ms) {
    static int16_t buf[2 * 128];           // 128 stereo frames
    int frames = RATE * ms / 1000;
    int period = hz ? RATE / hz : 0;
    int phase = 0;
    while (frames > 0) {
        int n = frames < 128 ? frames : 128;
        for (int i = 0; i < n; i++) {
            int16_t v = 0;
            if (period) {
                // short linear fade at the end of each note avoids clicks between notes
                int left = frames - i;
                int16_t amp = left < 160 ? (int16_t)(AMP * left / 160) : AMP;
                v = (phase < period / 2) ? amp : (int16_t)-amp;
                if (++phase >= period) phase = 0;
            }
            buf[2 * i] = buf[2 * i + 1] = v;
        }
        i2s.write((uint8_t*)buf, n * 4);
        frames -= n;
    }
}

static void audio_task(void*) {
    int sfx;
    for (;;) {
        if (xQueueReceive(cues, &sfx, portMAX_DELAY) != pdTRUE) continue;
        int count = 0;
        const up::Note* notes = up::melody(sfx, &count);
        if (!count) continue;
        playing = true;
        digitalWrite(PA_EN, HIGH);         // speaker amp on only while a tune plays
        for (int i = 0; i < count; i++) {
            if (uxQueueMessagesWaiting(cues)) break;   // a newer cue cuts this one short
            play_note(notes[i].hz, notes[i].ms);
        }
        play_note(0, 30);                  // flush silence before muting the amp
        digitalWrite(PA_EN, LOW);
        playing = false;
    }
}

bool audio_begin() {
    pinMode(PA_EN, OUTPUT);
    digitalWrite(PA_EN, LOW);
    i2s.setPins(I2S_BCK, I2S_WS, I2S_DO, I2S_DI, I2S_MCK);
    if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH))
        return false;
    es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
    if (!es) return false;
    codec = es;
    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = RATE * 256,
        .sample_frequency = RATE,
    };
    if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
    es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
    es8311_voice_volume_set(es, VOLUME, NULL);
    cues = xQueueCreate(1, sizeof(int));
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 2, NULL, 0);
    return true;
}

void audio_level(int l) {                  // parent setting: 0 normal, 1 low, 2 off
    if (l == level || !codec) return;
    level = l;
    if (l == 2) {                          // off now: drop what is queued and silence what plays
        if (cues) xQueueReset(cues);
        es8311_voice_volume_set(codec, 0, NULL);
        return;
    }
    es8311_voice_volume_set(codec, l == 0 ? VOLUME : VOLUME * 2 / 3, NULL);
}

void audio_play(int sfx) {
    if (level == 2) return;
    if (cues && sfx) xQueueOverwrite(cues, &sfx);
}

bool audio_busy() {
    return playing || (cues && uxQueueMessagesWaiting(cues));
}
