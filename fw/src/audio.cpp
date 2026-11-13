// The core's melodies (square wave) and her voice (Opus packs) -> I2S -> ES8311 -> speaker amp; and the
// microphone back the other way for her routine questions and TV test mode.
// Codec and I2S setup follow Waveshare's 15_ES8311 example for this board (which also echoes the mic).
#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <math.h>
#include <atomic>
#include "es8311.h"
#include "opus.h"
#include "audio.h"
#include "vad.h"
#include "store.h"
#include "../../core/unicorn.h"

static const int I2S_MCK = 16, I2S_BCK = 9, I2S_WS = 45, I2S_DO = 8, I2S_DI = 10, PA_EN = 46;
static const int RATE = 16000;
static const int VOLUME = 70;              // codec volume 0-100: a toy, not a party speaker
static const int16_t AMP = 2500;           // square-wave amplitude out of 32767
// the microphone: PGA gain (Waveshare's example uses 3 of 0..7 = 18 dB) and the voice-activity detector
static const es8311_mic_gain_t MIC_GAIN = ES8311_MIC_GAIN_18DB;   // VERIFY on the board: a child at arm's length
static const int FRAME = 320;              // 20 ms
static const int REC_MAX_MS = 6000;         // (when speech starts and stops: vad.h)
static const int PREROLL = 15 * FRAME;     // 300 ms kept from before the first word
static const int REC_SAMPLES = RATE * REC_MAX_MS / 1000 + PREROLL;

// fw/data/voice.bin (tools/voice.py), linked in by board_build.embed_files
extern const uint8_t voice_bin[] asm("_binary_data_voice_bin_start");
extern const uint8_t voice_bin_end[] asm("_binary_data_voice_bin_end");

struct Cue { int16_t sfx, line, clip; uint8_t lang; uint32_t hush; };
// audio_routine_stop() counts up: a routine clip queued (or playing) under an older count is not said (any more)
static std::atomic<uint32_t> hush{0};
static I2SClass i2s;
static QueueHandle_t cues;
static volatile bool playing;
static es8311_handle_t codec;
static int level = -1;
static OpusDecoder* dec;

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

// One clip of a pack: its Opus packets decoded straight at 16 kHz (libopus, fixed point); a newer cue cuts
// her short. pack_clip() has checked the whole clip, so a damaged pack (an SD card) plays nothing.
// gen: a routine clip's hush count (it stops when audio_routine_stop() moves on), -1 for her other lines
static bool say(const uint8_t* bin, uint32_t len, int lang, int index, int64_t gen = -1) {
    up::Clip c;
    if (!dec || !up::pack_clip(bin, len, lang, index, &c)) return false;
    opus_decoder_ctl(dec, OPUS_RESET_STATE);
    playing = true;
    digitalWrite(PA_EN, HIGH);
    static int16_t pcm[960], st[2 * 960];  // up to 60 ms per packet (ours are 20 ms)
    uint32_t skip = c.pre_skip / 3, left = c.samples, spent = 0;   // pre-skip counts at 48 kHz
    const uint8_t* p;
    int n;
    while (left && !uxQueueMessagesWaiting(cues) && (gen < 0 || gen == hush) && (n = up::ogg_packet(&c, &p)) > 0) {
        uint32_t t0 = micros();
        int got = opus_decode(dec, p, n, pcm, 960, 0);
        spent += micros() - t0;
        if (got < 0) break;                // a broken packet: stop there
        uint32_t from = skip < (uint32_t)got ? skip : got;
        skip -= from;
        uint32_t use = got - from < left ? got - from : left;
        left -= use;
        for (uint32_t i = 0; i < use; i++) st[2 * i] = st[2 * i + 1] = pcm[from + i];
        i2s.write((uint8_t*)st, use * 4);
    }
    play_note(0, 30);                      // flush silence before muting the amp
    digitalWrite(PA_EN, LOW);
    playing = false;
    // measured on the board: decoding time against the length of speech (CPU share of the audio task)
    Serial.printf("voice: %lu ms of speech decoded in %lu ms\n", (unsigned long)(c.samples / 16),
                  (unsigned long)(spent / 1000));
    return true;
}

