#!/usr/bin/env python3
"""Unicorn Pet home listener: the toy's spoken answers -> text, on the parent's Mac. Contract: server/API.md.

    python3 server/listener.py --setup   # add token/defaults to ~/.config/unicorn-pet/personal.json (mode 600)
    python3 server/listener.py           # serve on the LAN (port from "listener", default 8765)

Stdlib only, except the TV guess, which uses numpy + LiteRT (YAMNet) when present and falls back to a
loudness feature otherwise; server/run.sh starts it from the venv server/setup.sh made with those. Speech to text is a
whisper-server child process (Homebrew whisper.cpp) bound to 127.0.0.1, so the model stays loaded.
"""
import array
import hmac
import html
import io
import json
import math
import os
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
import uuid
import wave
from http.cookies import SimpleCookie
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import routines  # noqa: E402  (question keys and the wording she uses)
import parent  # noqa: E402

VERSION = 1
PERSONAL = Path(os.environ.get("UNICORN_PERSONAL", "~/.config/unicorn-pet/personal.json")).expanduser()
DATA = Path("~/Library/Application Support/unicorn-listener").expanduser()
MODEL = "small"  # chosen by measurement, see server/README.md
# the answers we expect, as Whisper's initial prompt: fixed "henüz değil" -> "Heinez dil" on small
PROMPT = "Evet. Hayır. Hı hı. Tamam. Daha değil. Henüz değil."
WHISPER_PORT = int(os.environ.get("UNICORN_WHISPER_PORT", 8766))
MAX_BODY = 1_000_000
MAX_SECONDS = 10.5
SOCKET_TIMEOUT = 10  # s without a byte from a client: its connection is dropped (a half-sent body cannot hold a thread)
MAX_CONNECTIONS = 16  # at once; more are closed right away instead of queueing threads


# ---------------------------------------------------------------- config

def defaults(cfg):
    try:
        host = subprocess.run(["scutil", "--get", "LocalHostName"], capture_output=True, text=True).stdout.strip()
    except OSError:
        host = ""
    return {
        "listener": f"{host or socket.gethostname().split('.')[0]}.local:8765",
        "journal_dir": f"~/Documents/{cfg.get('name', 'Unicorn')} günlüğü",
        "tv_dir": "~/Documents/unicorn-tv-samples",
    }


def setup():
    cfg = json.loads(PERSONAL.read_text()) if PERSONAL.exists() else {}
    added = [k for k, v in defaults(cfg).items() if cfg.setdefault(k, v) is v]
    if not cfg.get("token"):
        cfg["token"] = secrets.token_urlsafe(24)
        added.append("token")
    if not cfg.get("parent_pin"):
        cfg["parent_pin"] = f"{secrets.randbelow(10**6):06d}"
        added.append("parent_pin (the parent page's PIN: read it there)")
    PERSONAL.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(PERSONAL, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)
    os.chmod(PERSONAL, 0o600)
    print(f"{PERSONAL}: " + (f"added {', '.join(added)}" if added else "already complete") + " (mode 600)")


def load_config():
    cfg = json.loads(PERSONAL.read_text())
    if not cfg.get("token"):
        sys.exit(f"no token in {PERSONAL}: run `python3 server/listener.py --setup` first")
    cfg = {**defaults(cfg), **cfg}
    cfg["port"] = int(cfg["listener"].rsplit(":", 1)[1])
    cfg["journal"] = Path(cfg["journal_dir"]).expanduser()
    cfg["tv"] = Path(cfg["tv_dir"]).expanduser()
    cfg["state"] = Path(os.environ.get("UNICORN_STATE", DATA / "state.json"))
    return cfg


# ---------------------------------------------------------------- audio

