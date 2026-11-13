"""Her spoken lines, in every language: ElevenLabs -> trimmed, levelled, soft-limited 16 kHz mono ->
Opus (16 kbps VBR, voip, 20 ms frames: chosen by ear over IMA ADPCM) -> fw/data/voice.bin (the board
embeds it, the emulator fetches it) + core/voice.gen.h. Needs ffmpeg with libopus.

Run:  uv run --with numpy python tools/voice.py               public lines (needs ELEVENLABS_API_KEY for
                                                              lines not in the cache; source ~/.gizli-env)
      uv run --with numpy python tools/voice.py --personal    fw/personal/voice_name.bin + config.json
      uv run --with numpy python tools/voice.py --routines    fw/personal/routines_tr.bin (both private)
      python3 tools/voice.py --check     no network, no numpy: the generated headers match, every clip of
                                         every pack present parses and decodes (make -C test voice)
      ... --wav DIR                      also writes each clip, decoded again, as a .wav to listen to
Downloads are cached in tools/.voice_cache/ (gitignored): only new or changed lines cost characters.

Pack format (all three): "UPO1", u16 clips, u16 clips per language, then (u32 offset, u32 bytes) per clip,
then the clips, each a complete Ogg Opus stream (mono, metadata stripped). voice.bin holds languages one
after another (LANGS order, KEYS within); voice_name.bin and routines_tr.bin are Turkish only, in PKEYS and
routine_lines() order. core/sound.cpp reads the Ogg pages; the board decodes with libopus, the emulator
with decodeAudioData.
"""
import hashlib, json, os, struct, subprocess, sys, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VOICE_ID = "fTnZuHAAYP4CLqSegleo"          # "Unicorn Pet - calm" (ElevenLabs voice design, chosen 2026-11-13)
MODEL = "eleven_v3"                        # takes a delivery tag in front of the line
SETTINGS = {"stability": 0.5, "similarity_boost": 0.8, "style": 0.0, "use_speaker_boost": True, "speed": 1.0}
TAG = "[cheerful, gentle, friendly] "        # chosen by ear 2026-11-13: neither shouting nor ASMR
RATE = 16000
LANGS = ["tr", "en", "es", "fr", "de", "pt", "it", "ru", "sv", "id"]   # the order of tools/i18n.py