static bool speak(const Cue& c) {
    Pack pk;
    if (c.clip >= 0) return c.hush != hush || (store_pack(PACK_ROUTINES, &pk) && say(pk.data, pk.len, 0, c.clip, c.hush));
    if (!c.line) return false;
    int named = c.lang == up::LANG_TR ? up::name_line(c.sfx, c.line) : -1;   // her name: Turkish only
    if (named >= 0 && store_pack(PACK_NAMES, &pk) && say(pk.data, pk.len, 0, named)) return true;
    return say(voice_bin, (uint32_t)(voice_bin_end - voice_bin), c.lang, c.line - 1);
}

// ---- listening ----------------------------------------------------------------------------------

// A request (up::LK_*, -1 = none) waits in `pending` while a capture runs in `active`: a question that becomes
// due during a TV level check is recorded right after it, never dropped.
static std::atomic<int> pending{-1};
static volatile int active = -1;
static volatile bool cancelled, started, heard_ready;
static uint8_t* rec;                       // PSRAM: a WAV header, then up to REC_SAMPLES samples
static Heard heard;

static void wav_header(uint8_t* h, uint32_t samples) {
    uint32_t bytes = samples * 2;
    auto put32 = [&](int o, uint32_t v) { h[o] = v; h[o + 1] = v >> 8; h[o + 2] = v >> 16; h[o + 3] = v >> 24; };
    memcpy(h, "RIFF", 4); put32(4, 36 + bytes); memcpy(h + 8, "WAVEfmt ", 8); put32(16, 16);
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0; put32(24, RATE); put32(28, RATE * 2); h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); put32(40, bytes);
}

// one 20 ms frame of the microphone (the codec's ADC is the left slot; VERIFY), and its RMS
static int mic_frame(int16_t* out) {
    static int16_t st[2 * FRAME];
    size_t got = i2s.readBytes((char*)st, sizeof st);
    int n = (int)(got / 4);
    int64_t sum = 0;
    for (int i = 0; i < n; i++) { out[i] = st[2 * i]; sum += (int32_t)out[i] * out[i]; }
    for (int i = n; i < FRAME; i++) out[i] = 0;
    return n ? (int)sqrtf((float)sum / n) : 0;
}

static int db_of(float rms) { return rms < 1 ? -90 : (int)lroundf(20.0f * log10f(rms / 32768.0f)); }

static void record(int kind) {
    int16_t* pcm = (int16_t*)(rec + 44);
    int16_t frame[FRAME];
    i2s_channel_enable(i2s.rxChan());      // the microphone's half of I2S only while listening
    int room[VAD_ROOM_FRAMES];             // 100 ms: the ADC settles, the speaker's tail fades, and the room's
    for (int k = 0; k < VAD_ROOM_FRAMES; k++) room[k] = mic_frame(frame);   // level, before she may answer
    if (kind <= up::LK_OPEN) started = true;   // the ear shows for her question's own recording only
    Heard h = {kind, false, -90, 0, rec, 44};
    uint32_t n = 0;
    if (kind == up::LK_LEVEL) {            // about a second: the level, and how much of it was loud
        float sum = 0;
        int loud = 0;
        for (int k = 0; k < 50 && !cancelled; k++) {
            int r = mic_frame(frame);
            sum += (float)r * r;
            loud += db_of(r) >= up::TV_LEVEL_DB;
        }
        h.db = db_of(sqrtf(sum / 50));
        h.loud_pct = loud * 2;
    } else if (kind == up::LK_TV) {        // five seconds, as they are
        for (int k = 0; k < 250 && !cancelled; k++) { mic_frame(pcm + n); n += FRAME; }
        h.speech = true;
    } else {                               // her question's answer: wait for speech, stop after a pause
        Vad v = vad_begin(room);           // from the first frame on, her answer counts as speech
        while (!cancelled) {
            int16_t* at = h.speech ? pcm + n : pcm + (n % PREROLL);   // before speech: a 300 ms ring
            int step = vad_step(v, mic_frame(at));
            n += FRAME;
            if (step == VAD_END) break;    // a pause after speech, or nothing said
            if (step == VAD_START) {       // speech: the ring, in order, becomes the start
                int keep = n < (uint32_t)PREROLL ? n : PREROLL, head = n % PREROLL;
                static int16_t tmp[PREROLL];
                for (int i = 0; i < keep; i++) tmp[i] = pcm[(head + PREROLL - keep + i) % PREROLL];
                memcpy(pcm, tmp, keep * 2);
                n = keep;
                h.speech = true;
            } else if (h.speech && n + FRAME > (uint32_t)REC_SAMPLES) break;
        }
        if (!h.speech) n = 0;
    }
    i2s_channel_disable(i2s.rxChan());
    wav_header(rec, n);
    h.len = 44 + n * 2;
    heard = h;                             // (the task then marks it ready, once `active` is clear)
}