def read_wav(body):
    """-> 16-bit samples, or raise ValueError with the reason. Only 16 kHz mono 16-bit PCM, at most 10 s."""
    try:
        with wave.open(io.BytesIO(body)) as w:
            if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, 16000):
                raise ValueError(f"need 16 kHz mono 16-bit, got {w.getframerate()} Hz "
                                 f"{w.getnchannels()} ch {8 * w.getsampwidth()} bit")
            if w.getnframes() > MAX_SECONDS * 16000:
                raise ValueError("longer than 10 s")
            frames = w.readframes(w.getnframes())
    except (wave.Error, EOFError) as e:
        raise ValueError(f"not a PCM WAV: {e}")
    pcm = array.array("h")
    pcm.frombytes(frames[: len(frames) // 2 * 2])
    if sys.byteorder == "big":
        pcm.byteswap()
    return pcm


def frame_db(pcm, frame=480):
    """RMS level of each 30 ms frame in dBFS."""
    out = []
    for i in range(0, len(pcm) - frame + 1, frame):
        s = sum(x * x for x in pcm[i:i + frame]) / frame
        out.append(10 * math.log10(s / 32768**2) if s else -100.0)
    return out


def voiced_frames(pcm):
    """Frames loud enough to be someone talking near the toy: 10 dB over the clip's own noise floor, above
    -48 dBFS, and anything over -35 dBFS counts (so a clip that is talking from start to end still has
    voice). shortcut: energy only (no spectral check), so a cough or a door bang passes; Whisper then
    hears nothing and the answer becomes unclear/silent anyway."""
    db = frame_db(pcm)
    if not db:
        return db, []
    floor = sorted(db)[len(db) // 10]
    thr = max(-48, min(floor + 10, -35))
    return db, [d > thr for d in db]


def has_voice(pcm, min_ms=240):
    _, v = voiced_frames(pcm)
    return sum(v) * 30 >= min_ms


# ---------------------------------------------------------------- transcript -> answer

_FOLD = str.maketrans("çğıöşüâîû", "cgiosuaiu")
# Whisper's habits on silence/noise (trained on subtitled video); a sentence containing one is dropped
HALLUCINATIONS = ["altyazi", "izlediginiz icin", "abone ol", "begenmeyi unutmayin", "bir sonraki videoda",
                  "videoyu begen", "thanks for watching", "thank you for watching", "subtitles by", "amara org",
                  "abone olmayi", "kanalima abone", "iyi seyirler", "altyazi mk"]
NOISE_LABELS = {"muzik"}  # a sentence that is only this is a noise label ("Müzik."); "Müzik dinledim." stays


def fold(text):
    """Turkish-aware lower case, ASCII-folded, punctuation gone, repeated letters collapsed ("eveeet" -> "evet")."""
    t = text.replace("I", "ı").replace("İ", "i").lower().translate(_FOLD)
    t = re.sub(r"[^a-z0-9 ]+", " ", t)
    return re.sub(r"(.)\1+", r"\1", t).split()


def clean_transcript(text):
    text = re.sub(r"\[[^\]]*\]|\([^)]*\)|\*[^*]*\*|♪+", " ", text)  # [Müzik], (gülüşmeler), *alkış*, ♪
    keep = [s for s in re.split(r"(?<=[.!?])\s+", text)
            if not any(h in " ".join(fold(s)) for h in HALLUCINATIONS) and " ".join(fold(s)) not in NOISE_LABELS]
    out = " ".join(" ".join(keep).split())
    return out if fold(out) else ""


def _c(*words):  # keyword lists go through the same collapse as the transcript
    return {" ".join(fold(w)) for w in words}


UNCLEAR_W = _c("bilmiyorum", "bilmiyom", "bilmem", "hatırlamıyorum", "hatırlamıyom", "anlamadım", "anlamıyorum",
               "duymadım", "duyamadım", "belki", "galiba")
YES_W = _c("evet", "evt", "he", "hıhı", "ıhı", "mhm", "aynen", "tamam", "tamamdır", "tabii", "elbette", "oldu",
           "bitti", "bitirdim", "fırçaladım", "fırçaladık", "yedim", "yedik", "içtim", "içtik", "giydim", "giyindim",
           "giyindik", "taradım", "topladım", "topladık", "gittim", "gittik", "aldım", "ettim", "yaptım", "yaptık",
           "değiştirdim", "çıkardım", "koydum", "uğradım", "unutmadım", "unutmadık")
NO_W = _c("hayır", "yok", "yo", "değil", "değilim", "olmaz", "olmadı", "henüz", "unuttum", "unuttuk", "cık")
YES_EN = {"yes", "yeah", "yep", "yup", "sure", "ok", "okay", "okey", "done"}
NO_EN = {"no", "nope", "nah", "not", "didnt", "didn"}
DIDNT_FORGET = _c("unutmadım", "unutmadık")
NOT_A_VERB = _c("yardım", "adım", "kadim", "efendim")  # end like a past-tense verb, aren't


def classify_yesno(text):
    """Normalised transcript -> "yes" | "no" | "unclear" ("silent" is decided before, on the audio)."""
    w = fold(text)
    joined = " " + " ".join(w) + " "
    if any(t in UNCLEAR_W for t in w) or " emin degil" in joined:
        return "unclear"
    if set(w) & DIDNT_FORGET:  # "hayır, unutmadım": the hayır agrees with "...unutmadın, değil mi?"
        w = [t for t in w if t not in ("hayir", "yok")]
    yes, no = " hi hi " in joined, False  # "hı hı" is a yes, but a negative after it still wins
    for t in w:
        if t in YES_W:  # explicit list first: "unutmadım" (I didn't forget) is a yes
            yes = True
        elif t in NO_W or re.search(r"m[ae]d[iu][mkn]?$|m[iu]yo|[ae]c[ae][kg]", t):
            no = True  # -madım/-medim, -mıyorum, -acağım (not done yet)
        elif len(t) >= 5 and re.search(r"[dt][iu]m$", t) and t not in NOT_A_VERB:
            yes = True  # any other affirmative past tense, 1st person: "sildim", "kapattım"
    if no:
        return "no"  # a negative wins over a plain verb: "yok fırçalamadım", "evet ama yemedim"
    if yes:
        return "yes"
    if " not yet " in joined or any(t in NO_EN for t in w):
        return "no"
    if any(t in YES_EN for t in w):
        return "yes"
    return "unclear"


# ---------------------------------------------------------------- whisper

class Whisper:
    """A whisper-server child on 127.0.0.1, (re)started on demand."""

    def __init__(self):
        self.bin = os.environ.get("UNICORN_WHISPER_SERVER") or shutil.which("whisper-server") \
            or "/opt/homebrew/bin/whisper-server"
        self.model = Path(os.environ.get("UNICORN_WHISPER_MODEL", DATA / f"ggml-{MODEL}.bin"))
        self.proc = None
        self.lock = threading.Lock()

    def start(self):
        with self.lock:
            if self.proc and self.proc.poll() is None:
                return
            if not self.model.exists():
                raise RuntimeError(f"no Whisper model at {self.model}: run server/setup.sh")
            # -sns drops [Müzik]-style tokens; temperature fallback off so noise can't cost 5 decodes
            self.proc = subprocess.Popen(
                [self.bin, "-m", str(self.model), "-l", "tr", "-nt", "-sns", "-t", "4", "-nf",
                 "--host", "127.0.0.1", "--port", str(WHISPER_PORT)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(300):
                try:
                    urllib.request.urlopen(f"http://127.0.0.1:{WHISPER_PORT}/health", timeout=0.5).read()
                    return
                except OSError:
                    if self.proc.poll() is not None:
                        raise RuntimeError("whisper-server exited at start")
                    time.sleep(0.1)
            raise RuntimeError("whisper-server did not come up")

    def transcribe(self, wav):
        self.start()
        b = uuid.uuid4().hex
        fields = {"response_format": "json", "language": "tr", "temperature": "0.0", "no_timestamps": "true",
                  "prompt": PROMPT}
        body = b"".join(f'--{b}\r\nContent-Disposition: form-data; name="{k}"\r\n\r\n{v}\r\n'.encode()
                        for k, v in fields.items())
        body += (f'--{b}\r\nContent-Disposition: form-data; name="file"; filename="a.wav"\r\n'
                 f'Content-Type: audio/wav\r\n\r\n').encode() + wav + f"\r\n--{b}--\r\n".encode()
        req = urllib.request.Request(f"http://127.0.0.1:{WHISPER_PORT}/inference", body,
                                     {"Content-Type": f"multipart/form-data; boundary={b}"})
        with urllib.request.urlopen(req, timeout=5.5) as r:
            return json.loads(r.read()).get("text", "").strip()

    def stop(self):
        if self.proc:
            self.proc.terminate()


def hear(whisper, wav, pcm):
    """-> cleaned transcript, "" when nobody spoke (no Whisper call at all for a quiet clip)."""
    return clean_transcript(whisper.transcribe(wav)) if has_voice(pcm) else ""


# ---------------------------------------------------------------- journal + TV samples

def recorded_at(value):
    try:
        return datetime.fromisoformat(value)
    except (TypeError, ValueError):
        return datetime.now().astimezone()


def reserve(path):
    """A new empty file at path, or path-1, path-2 ...: created exclusively, so two uploads with the same
    timestamp never get the same name."""
    n, p = 1, path
    while True:
        try:
            os.close(os.open(p, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600))
            return p
        except FileExistsError:
            p, n = path.with_name(f"{path.stem}-{n}{path.suffix}"), n + 1


def question_text(key, name):
    ask = routines.HAPPY_ASK if key == "HAPPY" else routines.QUESTIONS[key]["ask"]
    return ask[0].format(name=name)


def write_journal(cfg, key, when, text, wav):
    cfg["journal"].mkdir(parents=True, exist_ok=True)
    audio = reserve(cfg["journal"] / f"{when:%Y-%m-%d_%H%M%S}.wav")
    audio.write_bytes(wav)
    md = cfg["journal"] / f"{when:%Y-%m-%d}.md"
    with parent.JOURNAL_LOCK:  # the same lock as the page's delete, which rewrites this file
        head = "" if md.exists() else f"# {when:%Y-%m-%d}\n"
        with md.open("a", encoding="utf-8") as f:
            f.write(f"{head}\n## {when:%H:%M} — {question_text(key, cfg.get('name', ''))}\n\n{text}\n\n"
                    f"[ses]({audio.name})\n")


TV_CLASSES = {5: "Speech synthesizer", 132: "Music", 262: "Background music", 263: "Theme music",
              265: "Soundtrack music", 518: "Television", 519: "Radio"}
_yamnet = None


def yamnet_scores(pcm):
    """Mean YAMNet class scores over ~1 s windows, or None without numpy/LiteRT/the model."""
    global _yamnet
    try:
        import numpy as np
        from ai_edge_litert.interpreter import Interpreter
    except ImportError:
        return None
    path = DATA / "yamnet.tflite"
    if not path.exists():
        return None
    if _yamnet is None:
        _yamnet = Interpreter(model_path=str(path))
        _yamnet.allocate_tensors()
    x = np.asarray(pcm, dtype=np.float32) / 32768.0
    x = np.pad(x, (0, max(0, 15600 - len(x))))
    inp, out = _yamnet.get_input_details()[0]["index"], _yamnet.get_output_details()[0]["index"]
    scores = []
    for i in range(0, len(x) - 15600 + 1, 8000):
        _yamnet.set_tensor(inp, x[i:i + 15600])
        _yamnet.invoke()
        scores.append(_yamnet.get_tensor(out)[0].copy())
    return np.mean(scores, axis=0)


def tv_guess(pcm):
    """-> (guess, confidence, why). TV/speaker media is continuous sound with music or a TV-ish timbre;
    a child answering the toy is a short burst between silences. shortcut: hand-set weights, untrained;
    retune them from the labels the /tv page collects."""
    db, v = voiced_frames(pcm)
    active = sum(v) / len(v) if v else 0.0
    loud = sum(d > -50 for d in db) / len(db) if db else 0.0  # steady sound at all, even if not speech-loud
    s = yamnet_scores(pcm)
    if s is None:
        score, why = 0.5 * active + 0.5 * loud, {"active": round(active, 2), "sound": round(loud, 2)}
    else:
        media = float(sum(s[i] for i in TV_CLASSES))
        score = 0.55 * min(1.0, 2 * media) + 0.25 * active + 0.2 * loud
        top = sorted(range(len(s)), key=lambda i: -s[i])[:3]
        why = {"media": round(media, 2), "active": round(active, 2), "sound": round(loud, 2),
               "top": [[YAMNET_LABELS.get(i, str(i)), round(float(s[i]), 2)] for i in top]}
    guess = "tv" if score >= 0.5 else "not_tv"
    return guess, round(score if guess == "tv" else 1 - score, 2), why


YAMNET_LABELS = {0: "Speech", 1: "Child speech", 2: "Conversation", 3: "Narration", 13: "Laughter", 24: "Singing",
                 494: "Silence", 500: "Inside, small room", 506: "Noise", **TV_CLASSES}


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "unicorn-listener"
    timeout = SOCKET_TIMEOUT  # per socket read/write (StreamRequestHandler applies it)
    cfg = whisper = store = auth = None

    def log_message(self, fmt, *args):  # method, path, status: never headers, PINs or transcripts
        sys.stderr.write(f"{datetime.now():%Y-%m-%d %H:%M:%S} {self.client_address[0]} {fmt % args}\n")

    def send(self, code, obj=None, body=None, ctype="application/json", headers=()):
        body = body if body is not None else json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype + ("; charset=utf-8" if ctype.startswith(("text", "app")) else ""))
        self.send_header("Content-Length", str(len(body)))
        for k, v in headers:
            self.send_header(k, v)
        if ctype == "text/html":
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Frame-Options", "DENY")
        self.end_headers()
        self.wfile.write(body)

    def redirect(self, where, headers=()):
        self.send(303, body=b"", ctype="text/plain", headers=[("Location", where), *headers])

    # -- who is asking
    def authorised(self):
        """The toy: X-Token from the config."""
        if hmac.compare_digest(self.headers.get("X-Token", "").encode(), self.cfg["token"].encode()):
            return True
        self.send(401, {"error": "token"})
        return False

    def is_local(self):
        """This Mac under its own name (a DNS-rebinding page has another Host, so it cannot pass)."""
        host = self.headers.get("Host", "").rsplit(":", 1)[0].strip("[]")
        return self.client_address[0] in ("127.0.0.1", "::1") and host in ("localhost", "127.0.0.1", "::1")

    def sid(self):
        c = SimpleCookie(self.headers.get("Cookie", ""))
        return c["sid"].value if "sid" in c else ""

    def parent_ok(self, page=True):
        """The parent: a session cookie from the PIN login, or a browser on the Mac itself."""
        if self.command == "POST":  # SameSite=Strict already stops cross-site posts; a mismatched Origin too
            origin = self.headers.get("Origin")
            if origin and urlsplit(origin).netloc != self.headers.get("Host"):
                self.send(403, {"error": "origin"})
                return False
        if self.is_local() or self.auth.valid(self.sid()):
            return True
        if page:
            self.redirect("/login")
        else:
            self.send(401, {"error": "login"})
        return False

    def body(self, limit=MAX_BODY):
        try:
            n = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self.send(411, {"error": "Content-Length required"})
            return None
        if n > limit or n < 0:
            self.close_connection = True
            self.send(413, {"error": f"body over {limit} bytes"})
            return None
        return self.rfile.read(n)

    def form(self):
        b = self.body(20_000)
        return None if b is None else parse_qs(b.decode("utf-8", "replace"), keep_blank_values=True)

    # -- routes
    def do_GET(self):
        url = urlsplit(self.path)
        path, q = url.path, parse_qs(url.query)
        if path == "/v1/ping":
            if self.authorised():
                self.send(200, {"ok": True, "server": "unicorn-listener", "version": VERSION})
        elif path == "/v1/config":
            if self.authorised():
                try:
                    rev = int(self.headers.get("X-Config-Rev", "-1"))
                except ValueError:
                    rev = -1
                with self.store.lock:
                    res = parent.config_for(self.store.load(), rev)
                if res is None:
                    self.send_response(304)
                    self.end_headers()
                else:
                    self.send(200, res)
        elif path == "/login":
            self.send(200, body=parent.login_page(), ctype="text/html")
        elif path in ("/", "/tv"):
            if self.parent_ok():
                tab = "tv" if path == "/tv" else q.get("t", ["rutinler"])[0]
                with self.store.lock:
                    state = self.store.load()
                body = {"rutinler": lambda: parent.tab_rutinler(state), "gunluk": lambda: parent.tab_gunluk(self.cfg),
                        "tv": lambda: parent.tab_tv(self.cfg, state), "oyuncak": lambda: parent.tab_oyuncak(state)}
                tab = tab if tab in body else "rutinler"
                err = q.get("e", [""])[0]
                note = f'<p class="note">Kaydedilemedi: {html.escape(err)}</p>' if err else ""
                self.send(200, body=parent.page("Unicorn Pet", note + body[tab](), tab), ctype="text/html")
        elif m := re.fullmatch(r"/(tv|journal)/audio/([\w-]+\.wav)", path):
            if self.parent_ok(page=False):
                folder = self.cfg["tv" if m[1] == "tv" else "journal"]
                f = folder / m[2]
                if f.is_file():
                    self.send(200, body=f.read_bytes(), ctype="audio/wav")
                else:
                    self.send(404, {"error": "no such recording"})
        else:
            self.send(404, {"error": "not found"})

    def do_POST(self):
        path = urlsplit(self.path).path
        if path == "/login":
            return self.login()
        if path == "/logout":
            self.auth.logout(self.sid())
            return self.redirect("/login", [("Set-Cookie", "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict")])
        if path in ("/settings", "/journal/delete", "/tv/label"):
            if self.parent_ok(page=path != "/tv/label"):
                {"/settings": self.save_settings, "/journal/delete": self.delete_entry, "/tv/label": self.tv_label}[path]()
            return
        if path == "/v1/status":
            if self.authorised() and (b := self.body(64_000)) is not None:
                try:
                    with self.store.lock:
                        state = parent.toy_report(self.store.load(), json.loads(b))
                        self.store.save(state)
                except (ValueError, TypeError, AttributeError) as e:  # a payload of the wrong shape: 400, not a dropped line
                    return self.send(400, {"error": str(e)})
                self.send(200, {"ok": True})
            return
        if path not in ("/v1/answer", "/v1/journal", "/v1/tv-sample"):
            return self.send(404, {"error": "not found"})
        self.audio(path)

    def audio(self, path):
        t0 = time.monotonic()
        if not self.authorised() or (wav := self.body()) is None:
            return
        try:
            pcm = read_wav(wav)
        except ValueError as e:
            return self.send(400, {"error": str(e)})
        when = recorded_at(self.headers.get("X-Recorded-At"))
        if path == "/v1/tv-sample":
            return self.tv_sample(wav, pcm, when)
        key = self.headers.get("X-Question", "")
        kind = "open" if path == "/v1/journal" else self.headers.get("X-Kind", "")
        if key != "HAPPY" and key not in routines.QUESTIONS or kind not in ("yesno", "open"):
            return self.send(400, {"error": "X-Question / X-Kind"})
        try:
            text = hear(self.whisper, wav, pcm)
        except Exception as e:  # noqa: BLE001  the toy falls back to its buttons on any error
            self.log_message("whisper failed: %s", type(e).__name__)
            return self.send(503, {"error": "speech to text unavailable"})
        if kind == "open" and text:
            write_journal(self.cfg, key, when, text, wav)
        if path == "/v1/journal":
            res = {"ok": True, "text": text}
        elif kind == "open":
            res = {"answer": "said" if text else "silent", "text": text}
        else:
            res = {"answer": classify_yesno(text) if text else "silent", "text": text}
        self.log_message("%s %s %s -> %s in %.2f s", path, key, kind, res.get("answer", "ok"), time.monotonic() - t0)
        self.send(200, res)

    def login(self):
        f = self.form()
        if f is None:
            return
        ip = self.client_address[0]
        if self.auth.locked(ip):
            return self.send(429, body=parent.login_page("Çok fazla yanlış deneme. 15 dakika sonra tekrar deneyin."),
                             ctype="text/html")
        sid = self.auth.login(ip, f.get("pin", [""])[-1].strip())
        if not sid:
            return self.send(401, body=parent.login_page("PIN yanlış."), ctype="text/html")
        self.redirect("/", [("Set-Cookie", f"sid={sid}; Path=/; Max-Age={parent.Auth.SESSION_S}; HttpOnly; "
                                           "SameSite=Strict")])

    def save_settings(self):
        f = self.form()
        if f is None:
            return
        tab = f.get("_tab", ["rutinler"])[-1]
        try:
            with self.store.lock:
                state = self.store.load()
                if parent.page_change(state, parent.form_patch(f, state)):
                    self.store.save(state)
        except ValueError as e:
            return self.redirect(f"/?t={tab}&e={urllib.parse.quote(str(e))}")
        self.redirect(f"/?t={tab}")

    def delete_entry(self):
        f = self.form()
        if f is not None:
            parent.journal_delete(self.cfg["journal"], f.get("wav", [""])[-1])
            self.redirect("/?t=gunluk")

    def tv_sample(self, wav, pcm, when):
        guess, conf, why = tv_guess(pcm)
        self.cfg["tv"].mkdir(parents=True, exist_ok=True)
        f = reserve(self.cfg["tv"] / f"{when:%Y-%m-%d_%H%M%S}.wav")
        f.write_bytes(wav)
        f.with_suffix(".json").write_text(json.dumps(
            {"guess": guess, "confidence": conf, "label": None, "why": why}, ensure_ascii=False))
        self.send(200, {"guess": guess, "confidence": conf})

    def tv_label(self):
        raw = self.body(10_000)
        if raw is None:
            return
        try:
            req = json.loads(raw)
            name, label = str(req["name"]), req["label"]
        except (ValueError, KeyError, TypeError):
            return self.send(400, {"error": "json"})
        f = self.cfg["tv"] / f"{name}.json"
        if not re.fullmatch(r"[\w-]+", name) or label not in ("tv", "not_tv") or not f.exists():
            return self.send(400, {"error": "name/label"})
        d = json.loads(f.read_text())
        d["label"] = label
        f.write_text(json.dumps(d, ensure_ascii=False))
        self.send(200, {"ok": True})


class Server(ThreadingHTTPServer):
    """At most max_connections handled at once; any more are closed at once."""
    daemon_threads = True

    def __init__(self, addr, handler, max_connections=MAX_CONNECTIONS):
        super().__init__(addr, handler)
        self.slots = threading.BoundedSemaphore(max_connections)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            return self.shutdown_request(request)
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


def serve(cfg, whisper, port, max_connections=MAX_CONNECTIONS):
    Handler.cfg, Handler.whisper = cfg, whisper
    Handler.store, Handler.auth = parent.Store(cfg["state"]), parent.Auth(cfg.get("parent_pin", ""))
    return Server(("0.0.0.0", port), Handler, max_connections)


def main():
    if "--setup" in sys.argv:
        return setup()
    cfg = load_config()
    if not cfg.get("parent_pin"):
        print("warning: no parent_pin: the parent page only opens on this Mac (run --setup)", file=sys.stderr)
    whisper = Whisper()
    try:
        whisper.start()  # load the model now, not on the child's first answer
    except (RuntimeError, OSError) as e:
        print(f"warning: {e}; answers will get 503 until it works", file=sys.stderr)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))  # launchd stop: take whisper-server down too
    srv = serve(cfg, whisper, cfg["port"])
    print(f"unicorn-listener on :{cfg['port']} (whisper {whisper.model.name}); parent page "
          f"http://{cfg['listener']}/", file=sys.stderr)
    try:
        srv.serve_forever()
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        whisper.stop()


if __name__ == "__main__":
    main()