# key: one line per language. She is a girl: gendered languages agree.
LINES = {
    "HELLO": ["Merhaba!", "Hello!", "¡Hola!", "Coucou !", "Hallo!", "Olá!", "Ciao!", "Привет!", "Hej!", "Halo!"],
    "MISSED": ["Seni çok özledim!", "I missed you so much!", "¡Te extrañé mucho!", "Tu m'as tellement manqué !",
               "Ich hab dich so vermisst!", "Senti muito a sua falta!", "Mi sei mancato tantissimo!",
               "Я так по тебе скучала!", "Jag har saknat dig så!", "Aku kangen banget!"],
    "BIRTHDAY": ["Bugün benim doğum günüm!", "It's my birthday today!", "¡Hoy es mi cumpleaños!",
                 "C'est mon anniversaire !", "Heute ist mein Geburtstag!", "Hoje é meu aniversário!",
                 "Oggi è il mio compleanno!", "Сегодня мой день рождения!", "Idag fyller jag år!",
                 "Hari ini ulang tahunku!"],
    "GREW": ["Bak, büyüdüm!", "Look, I grew up!", "¡Mira, he crecido!", "Regarde, j'ai grandi !",
             "Schau, ich bin gewachsen!", "Olha, eu cresci!", "Guarda, sono cresciuta!", "Смотри, я выросла!",
             "Titta, jag har vuxit!", "Lihat, aku sudah besar!"],
    "YAY": ["Yaşasın!", "Yay!", "¡Bien!", "Youpi !", "Juhu!", "Eba!", "Evviva!", "Ура!", "Hurra!", "Hore!"],
    "BYE": ["Arkadaşlarımla oynamaya gidiyorum. Görüşürüz!", "I'm off to play with my friends. See you soon!",
            "Me voy a jugar con mis amigos. ¡Hasta pronto!", "Je vais jouer avec mes amis. À bientôt !",
            "Ich gehe mit meinen Freunden spielen. Bis bald!", "Vou brincar com meus amigos. Até logo!",
            "Vado a giocare con i miei amici. A presto!", "Я пойду играть с друзьями. До скорого!",
            "Jag går och leker med mina vänner. Vi ses snart!", "Aku main dulu sama teman-teman. Sampai nanti!"],
    "MORNING": ["Günaydın!", "Good morning!", "¡Buenos días!", "Bonjour !", "Guten Morgen!", "Bom dia!",
                "Buongiorno!", "Доброе утро!", "God morgon!", "Selamat pagi!"],
    "AWAKE": ["Uyandım!", "I'm awake!", "¡Ya desperté!", "Je suis réveillée !", "Ich bin wach!", "Acordei!",
              "Mi sono svegliata!", "Я проснулась!", "Jag är vaken!", "Aku sudah bangun!"],
    "SLEEPY": ["Uykum geldi...", "I'm sleepy...", "Tengo sueño...", "J'ai sommeil...", "Ich bin müde...",
               "Estou com sono...", "Ho sonno...", "Я хочу спать...", "Jag är sömnig...", "Aku ngantuk..."],
    "GIGGLE": ["Hihi, gıdıklanıyorum!", "Hee hee, that tickles!", "¡Jiji, me haces cosquillas!",
               "Hihi, ça chatouille !", "Hihi, das kitzelt!", "Hihi, faz cócegas!", "Ihih, mi fai il solletico!",
               "Хи-хи, щекотно!", "Hihi, det kittlas!", "Hihi, geli!"],
    "YUM": ["Nam nam, çok lezzetli!", "Yum yum, so tasty!", "¡Ñam ñam, qué rico!", "Miam miam, c'est bon !",
            "Mmm, lecker!", "Nham nham, que gostoso!", "Gnam gnam, che buono!", "Ням-ням, как вкусно!",
            "Mums, vad gott!", "Nyam nyam, enak sekali!"],
    "TIRED": ["Çok yorgunum, biraz dinleneyim.", "I'm too tired. Let me rest a bit.",
              "Estoy muy cansada. Voy a descansar.", "Je suis trop fatiguée. Je me repose un peu.",
              "Ich bin zu müde. Ich ruhe mich aus.", "Estou muito cansada. Vou descansar um pouco.",
              "Sono troppo stanca. Riposo un po'.", "Я очень устала. Немного отдохну.",
              "Jag är för trött. Jag vilar lite.", "Aku capek sekali. Istirahat dulu ya."],
    "PLAY": ["Hadi oynayalım!", "Let's play!", "¡Vamos a jugar!", "On joue ?", "Lass uns spielen!",
             "Vamos brincar!", "Giochiamo!", "Давай играть!", "Kom, vi leker!", "Ayo main!"],
    "BATH": ["Köpük köpük, ne güzel!", "Bubbles! How lovely!", "¡Burbujas, qué bonito!",
             "Des bulles, c'est trop bien !", "Blubber, wie schön!", "Bolhas, que lindo!", "Bolle, che bello!",
             "Пузырьки! Как здорово!", "Bubblor, vad härligt!", "Gelembung, asyik!"],
    "REST": ["Oh, biraz dinleneyim.", "Ahh, time for a little rest.", "Ah, a descansar un poco.",
             "Ah, un petit repos.", "Ah, ein bisschen ausruhen.", "Ah, hora de descansar.", "Ah, un po' di riposo.",
             "Ах, немножко отдохну.", "Ah, lite vila.", "Ah, istirahat sebentar."],
    "PRETTY": ["Çok güzel oldum!", "I look so pretty!", "¡Qué bonita estoy!", "Je suis trop jolie !",
               "Bin ich hübsch!", "Fiquei linda!", "Come sono carina!", "Какая я красивая!", "Vad fin jag är!",
               "Aku cantik sekali!"],
}
KEYS = list(LINES)
assert all(len(v) == len(LANGS) for v in LINES.values())