static void audio_task(void*) {
    Cue c;
    for (;;) {
        if (xQueueReceive(cues, &c, pdMS_TO_TICKS(50)) != pdTRUE) {
            int kind = pending;            // after what she said, and once the last result was taken (rec is reused)
            if (kind >= 0 && rec && !heard_ready && pending.compare_exchange_strong(kind, -1)) {
                active = kind; cancelled = false; record(kind); active = -1; heard_ready = !cancelled;
            }
            continue;
        }
        if (speak(c)) continue;
        int count = 0;
        const up::Note* notes = up::melody(c.sfx, &count);
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
    i2s_channel_disable(i2s.rxChan());     // the microphone's DMA runs only while listening
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
    es8311_microphone_config(es, false);   // the analog mic, as in Waveshare's example
    es8311_microphone_gain_set(es, MIC_GAIN);
    audio_mic(false);                      // powered up only while listening
    int err = 0;
    dec = opus_decoder_create(RATE, 1, &err);   // ~18 KB; libopus' scratch (120 KB) goes to PSRAM on first use
    if (err != OPUS_OK) { dec = nullptr; Serial.println("opus decoder: no memory"); }
    rec = (uint8_t*)ps_malloc(44 + REC_SAMPLES * 2);
    cues = xQueueCreate(1, sizeof(Cue));
    // 16 KB: libopus keeps its big buffers on a heap scratch (NONTHREADSAFE_PSEUDOSTACK), not this stack
    xTaskCreatePinnedToCore(audio_task, "audio", 16384, NULL, 2, NULL, 0);
    return true;
}

// ES8311 REG0E: bit 6 powers down the PGA, bit 5 the ADC modulator (VERIFY against the datasheet on the
// board: 0x02 is es8311_init's "everything on"). Main task only: it is an I2C write.
void audio_mic(bool on) {
    if (!codec) return;
    uint8_t v = on ? 0x02 : 0x62;
    Wire.beginTransmission(ES8311_ADDRRES_0);
    Wire.write(0x0E);
    Wire.write(v);
    Wire.endTransmission();
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

void audio_play(int sfx, int line, int lang, int clip) {
    if (level == 2 || !cues || (!sfx && !line && clip < 0)) return;
    Cue c = {(int16_t)sfx, (int16_t)line, (int16_t)clip, (uint8_t)lang, hush.load()};
    xQueueOverwrite(cues, &c);
}

void audio_listen(int kind) {
    started = false;
    pending = kind;
}

void audio_listen_cancel() { pending = -1; cancelled = true; }
void audio_routine_stop() { hush++; }
bool audio_listening() { return pending >= 0 || active >= 0; }

bool audio_listen_started() {
    if (!started) return false;
    started = false;
    return true;
}

bool audio_heard(Heard* out) {
    if (!heard_ready) return false;
    heard_ready = false;
    *out = heard;
    return true;
}

bool audio_busy() {
    return playing || audio_listening() || heard_ready || (cues && uxQueueMessagesWaiting(cues));
}
