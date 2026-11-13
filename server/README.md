# Home listener

A small server on the parent's Mac that the toy talks to over the home Wi-Fi (contract: [API.md](API.md)):

- **Spoken answers to text.** The toy asks a routine question ("dişlerini fırçaladın mı?"), records the
  answer and posts the WAV. A voice-activity check answers `silent` at once for a quiet clip; otherwise
  Whisper (whisper.cpp, Turkish forced) transcribes it and Turkish rules turn it into `yes` / `no` / `unclear`.
- **The day's journal.** The evening's open question ("bugün seni en çok ne mutlu etti?"): the transcript
  goes into a dated Markdown file, the recording next to it.
- **TV test mode.** 5 s samples, each with a TV / not-TV guess, for the parent to label on the page.
- **The parent page** (Turkish, phone-friendly, PIN): routines, journal, TV labelling, toy settings and status.
  The toy owns its settings; the page is a remote control that reaches the toy the next time it connects.

## Privacy

Nothing leaves the Mac. Whisper and the TV classifier run locally; nothing is uploaded anywhere and the
listener makes no outgoing connections. Plain HTTP on the home LAN, guarded by a shared token (toy) and a
PIN (parent page). Bind is on all interfaces, so keep it on a home network you trust.

What is stored, and how to delete it:

| What | Where | Delete |
|---|---|---|
| Journal: transcripts + recordings | `journal_dir` (default `~/Documents/<name> günlüğü/`) | "Sil" on the Günlük tab, or delete the files |
| TV samples + labels | `tv_dir` (default `~/Documents/unicorn-tv-samples/`) | delete the folder |
| Settings, toy status, last 5 weeks of answers/stars | `~/Library/Application Support/unicorn-listener/state.json` | delete the file |
| Whisper + YAMNet models, Python venv | `~/Library/Application Support/unicorn-listener/` | delete the folder |
| Token, PIN, the child's name, paths | `~/.config/unicorn-pet/personal.json` (mode 600) | edit the file |
| Log: time, IP, path, status, answer class (no transcripts) | `~/Library/Logs/unicorn-listener.log` | delete the file |

Yes/no answers are not recorded: their audio is transcribed in memory and dropped.

## Setup

```sh
server/setup.sh            # brew install whisper.cpp (if missing), models, venv, token + PIN
server/run.sh              # try it in the foreground (Ctrl-C to stop)
server/install-launchd.sh  # keep it running at login (--remove to take it out)
```

`setup.sh` adds to `~/.config/unicorn-pet/personal.json` (only the missing ones): `listener`
(`<this Mac>.local:8765`, which the toy uses), `token` (random, shared with the toy through
`tools/voice.py --personal`), `parent_pin` (random 6 digits: read it in that file), `journal_dir`,
`tv_dir`. Change any of them there and restart.

Parent page: `http://<this Mac>.local:8765/` from a phone on the same Wi-Fi, or `http://localhost:8765/` on
the Mac itself (no PIN there). Five wrong PINs lock that device out for 15 minutes.

If macOS asks whether Python may accept incoming connections, allow it; with the firewall on and no prompt,
add `~/Library/Application Support/unicorn-listener/venv/bin/python` under System Settings → Network →
Firewall → Options. The Mac must be awake: when it sleeps the toy falls back to its on-screen buttons.

## Choices

**Whisper model: `small`** (466 MB). Measured on this M4 MacBook Air with whisper.cpp 1.9.5 (Metal), 5 s
clips of Turkish TTS (macOS `say -v Yelda`), the model kept loaded in `whisper-server`:

| model (kept loaded) | median | p90 | right, 22 phrases × 2 |
|---|---|---|---|
| base | 0.25 s | 0.61 s | ~80%: "yemedim"→"yemediğim", "daha değil"→"daha da iyi" |
| small | 0.69 s | 1.33 s | 40/44: "hı hı"→"bir bak", "henüz değil"→"heinez dil" |
| **small + answer prompt** | **0.67 s** | **1.14 s** | **42/44**: only "hı hı" |
| large-v3-turbo q5_0 | 2.94 s | 3.36 s | 44/44, but its encoder alone takes 2.4 s per clip |
| large-v3-turbo q5_0, audio_ctx 768 | 1.30 s | 1.72 s | 38/44; at 512 it loops ("Su içmedin. Su içmedin.") past 5 s |

End to end through the listener (`server/e2e.py`, 4 s clips incl. silence/noise): 24/25 right, speech
median 0.31 s (max 0.65 s), silence 0.01 s without touching Whisper.

`small` is the one that answers inside 1–2 s; turbo is more accurate but takes 3 s, half the toy's 6 s budget.
A short prompt with the expected answers (`Evet. Hayır. Hı hı. Tamam. Daha değil. Henüz değil.`) fixed
"henüz değil". To switch, set `MODEL` in listener.py and the file name in setup.sh. Caveat: the test voice is
an adult TTS voice; a five-year-old's speech will be harder, and the /v1/answer logs will tell.

**Voice activity:** an energy check on 30 ms frames (10 dB over the clip's own noise floor, at least
240 ms) so silence never reaches Whisper; plus a filter for Whisper's subtitle hallucinations ("Altyazı
M.K.", "İzlediğiniz için teşekkürler", "[Müzik]" …).

**Yes/no rules** (`classify_yesno`): Turkish-aware lower case, ASCII-folded, repeated letters collapsed.
"bilmiyorum"/"emin değilim"/"belki" → unclear; a negative (hayır, yok, değil, unuttum, -madım/-medim,
-mıyorum, -acağım) wins over a positive verb; then yes-words and any affirmative first-person past tense
(-dım/-tım); English last. "Unutmadım" ("I didn't forget") is a yes, also after "hayır".

**TV guess:** YAMNet (AudioSet, 4 MB TFLite through LiteRT, ~5 ms per window on the CPU) scores media
classes (Television, Radio, Speech synthesizer, Music, Theme/Background/Soundtrack music) and is mixed
with how much of the 5 s is continuous sound (a TV plays on; a child near the toy talks in bursts). Without
the venv it uses the loudness part alone. The weights are hand-set: label samples on the TV tab and the
accuracy there tells whether they need retuning.

## Checks

```sh
make -C test listener                 # pure Python: yes/no rules, WAV + voice checks, sync, HTTP with a fake Whisper
python3 server/e2e.py                 # real Whisper: Turkish clips via `say`, on a throwaway config and port
```