# Lines with the child's name: a separate, private voice pack. The name lives only in
# ~/.config/unicorn-pet/personal.json ({"name": ..., "spoken": {lang: how to say it}}) and the pack in
# fw/personal/ (gitignored, flashed once with `pio run -d fw -t uploadfs`, kept by WiFi updates):
# the public repo and the public firmware never hold a child's name.
PERSONAL = {
    "AWAKE_N": ["{name}, uyandım ben!", "{name}, I'm awake!", "¡{name}, ya desperté!", "{name}, je suis réveillée !",
                "{name}, ich bin wach!", "{name}, acordei!", "{name}, mi sono svegliata!", "{name}, я проснулась!",
                "{name}, jag är vaken!", "{name}, aku sudah bangun!"],
    "BACK_N": ["{name}, ben geldim! Hadi oynayalım.", "{name}, I'm back! Let's play.", "¡{name}, ya volví! Vamos a jugar.",
               "{name}, je suis revenue ! On joue ?", "{name}, ich bin wieder da! Lass uns spielen.",
               "{name}, voltei! Vamos brincar?", "{name}, sono tornata! Giochiamo?", "{name}, я вернулась! Давай играть.",
               "{name}, jag är tillbaka! Ska vi leka?", "{name}, aku sudah pulang! Ayo main."],
    "CALL_N": ["{name}, neredesin? Seni özledim, gel oynayalım.", "{name}, where are you? I miss you, come and play.",
               "{name}, ¿dónde estás? Te extraño, ven a jugar.", "{name}, où es-tu ? Tu me manques, viens jouer.",
               "{name}, wo bist du? Ich vermisse dich, komm spielen.", "{name}, cadê você? Estou com saudade, vem brincar.",
               "{name}, dove sei? Mi manchi, vieni a giocare.", "{name}, где ты? Я скучаю, иди играть.",
               "{name}, var är du? Jag saknar dig, kom och lek.", "{name}, kamu di mana? Aku kangen, ayo main."],
    "HELLO_N": ["Merhaba {name}!", "Hello {name}!", "¡Hola {name}!", "Coucou {name} !", "Hallo {name}!", "Olá {name}!",
                "Ciao {name}!", "Привет, {name}!", "Hej {name}!", "Halo {name}!"],
    "MORNING_N": ["Günaydın {name}!", "Good morning, {name}!", "¡Buenos días, {name}!", "Bonjour {name} !",
                  "Guten Morgen, {name}!", "Bom dia, {name}!", "Buongiorno, {name}!", "Доброе утро, {name}!",
                  "God morgon, {name}!", "Selamat pagi, {name}!"],
    "MISSED_N": ["{name}, seni çok özledim!", "{name}, I missed you so much!", "¡{name}, te extrañé mucho!",
                 "{name}, tu m'as tellement manqué !", "{name}, ich hab dich so vermisst!", "{name}, senti muito a sua falta!",
                 "{name}, mi sei mancata tantissimo!", "{name}, я так по тебе скучала!", "{name}, jag har saknat dig så!",
                 "{name}, aku kangen banget!"],
}
PKEYS = list(PERSONAL)
assert all(len(v) == len(LANGS) for v in PERSONAL.values())

# ---- Opus: one self-contained Ogg Opus stream per clip ------------------------------------------
OPUS = ["-c:a", "libopus", "-b:a", "16k", "-vbr", "on", "-application", "voip", "-frame_duration", "20",
        "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", "-f", "ogg"]


def ffmpeg(args, data):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", *args], input=data, capture_output=True)
    if r.returncode: raise SystemExit("ffmpeg failed: " + r.stderr.decode(errors="replace")[-400:])
    return r.stdout


def encode(pcm):
    """16 kHz mono int16 -> Ogg Opus, 16 kbps VBR, voip, 20 ms frames."""
    return ffmpeg(["-f", "s16le", "-ar", str(RATE), "-ac", "1", "-i", "pipe:0", *OPUS, "pipe:1"], pcm.tobytes())


def decode(clip):
    """Ogg Opus -> 16 kHz mono int16 bytes (ffmpeg's own decoder: an independent check of the stream)."""
    return ffmpeg(["-f", "ogg", "-i", "pipe:0", "-f", "s16le", "-ar", str(RATE), "-ac", "1", "pipe:1"], clip)


def ogg_samples(clip):
    """What core/sound.cpp's reader will accept: OggS pages, OpusHead, no packet across a page, last granule.
    Returns the clip's length in 16 kHz samples (granule minus pre-skip, at 48 kHz in the stream)."""
    i, pages, gran, pre = 0, 0, 0, None
    while i < len(clip):
        assert clip[i:i + 4] == b"OggS" and clip[i + 4] == 0, "not an Ogg page"
        nseg = clip[i + 26]
        segs = clip[i + 27:i + 27 + nseg]
        body = i + 27 + nseg
        assert nseg and segs[-1] != 255, "a packet runs on into the next page"   # the core reader stops there
        if pages == 0:
            assert clip[body:body + 8] == b"OpusHead" and clip[body + 9] == 1, "no mono OpusHead"
            pre = struct.unpack_from("<H", clip, body + 10)[0]
        gran = struct.unpack_from("<q", clip, i + 6)[0]
        i = body + sum(segs)
        pages += 1
    assert i == len(clip) and pages >= 3 and pre is not None
    return (gran - pre) // 3


