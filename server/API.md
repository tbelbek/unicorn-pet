# Toy ↔ home server (the "listener") — contract

The toy never talks to the internet for this. It talks to one machine on the home network (the
parent's MacBook) that turns her questions' spoken answers into text with Whisper, keeps the child's
"what made you happy today" answers in a private journal, and (test mode only) collects short audio
samples to learn what the TV sounds like. Nothing here is personal data in the repo: the server address,
the shared token and the child's name live in `~/.config/unicorn-pet/personal.json` on the Mac, and
the toy gets them in its private config file (`/unicorn/config.json` on the SD card or the internal
LittleFS partition, built by `tools/voice.py --personal`).

## Transport
- HTTP/1.1 on the LAN, default port **8765**, host from the config (`"listener": "my-mac.local:8765"`,
  resolved with mDNS). Plain HTTP is acceptable on the home LAN; every request carries
  `X-Token: <token>` from the config and the server answers 401 without it.
- The server drops a connection that sends nothing for 10 s and handles at most 16 at once (more are closed
  at once), so a half-sent request cannot tie it up.
- Audio: `Content-Type: audio/wav`, 16 kHz, mono, 16-bit PCM WAV (at most 10 s, ~320 KB).
- Every response is JSON. The toy waits at most **2 s** for /v1/ping and **6 s** for /v1/answer (and 6 s for
  the others), one deadline over the whole request: name lookup, connect, upload and the full reply; on
  timeout or any error it falls back to the on-screen ✔ / ✖ buttons (or, for the open question, to
  "voice heard → nice reply").

## Endpoints
| Method | Path | Request | Response |
|---|---|---|---|
| GET | `/v1/ping` | — | `{"ok": true, "server": "unicorn-listener", "version": 1}` |
| POST | `/v1/answer` | WAV body; headers `X-Question: TEETH_AM` (a key of tools/routines.py QUESTIONS, or `HAPPY`), `X-Kind: yesno` or `open`, `X-Recorded-At: 2026-11-13T19:05:00+01:00` (toy local time) | yesno: `{"answer": "yes"|"no"|"unclear"|"silent", "text": "<transcript>"}`; open: `{"answer": "said"|"silent", "text": "<transcript>"}` — an open answer is written to the journal |
| POST | `/v1/journal` | WAV body; headers `X-Question`, `X-Recorded-At` — an open answer recorded while the server was unreachable, uploaded later | `{"ok": true, "text": "<transcript>"}` |
| POST | `/v1/tv-sample` | WAV body (5 s); header `X-Recorded-At` — TV test mode only | `{"guess": "tv"|"not_tv", "confidence": 0.0-1.0}`; the server stores the sample with its guess for the parent to label |

## Classification (`/v1/answer`, yesno)
- `silent`: no speech (voice-activity check before Whisper, or an empty transcript).
- `yes` / `no`: Turkish keyword rules on the normalised transcript (evet, hı hı, tamam, fırçaladım,
  yedim, içtim, giydim, taradım, topladım, gittim … / hayır, yok, daha değil, fırçalamadım, yemedim …,
  negative suffixes -madım/-medim win over a plain verb), then English as a fallback.
- anything else: `unclear`.

## Journal (open answers)
`~/Documents/<name> günlüğü/YYYY-MM-DD.md` (the config's journal_dir; that is its default), one entry per
answer: time, the question asked, the transcript; the audio next to it as `YYYY-MM-DD_HHMMSS.wav`.

## TV test mode
What the toy does (core/game.cpp `check_tv`, `level_result`): by day only (never at bedtime, never with an
untrusted clock), once a minute it turns the microphone on for about a second and measures the ambient level on
the toy itself (nothing stored or sent). Only when two checks in a row find sustained sound (level at least
`TV_LEVEL_DB` in at least `TV_SUSTAIN_PCT` % of the frames: one slammed door does not count) does it record 5 s
and POST them to `/v1/tv-sample`: at most 6 samples an hour, and none within 10 minutes of the last while the same
sound goes on (unless its level changed by `TV_CHANGE_DB` or a quiet minute came between). On battery the toy
joins WiFi just for that sample; on USB power the link is up anyway. Test mode switches itself off 7 days after
it was switched on (`tv_test_since`).
Samples go to `~/Documents/unicorn-tv-samples/` (from the config) as `<timestamp>.wav` + `<timestamp>.json`
(`{"guess", "confidence", "label": null}`). A local page at `http://localhost:8765/tv` lists them with a
player, the guess, and two buttons (TV / normal) that set `label`; it shows the accuracy so far
(agreement of guess with label, and a confusion count). The toy does nothing else in test mode.

## Parent page and settings sync (added)
The listener also serves a **parent web page** on the LAN at `http://<mac>.local:8765/` (all pages
except the toy's /v1 endpoints need a parent login: a PIN/password from personal.json `"parent_pin"`,
session cookie, HttpOnly, SameSite=Strict; rate-limit wrong PINs). It is the comfortable place to set what the
toy's tiny settings screen also sets. Tabs, in Turkish (the parent's language), mobile-friendly:
- **Rutinler**: on/off; each question on/off; weekday wake, weekend wake, bedtime; holiday mode with
  optional date ranges; the weekly table (which question was asked, the answer yes/no/unclear/✔/✖, stars).
- **Günlük**: the "what made you happy" entries by date with transcript and an audio player; delete an entry.
- **TV**: test mode on/off; the labelling list (this is the /tv page) with accuracy and confusion.
- **Oyuncak**: language, volume, brightness, play limit, break length, calm mode, shake; read-only: last
  seen, battery %, charging, firmware version, update available (the update itself stays on the toy).

Sync — the toy is the owner of its state, the page is a remote control:
| Method | Path | Request | Response |
|---|---|---|---|
| GET | `/v1/config` | `X-Token`; `X-Config-Rev: <rev the toy has>` | `{"rev": N, "settings": {...}}` or 304 when unchanged |
| POST | `/v1/status` | JSON `{"rev": N, "settings": {...}, "battery": 0-100|-1, "plugged": bool, "fw": N, "update": N|0, "day": {"answers": [{"q": "TEETH_AM", "a": "yes", "t": "07:12"}], "stars": 3}}` | `{"ok": true}` |
`settings` keys: `routines` (bool), `questions` ({key: bool}), `wake_weekday`, `wake_weekend`, `bedtime`
("HH:MM", any minute: the toy keeps the minute; its own screen steps by 10), `holiday` (bool), `holiday_ranges` ([["YYYY-MM-DD","YYYY-MM-DD"]]), `tv_test` (bool), `tv_test_since`
("YYYY-MM-DD", or ""; the page sets it to today when it turns test mode on; the toy keeps that date, however late
the config reaches it, and ends test mode 7 days after it, at once if those are already over), `lang`
(tr|en|…), `volume` (0 normal,1 low,2 off), `brightness` (1-255), `play_min`, `break_min`, `calm`, `shake` (bool).
Every change on either side bumps `rev`; the toy applies a newer page config when it connects and posts
its full state after any connection, so a change made on the toy's own screen shows up on the page next time.
On a conflict (both changed since the last sync) the toy's own change wins for that key (the person holding
the toy is right there). The toy marks a key as "changed here" until a status carrying that change is acknowledged
(200); a key changed again while that request was out stays marked for the next status. The toy connects at its usual moments only (answers, clock sync, TV samples), so a
page change reaches it within hours; the page says "will reach the toy next time it connects (last seen …)".

Server details (as built, server/parent.py): unknown keys and invalid values in a toy's `settings` are dropped
(the page refuses them; a date must be a real calendar day); `questions` may be partial (missing keys stay on).
A status of the wrong shape (not an object, a bad `rev`, `day.answers` not a list) gets 400. The conflict rule is a three-way
merge against the settings the toy reported last time: a key whose value differs from that report is the
toy's change and wins; every other key keeps the page's value. `questions` is merged the same way per question,
so the toy switching one off does not undo the page switching another. After the merge `rev` becomes the toy's rev
when the toy already has exactly the merged settings (so its next GET is a 304), otherwise one more than
both. A missing or non-numeric `X-Config-Rev` always gets the full config. `day` is stored under the Mac's
date when it arrives. The parent page opens without the PIN only in a browser on the Mac itself
(`http://localhost:8765/`, which is also where `/tv` lives).
