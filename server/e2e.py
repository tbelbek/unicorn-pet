#!/usr/bin/env python3
"""End-to-end check of the listener with real Whisper: Turkish clips from `say -v Yelda` (an adult TTS
voice, not a child) -> 16 kHz mono WAV -> POST, with latency and accuracy. macOS only.

    python3 server/e2e.py [--model path/to/ggml-x.bin]
    "~/Library/Application Support/unicorn-listener/venv/bin/python" server/e2e.py   # with the YAMNet TV guess

It starts its own listener on a throwaway config (temp token, temp journal/TV dirs, port 18765), so the
real token, journal and TV samples are never touched.
"""
import json
import os
import secrets
import statistics
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = 18765
CASES = [  # (spoken, question, kind, expected)
    ("Evet", "TEETH_AM", "yesno", "yes"), ("Evet, fırçaladım.", "TEETH_PM", "yesno", "yes"),
    ("Hı hı", "DRESS", "yesno", "yes"), ("Tamam", "HAIR", "yesno", "yes"),
    ("Yedim", "BREAKFAST", "yesno", "yes"), ("Vitaminimi içtim", "VITAMIN", "yesno", "yes"),
    ("Okul kıyafetlerimi giydim", "SCHOOL_CLOTHES", "yesno", "yes"), ("Oyuncaklarımı topladım", "TOYS", "yesno", "yes"),
    ("Tuvalete gittim", "TOILET", "yesno", "yes"), ("Saçlarımı taradım", "HAIR", "yesno", "yes"),
    ("Hayır", "TEETH_AM", "yesno", "no"), ("Yok, fırçalamadım.", "TEETH_PM", "yesno", "no"),
    ("Daha değil", "DRESS", "yesno", "no"), ("Yemedim", "DINNER", "yesno", "no"),
    ("Su içmedim", "WATER", "yesno", "no"), ("Unuttum", "VITAMIN", "yesno", "no"),
    ("Henüz değil", "TOYS", "yesno", "no"), ("Toplamadım", "TOYS", "yesno", "no"),
    ("Bilmiyorum", "TEETH_AM", "yesno", "unclear"), ("Kırmızı balonum nerede?", "DRESS", "yesno", "unclear"),
    ("Bugün parkta arkadaşımla salıncağa bindik, çok eğlendik.", "HAPPY", "open", "said"),
    ("Annemle kek yaptık.", "HAPPY", "open", "said"),
    (None, "TEETH_AM", "yesno", "silent"), ("noise", "HAIR", "yesno", "silent"), (None, "HAPPY", "open", "silent"),
]


def clip(text, out, seconds=4.0):
    """`say` -> 16 kHz mono 16-bit WAV, 0.5 s of quiet before, padded to `seconds` like the toy's recording."""
    if text is None:
        src = ["-f", "lavfi", "-i", "anullsrc=r=16000:cl=mono"]
    elif text == "noise":  # a quiet fan / room hiss
        src = ["-f", "lavfi", "-i", "anoisesrc=r=16000:a=0.002:c=pink"]
    else:
        subprocess.run(["say", "-v", "Yelda", "-o", f"{out}.aiff", text], check=True)
        src = ["-i", f"{out}.aiff"]
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", *src, "-af", "adelay=500,apad", "-t", str(seconds),
                    "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", out], check=True)
    return Path(out).read_bytes()


def post(path, wav, token, **h):
    req = urllib.request.Request(f"http://127.0.0.1:{PORT}{path}", wav,
                                 {"X-Token": token, "Content-Type": "audio/wav",
                                  **{k.replace("_", "-"): v for k, v in h.items()}})
    t = time.monotonic()
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read()), time.monotonic() - t


def main():
    model = sys.argv[sys.argv.index("--model") + 1] if "--model" in sys.argv else None
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        token = secrets.token_urlsafe(24)
        (d / "personal.json").write_text(json.dumps({"name": "Test", "token": token, "listener": f"localhost:{PORT}",
                                                     "journal_dir": str(d / "j"), "tv_dir": str(d / "tv")}))
        env = {**os.environ, "UNICORN_PERSONAL": str(d / "personal.json"), "UNICORN_WHISPER_PORT": str(PORT + 1)}
        if model:
            env["UNICORN_WHISPER_MODEL"] = model
        t = time.monotonic()
        srv = subprocess.Popen([sys.executable, str(HERE / "listener.py")], env=env, stderr=subprocess.DEVNULL)
        try:
            for _ in range(600):
                try:
                    urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{PORT}/v1/ping",
                                                                  headers={"X-Token": token}), timeout=1)
                    break
                except (urllib.error.URLError, ConnectionError):
                    time.sleep(0.1)
            print(f"listener up in {time.monotonic() - t:.1f} s (model loaded)")
            ok, lat = 0, {"speech": [], "silent": []}
            for i, (text, q, kind, want) in enumerate(CASES):
                wav = clip(text, str(d / f"c{i}.wav"))
                res, s = post("/v1/answer", wav, token, X_Question=q, X_Kind=kind,
                              X_Recorded_At=f"2026-11-13T19:{i:02d}:00+01:00")
                ok += res["answer"] == want
                lat["silent" if want == "silent" else "speech"].append(s)
                print(f"{'ok ' if res['answer'] == want else 'BAD'} {s:5.2f}s {want:7} {res['answer']:7} "
                      f"{text or '(silence)'!s:40.40} -> {res['text']!r}")
            print(f"\naccuracy {ok}/{len(CASES)}; latency speech median {statistics.median(lat['speech']):.2f} s, "
                  f"max {max(lat['speech']):.2f} s; silence median {statistics.median(lat['silent']):.3f} s")
            journal = sorted((d / "j").glob("*"))
            print("journal:", [p.name for p in journal])
            for text in ("Bu akşam haberlerde yağmurlu bir hafta sonu bekleniyor, sıcaklıklar düşüyor ve "
                         "rüzgâr kuvvetleniyor.", "Evet", None):
                res, s = post("/v1/tv-sample", clip(text, str(d / "tv.wav"), 5.0), token)
                print(f"tv-sample {text and text[:24] or '(silence)'!s:26} -> {res} in {s:.2f} s")
        finally:
            srv.terminate()
            srv.wait()


if __name__ == "__main__":
    main()