def pack(clips, per_lang):
    """[Ogg clip] -> "UPO1", u16 clips, u16 clips per language, then (u32 offset, u32 bytes) each, then the clips."""
    head = b"UPO1" + struct.pack("<HH", len(clips), per_lang)
    base, table, at = len(head) + 8 * len(clips), b"", 0
    for c in clips:
        table += struct.pack("<II", base + at, len(c))
        at += len(c)
    return head + table + b"".join(clips)


def unpack(data):
    assert data[:4] == b"UPO1", "not a UPO1 pack"
    n, per = struct.unpack_from("<HH", data, 4)
    out = []
    for k in range(n):
        off, size = struct.unpack_from("<II", data, 8 + 8 * k)
        assert off + size <= len(data)
        out.append(data[off:off + size])
    return out, per


def clip(text, lang, wav):
    """One line: download (cached), shape, encode; optionally a decoded .wav to listen to."""
    enc = encode(shape(fetch(text, lang)))
    if wav:
        import wave
        wav.parent.mkdir(parents=True, exist_ok=True)
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(decode(enc))
    return enc


# ---- one clip: download (cached), trim, level, soft-limit ---------------------------------------
def spoken(text):
    """What the voice reads: the line behind a delivery tag (the tag is not spoken)."""
    return TAG + text


def fetch(text, lang):
    text = spoken(text)
    cache = ROOT / "tools/.voice_cache"
    cache.mkdir(exist_ok=True)
    tag = hashlib.sha1(json.dumps([VOICE_ID, MODEL, SETTINGS, text]).encode()).hexdigest()[:16]
    f = cache / f"{lang}_{tag}.pcm"
    if not f.exists():
        key = os.environ.get("ELEVENLABS_API_KEY")
        if not key: raise SystemExit("ELEVENLABS_API_KEY is not set (source ~/.gizli-env)")
        req = urllib.request.Request(
            f"https://api.elevenlabs.io/v1/text-to-speech/{VOICE_ID}?output_format=pcm_{RATE}",
            data=json.dumps({"text": text, "model_id": MODEL, "voice_settings": SETTINGS}).encode(),
            headers={"xi-api-key": key, "content-type": "application/json"})
        f.write_bytes(urllib.request.urlopen(req, timeout=120).read())
    import numpy as np
    return np.frombuffer(f.read_bytes(), "<i2").astype(np.float32) / 32768


def shape(x):
    """Trim silence, level every line to the same loudness, soft-limit: nothing jumps out."""
    import numpy as np
    win = 160                                            # 10 ms
    env = np.sqrt(np.convolve(x * x, np.ones(win) / win, "same"))
    on = np.nonzero(env > 0.01)[0]
    x = x[max(0, on[0] - 480):on[-1] + 1600] if len(on) else x   # 30 ms before, 100 ms after
    speech = env[on] if len(on) else env
    x = x * (0.09 / max(1e-4, float(np.sqrt(np.mean(speech ** 2)))))   # same, soft speech RMS for every line
    x = np.tanh(x * 1.5) / np.tanh(1.5) * 0.6            # soft limiter, peaks under -4.4 dBFS
    fade = np.linspace(0, 1, 160)
    x[:160] *= fade
    x[-160:] *= fade[::-1]
    return np.round(x * 32767).astype(np.int16)


def header_text():
    keys = ", ".join(f"V_{k}" for k in KEYS)
    names = ", ".join(f"NL_{k[:-2]}" for k in PKEYS)
    return ("// GENERATED by tools/voice.py - do not edit.\n#pragma once\n"
            f"// her spoken lines; V_NONE = say nothing. fw/data/voice.bin holds them for {len(LANGS)} languages\n"
            f"enum Voice {{ V_NONE, {keys}, V_COUNT }};\n"
            "// the private name pack (fw/personal/voice_name.bin, Turkish only), in its order\n"
            f"enum NameLine {{ {names}, NL_COUNT }};\n")


def routines():
    sys.path.insert(0, str(ROOT / "tools"))
    import routines as R
    return R


def routine_lines():
    """The routine pack's clips in a fixed order (Turkish only): per question its 10 ways of asking, then
    10 yes and 10 no answers (TEETH_PM shares TEETH_AM's answers); then unclear, happy ask/reply/silent, greet."""
    R = routines()
    out = []
    for key, q in R.QUESTIONS.items():
        out += [(f"{key}_ASK{i}", a) for i, a in enumerate(q["ask"])]
        if "answers" not in q:
            out += [(f"{key}_YES{i}", a) for i, a in enumerate(R.YES[key])]
            out += [(f"{key}_NO{i}", a) for i, a in enumerate(R.NO[key])]
    for name, lst in (("UNCLEAR", R.UNCLEAR), ("HAPPY_ASK", R.HAPPY_ASK), ("HAPPY_REPLY", R.HAPPY_REPLY),
                      ("HAPPY_SILENT", R.HAPPY_SILENT), ("GREET_AM", R.GREET_AM)):
        out += [(f"{name}{i}", a) for i, a in enumerate(lst)]
    return out


def routine_header():
    """core/routine.gen.h: the questions, when they are asked and where their clips sit in routines_tr.bin.
    No words in it: the pack (and the child's name) stay private."""
    R = routines()
    keys = list(R.QUESTIONS) + ["HAPPY"]
    idx = {k: i for i, (k, _) in enumerate(routine_lines())}
    when = {"am": "QW_AM", "pm": "QW_PM", "pm-last": "QW_PM_LAST"}
    days = {"all": "QD_ALL", "school": "QD_SCHOOL", "free": "QD_FREE"}
    W, D, AT, ASK, YES, NO = [], [], [], [], [], []
    for k, q in R.QUESTIONS.items():
        w = q["when"]
        W.append(when.get(w, "QW_AT"))
        AT.append(0 if w in when else int(w[:2]) * 60 + int(w[3:]))
        D.append(days[q["days"]])
        a = q.get("answers", k)
        ASK.append(idx[f"{k}_ASK0"]); YES.append(idx[f"{a}_YES0"]); NO.append(idx[f"{a}_NO0"])
    W.append("QW_OPEN"); D.append("QD_ALL"); AT.append(0); ASK.append(idx["HAPPY_ASK0"])
    j = lambda xs: ", ".join(str(x) for x in xs)
    return ("// GENERATED by tools/voice.py from tools/routines.py - do not edit.\n#pragma once\n"
            "// Her daily-routine questions (Q_HAPPY is the open one at the end of the day), and where each list\n"
            "// of 10 variants starts in fw/personal/routines_tr.bin.\n"
            f"enum Question {{ {', '.join('Q_' + k for k in keys)}, Q_COUNT, Q_YESNO = Q_HAPPY }};\n"
            "enum QWhen { QW_AM, QW_PM, QW_PM_LAST, QW_AT, QW_OPEN };   // morning hour, evening, last of the evening, a time\n"
            "enum QDays { QD_ALL, QD_SCHOOL, QD_FREE };                  // every day, school days, weekends and holidays\n"
            f"static const u8 Q_WHEN[Q_COUNT] = {{{j(W)}}};\n"
            f"static const u8 Q_DAYS[Q_COUNT] = {{{j(D)}}};\n"
            f"static const u16 Q_AT[Q_COUNT] = {{{j(AT)}}};   // minute of the day, for QW_AT\n"
            f"static const u16 Q_ASK[Q_COUNT] = {{{j(ASK)}}};\n"
            f"static const u16 Q_YES[Q_YESNO] = {{{j(YES)}}};\n"
            f"static const u16 Q_NO[Q_YESNO] = {{{j(NO)}}};\n"
            f"enum {{ R_VARIANTS = 10, R_UNCLEAR = {idx['UNCLEAR0']}, R_HAPPY_REPLY = {idx['HAPPY_REPLY0']}, "
            f"R_HAPPY_SILENT = {idx['HAPPY_SILENT0']}, R_GREET = {idx['GREET_AM0']}, R_CLIPS = {len(idx)} }};\n"
            f"static const char* const Q_KEY[Q_COUNT] = {{{', '.join(chr(34) + k + chr(34) for k in keys)}}};   // X-Question\n")


def personal_conf():
    cfg = Path.home() / ".config/unicorn-pet/personal.json"
    return json.loads(cfg.read_text(encoding="utf-8")) if cfg.exists() else None


def write(dst, data, what):
    dst.parent.mkdir(exist_ok=True)
    dst.write_bytes(data)
    print(f"ok {dst.relative_to(ROOT)} {len(data) // 1024} KB, {what}")


def personal(wav_dir):
    """fw/personal/voice_name.bin (the lines with her name, Turkish only) and config.json for the toy."""
    conf = personal_conf()
    if not conf: raise SystemExit("no ~/.config/unicorn-pet/personal.json: write {\"name\": ...} there first")
    # Turkish only, like the routines (the child's language); in any other language the toy says her
    # usual lines without the name. One language keeps the private pack small.
    said = conf.get("spoken", {}).get("tr", conf["name"])
    clips = [clip(v[0].replace("{name}", said), "tr", wav_dir and wav_dir / f"p_tr_{k}.wav") for k, v in PERSONAL.items()]
    write(ROOT / "fw/personal/voice_name.bin", pack(clips, len(clips)), f"{len(clips)} clips (private, gitignored)")
    # what the toy needs to reach the listener (server/API.md); never printed
    if not conf.get("token"): print("note: no token in personal.json yet (python3 server/listener.py --setup)")
    toy = {"listener": conf.get("listener", ""), "token": conf.get("token", "")}   # set by server/listener.py --setup
    dst = ROOT / "fw/personal/config.json"
    dst.write_text(json.dumps(toy) + "\n", encoding="utf-8")
    os.chmod(dst, 0o600)
    print(f"ok {dst.relative_to(ROOT)} (listener and token; private, gitignored)")


def build_routines(wav_dir):
    conf = personal_conf() or {}
    name = conf.get("spoken", {}).get("tr", conf.get("name", ""))
    clips = []
    for key, text in routine_lines():
        text = text.replace("{name}", name) if name else text.replace("{name}, ", "").replace(" {name}", "")
        clips.append(clip(text, "tr", wav_dir and wav_dir / f"r_{key}.wav"))
    write(ROOT / "fw/personal/routines_tr.bin", pack(clips, len(clips)), f"{len(clips)} clips")


def check_pack(path, n_want, langs=1):
    """Every clip parses as the core's reader will read it, and ffmpeg decodes it to real speech."""
    import array
    clips, per = unpack(path.read_bytes())
    assert len(clips) == n_want * langs and per == n_want, f"{path.name}: {len(clips)} clips, {per} per language"
    for k, c in enumerate(clips):
        n = ogg_samples(c)
        pcm = array.array("h", decode(c))
        assert RATE // 4 < n < RATE * 8 and abs(len(pcm) - n) < 400, (path.name, k, n, len(pcm))
        peak = max(abs(v) for v in pcm)
        assert 3000 < peak < 32000, (path.name, k, peak)   # spoken, and soft-limited
    print(f"ok {path.relative_to(ROOT)}: {len(clips)} clips decode")


def main():
    gen, rgen = ROOT / "core/voice.gen.h", ROOT / "core/routine.gen.h"
    if "--check" in sys.argv:          # no network: the headers match the sources, every clip decodes
        for f, t in ((gen, header_text()), (rgen, routine_header())):
            if not f.exists() or f.read_text() != t: raise SystemExit(f"{f} is stale: run tools/voice.py")
        check_pack(ROOT / "fw/data/voice.bin", len(KEYS), len(LANGS))
        for f, n in (("voice_name.bin", len(PKEYS)), ("routines_tr.bin", len(routine_lines()))):
            if (ROOT / "fw/personal" / f).exists(): check_pack(ROOT / "fw/personal" / f, n)
        print("ok voice.gen.h, routine.gen.h"); return
    wav_dir = Path(sys.argv[sys.argv.index("--wav") + 1]) if "--wav" in sys.argv else None
    rgen.write_text(routine_header())
    if "--personal" in sys.argv:
        personal(wav_dir); return
    if "--routines" in sys.argv:
        build_routines(wav_dir); return
    clips = [clip(LINES[key][li], lang, wav_dir and wav_dir / f"{lang}_{key}.wav")
             for li, lang in enumerate(LANGS) for key in KEYS]
    secs = sum(ogg_samples(c) for c in clips) / RATE
    write(ROOT / "fw/data/voice.bin", pack(clips, len(KEYS)), f"{len(clips)} clips, {secs:.0f} s of speech")
    gen.write_text(header_text())


if __name__ == "__main__":
    main()
