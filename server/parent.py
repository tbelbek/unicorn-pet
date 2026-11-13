"""The parent page (Turkish) and the toy <-> page settings sync of server/API.md. Stdlib only.

Sync model: the toy owns its state, the page is a remote control. The state file keeps the page's
current settings, `rev`, and the settings the toy last reported (`toy_settings`). On /v1/status a key
the toy changed since its last report wins; any other key keeps the page's value (a three-way merge).
"""
import hmac
import html
import json
import os
import re
import secrets
import sys
import threading
import time
from datetime import date, datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import routines  # noqa: E402

QKEYS = list(routines.QUESTIONS) + ["HAPPY"]
LANGS = ["tr", "en", "es", "fr", "de", "pt", "it", "ru", "sv", "id"]  # tools/i18n.py LANGS, the toy's order
DEFAULTS = {  # the toy's own defaults (core/unicorn.h); its first report replaces whatever differs
    "routines": True, "questions": {k: True for k in QKEYS}, "wake_weekday": "07:00", "wake_weekend": "08:00",
    "bedtime": "19:30", "holiday": False, "holiday_ranges": [], "tv_test": False, "tv_test_since": "",
    "lang": "tr", "volume": 0, "brightness": 180, "play_min": 10, "break_min": 30, "calm": False, "shake": True,
}
TV_TEST_DAYS = 7
REV_MAX = 2**32 - 1  # the toy keeps rev as a uint32 (and refuses a config with a bigger one)
_TIME = re.compile(r"([01]\d|2[0-3]):[0-5]\d")
_DATE = re.compile(r"\d{4}-\d{2}-\d{2}")
JOURNAL_LOCK = threading.Lock()  # every change to a journal day file: an answer's append and a delete


def _is_date(v):
    """A real calendar day as YYYY-MM-DD (not 2026-99-99)."""
    try:
        return isinstance(v, str) and bool(_DATE.fullmatch(v)) and 2000 <= date.fromisoformat(v).year <= 2099  # the toy's
    except ValueError:
        return False


# ---------------------------------------------------------------- settings + sync

def _valid(k, v):
    if k in ("routines", "holiday", "tv_test", "calm", "shake"):
        return isinstance(v, bool)
    if k in ("wake_weekday", "wake_weekend", "bedtime"):
        return isinstance(v, str) and bool(_TIME.fullmatch(v))
    if k == "questions":
        return isinstance(v, dict) and all(q in QKEYS and isinstance(b, bool) for q, b in v.items())
    if k == "holiday_ranges":
        try:
            return isinstance(v, list) and len(v) <= 20 and all(
                isinstance(r, list) and len(r) == 2 and all(_is_date(d) for d in r)
                and date.fromisoformat(r[0]) <= date.fromisoformat(r[1]) for r in v)
        except (TypeError, ValueError):
            return False
    if k == "tv_test_since":
        return v == "" or _is_date(v)
    if k == "lang":
        return v in LANGS
    ranges = {"volume": (0, 2), "brightness": (1, 255), "play_min": (1, 240), "break_min": (1, 240)}
    if k in ranges:
        return type(v) is int and ranges[k][0] <= v <= ranges[k][1]
    return False


def sanitize(settings, strict=False):
    """Known keys with valid values only. strict: any bad key is a ValueError (the page); else dropped (the toy)."""
    if not isinstance(settings, dict):
        raise ValueError("settings must be an object")
    bad = [k for k, v in settings.items() if not _valid(k, v)]
    if strict and bad:
        raise ValueError(f"bad settings: {', '.join(map(str, bad))}")
    out = {k: v for k, v in settings.items() if k not in bad}
    if "questions" in out:
        out["questions"] = {**DEFAULTS["questions"], **out["questions"]}
    return out


def new_state():
    return {"rev": 0, "settings": json.loads(json.dumps(DEFAULTS)), "toy_settings": json.loads(json.dumps(DEFAULTS)),
            "toy": {}, "days": {}, "served": {}}


SERVED_MAX = 32  # snapshots kept for a toy that never reports back (one per GET of a new rev, so few)


def page_change(state, patch):
    """The parent saved a form: apply it, bump rev when anything changed. Returns True if it changed."""
    patch = sanitize(patch, strict=True)
    new = {**state["settings"], **patch}
    if new == state["settings"]:
        return False
    state["settings"], state["rev"] = new, (state["rev"] + 1) & REV_MAX
    return True


def toy_report(state, body, today=None):
    """POST /v1/status: merge the toy's settings (its own changes win), keep its status and today's answers."""
    if not isinstance(body, dict):
        raise ValueError("JSON object expected")
    toy_rev = body.get("rev", 0)
    if type(toy_rev) is not int or not 0 <= toy_rev <= REV_MAX:
        raise ValueError("rev")
    toy = sanitize(body.get("settings") or {})
    # What the toy started from: the config it fetched since its last report (served at toy_rev), else what it
    # reported last time. A value that differs from that is the toy's own change; an equal one is just an
    # acknowledgement, even when the page has moved on since (a status that arrives late cannot undo it).
    sv = state.setdefault("served", {})
    served = sv.get(str(toy_rev)) if toy_rev != state["toy"].get("rev") else None
    base = served if served is not None else state["toy_settings"]
    if served is not None:  # acknowledged (with any older one): the toy is past them
        for k in [k for k in sv if int(k) <= toy_rev]:
            del sv[k]
    page = state["settings"]
    merged = {**page, **{k: v for k, v in toy.items() if v != base.get(k)}}
    if "questions" in toy:  # per question: the toy switching WATER off does not undo the page switching TEETH_AM off
        bq = base.get("questions", {})
        merged["questions"] = {**page["questions"], **{q: on for q, on in toy["questions"].items() if on != bq.get(q)}}
    toy_full = {**base, **toy}
    state["settings"], state["toy_settings"] = merged, toy_full
    # equal to what the toy has: its rev, so its next GET is a 304; otherwise newer than both
    state["rev"] = (max(state["rev"], toy_rev) + (0 if merged == toy_full else 1)) & REV_MAX
    st = state["toy"]
    st.update(rev=toy_rev, last_seen=datetime.now().astimezone().isoformat(timespec="seconds"))
    for k, ok in (("battery", lambda v: type(v) is int and -1 <= v <= 100), ("plugged", lambda v: type(v) is bool),
                  ("fw", lambda v: type(v) is int and v >= 0), ("update", lambda v: type(v) is int and v >= 0)):
        if ok(body.get(k)):
            st[k] = body[k]
    day = body.get("day")
    if isinstance(day, dict):
        if not isinstance(day.get("answers", []), list):
            raise ValueError("day.answers must be a list")
        answers = [{"q": a["q"], "a": str(a.get("a", ""))[:10], "t": str(a.get("t", ""))[:5]}
                   for a in day.get("answers", [])[:60] if isinstance(a, dict) and a.get("q") in QKEYS]
        stars = day.get("stars") if type(day.get("stars")) is int and 0 <= day["stars"] <= len(QKEYS) else 0
        today = today or date.today().isoformat()
        state["days"][today] = {"answers": answers, "stars": stars}
        for old in sorted(state["days"])[:-35]:  # five weeks are plenty for a weekly table
            del state["days"][old]
    return state


def config_for(state, toy_rev):
    """GET /v1/config: None (= 304) when the toy already has this rev. Otherwise the settings handed out are kept
    in state["served"] (the caller saves the state) until the toy acknowledges that rev: a value it reports back
    unchanged is then an acknowledgement, not its own change (a status that arrives late cannot undo the page)."""
    if toy_rev == state["rev"]:
        return None
    sv = state.setdefault("served", {})
    sv[str(state["rev"])] = json.loads(json.dumps(state["settings"]))
    for old in sorted(sv, key=int)[:-SERVED_MAX]:
        del sv[old]
    return {"rev": state["rev"], "settings": state["settings"]}


class Store:
    """state.json in the app-support folder, written atomically; one lock for every read-modify-write."""

    def __init__(self, path):
        self.path, self.lock = Path(path), threading.Lock()

    def load(self):
        try:
            s = {**new_state(), **json.loads(self.path.read_text())}
            if type(s["rev"]) is not int or not 0 <= s["rev"] <= REV_MAX:  # (an older server took any rev)
                last = s["toy"].get("rev")
                s["rev"] = ((last if type(last) is int and 0 <= last <= REV_MAX else 0) + 1) & REV_MAX
            return s
        except (OSError, ValueError):
            return new_state()

    def save(self, state):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(state, ensure_ascii=False, indent=1))
        os.replace(tmp, self.path)


# ---------------------------------------------------------------- login

class Auth:
    SESSION_S, WINDOW_S, MAX_FAILS = 30 * 86400, 15 * 60, 5

    def __init__(self, pin):
        self.pin, self.sessions, self.fails, self.lock = str(pin), {}, {}, threading.Lock()

    def locked(self, ip):
        now = time.time()
        with self.lock:
            self.fails[ip] = [t for t in self.fails.get(ip, []) if now - t < self.WINDOW_S]
            return len(self.fails[ip]) >= self.MAX_FAILS

    def login(self, ip, pin):
        """-> session id, or None (wrong PIN, counted)."""
        if self.locked(ip):
            return None
        if self.pin and hmac.compare_digest(pin.encode(), self.pin.encode()):
            sid = secrets.token_urlsafe(32)
            with self.lock:
                self.sessions[sid] = time.time() + self.SESSION_S
                self.fails.pop(ip, None)
            return sid
        with self.lock:
            self.fails.setdefault(ip, []).append(time.time())
        time.sleep(0.5)
        return None

    def valid(self, sid):
        with self.lock:
            return bool(sid) and self.sessions.get(sid, 0) > time.time()

    def logout(self, sid):
        with self.lock:
            self.sessions.pop(sid, None)


# ---------------------------------------------------------------- journal + TV data

JOURNAL_WAV = re.compile(r"\d{4}-\d{2}-\d{2}_\d{6}(-\d+)?\.wav")


def journal_entries(folder):
    """[(date, [(time, question, text, wav name)])], newest day first."""
    out = []
    for md in sorted(Path(folder).glob("????-??-??.md"), reverse=True):
        items = []
        for sec in md.read_text(encoding="utf-8").split("\n## ")[1:]:
            head, _, rest = sec.partition("\n")
            when, _, q = head.partition(" — ")
            m = re.search(r"\[ses\]\(([^)]+)\)", rest)
            text = re.sub(r"\[ses\]\([^)]+\)", "", rest).strip()
            items.append((when, q, text, m[1] if m else ""))
        out.append((md.stem, items))
    return out


def journal_delete(folder, wav):
    """Remove one entry (its section and its audio). False if there is no such entry."""
    if not JOURNAL_WAV.fullmatch(wav):
        return False
    md = Path(folder) / f"{wav[:10]}.md"
    with JOURNAL_LOCK:  # an answer arriving meanwhile is appended before or after, never lost
        if not md.exists():
            return False
        secs = md.read_text(encoding="utf-8").split("\n## ")
        keep = [secs[0]] + [s for s in secs[1:] if f"[ses]({wav})" not in s]
        if len(keep) == len(secs):
            return False
        if len(keep) == 1:
            md.unlink()
        else:
            md.write_text("\n## ".join(keep), encoding="utf-8")
        (Path(folder) / wav).unlink(missing_ok=True)
    return True


def tv_samples(folder):
    out = []
    for j in sorted(Path(folder).glob("*.json"), reverse=True):
        try:
            out.append((j.stem, json.loads(j.read_text())))
        except (OSError, ValueError):
            pass
    return out


def tv_stats(samples):
    c = {(g, l): 0 for g in ("tv", "not_tv") for l in ("tv", "not_tv")}
    for _, d in samples:
        if d.get("label") in ("tv", "not_tv") and d.get("guess") in ("tv", "not_tv"):
            c[d["guess"], d["label"]] += 1
    n = sum(c.values())
    return n, (c["tv", "tv"] + c["not_tv", "not_tv"]) / n if n else None, c


# ---------------------------------------------------------------- pages

E = html.escape
CSS = """:root{--bg:#fff;--fg:#222;--mut:#666;--line:#ddd;--acc:#7a4fd0;--card:#f6f3fc}
@media (prefers-color-scheme:dark){:root{--bg:#121014;--fg:#eee;--mut:#aaa;--line:#333;--acc:#b597ff;--card:#1d1922}}
*{box-sizing:border-box}body{font:16px/1.4 system-ui,sans-serif;margin:0 auto;max-width:860px;padding:12px 16px;
background:var(--bg);color:var(--fg)}nav{display:flex;gap:4px;flex-wrap:wrap;margin:8px 0 16px}
nav a{padding:8px 12px;border-radius:8px;text-decoration:none;color:var(--fg);background:var(--card)}
nav a.on{background:var(--acc);color:var(--bg)}h1{font-size:20px;margin:4px 0}h2{font-size:17px;margin:20px 0 8px}
.card{background:var(--card);border-radius:10px;padding:12px;margin:8px 0}label{display:flex;justify-content:space-between;
align-items:center;gap:12px;padding:6px 0;border-bottom:1px solid var(--line)}label:last-child{border:0}
input,select,textarea,button{font:inherit;color:inherit}input[type=time],input[type=number],select,textarea,input[type=password]{
background:var(--bg);border:1px solid var(--line);border-radius:6px;padding:6px}textarea{width:100%}
input[type=checkbox]{width:22px;height:22px;accent-color:var(--acc)}button{background:var(--acc);color:var(--bg);border:0;
border-radius:8px;padding:9px 16px;margin-top:8px}button.alt{background:var(--card);color:var(--fg);border:1px solid var(--line)}
button.on{outline:2px solid var(--fg)}.mut{color:var(--mut);font-size:14px}.wrap{overflow-x:auto}
table{border-collapse:collapse;width:100%;font-size:14px}td,th{padding:6px;border-bottom:1px solid var(--line);text-align:left}
audio{width:100%;max-width:280px;height:36px}.note{border-left:3px solid var(--acc);padding:6px 10px;margin:8px 0}"""
TABS = [("rutinler", "Rutinler"), ("gunluk", "Günlük"), ("tv", "TV"), ("oyuncak", "Oyuncak")]
ANSWER_TR = {"yes": "evet", "no": "hayır", "unclear": "?", "silent": "—", "tap_yes": "✔", "tap_no": "✖"}


def page(title, body, tab=None):
    nav = "".join(f'<a href="/?t={k}" class="{"on" if k == tab else ""}">{t}</a>' for k, t in TABS) if tab else ""
    out = (f'<form method="post" action="/logout" style="float:right"><button class="alt">Çıkış</button></form>'
           if tab else "")
    return (f'<!doctype html><html lang="tr"><meta charset="utf-8"><meta name="viewport" '
            f'content="width=device-width,initial-scale=1"><title>{E(title)}</title><style>{CSS}</style>'
            f'<body>{out}<h1>{E(title)}</h1><nav>{nav}</nav>{body}</body></html>').encode()


def login_page(msg=""):
    return page("Unicorn Pet — ebeveyn", f'''<form method="post" action="/login" class="card">
<label>PIN <input type="password" name="pin" inputmode="numeric" autocomplete="current-password" autofocus></label>
<button>Giriş</button>{f'<p class="mut">{E(msg)}</p>' if msg else ""}</form>''')


def _seen(state):
    t = state["toy"].get("last_seen")
    if not t:
        return "oyuncak henüz hiç bağlanmadı"
    return f"son görülme {datetime.fromisoformat(t):%d.%m.%Y %H:%M}"


def _pending(state):
    if state["toy"].get("rev") == state["rev"]:
        return ""
    return (f'<p class="note">Değişiklikler oyuncak bir sonraki bağlandığında ona ulaşır ({E(_seen(state))}). '
            f'Oyuncak cevaplarda, saat eşitlemede ve TV örneklerinde bağlanır; bu birkaç saat sürebilir.</p>')


def _check(name, on, text):
    return f'<label>{E(text)} <input type="checkbox" name="{E(name)}" {"checked" if on else ""}></label>'


QLABEL = {"TEETH_AM": "Diş (sabah)", "TEETH_PM": "Diş (akşam)", "DRESS": "Giyinme", "SCHOOL_CLOTHES": "Okul kıyafeti",
          "HAIR": "Saç", "VITAMIN": "Vitamin", "BREAKFAST": "Kahvaltı", "WATER": "Su", "DINNER": "Akşam yemeği",
          "TOYS": "Oyuncak toplama", "TOILET": "Tuvalet", "HAPPY": "Günün güzel anı"}


def _qlabel(k):
    return QLABEL.get(k, k)


def tab_rutinler(state):
    s = state["settings"]
    qs = "".join(_check(f"q_{k}", s["questions"].get(k, True), _qlabel(k)) for k in QKEYS)
    ranges = "\n".join(f"{a} {b}" for a, b in s["holiday_ranges"])
    days = [(date.today() - timedelta(days=i)).isoformat() for i in range(7)]
    head = "".join(f"<th>{E(_qlabel(k))}</th>" for k in QKEYS)
    rows = ""
    for d in days:
        day = state["days"].get(d, {"answers": [], "stars": None})
        cell = {}
        for a in day["answers"]:
            cell[a["q"]] = f'{E(ANSWER_TR.get(a["a"], str(a["a"])))} <span class="mut">{E(str(a["t"]))}</span>'
        stars = "★" * min(day["stars"], len(QKEYS)) if day.get("stars") else ("" if day.get("stars") is None else "0")
        rows += (f"<tr><td>{date.fromisoformat(d):%d.%m}</td>" + "".join(
            f"<td>{cell.get(k, '')}</td>" for k in QKEYS) + f"<td>{stars}</td></tr>")
    return f'''{_pending(state)}<form method="post" action="/settings"><input type="hidden" name="_tab" value="rutinler">
<div class="card">{_check("routines", s["routines"], "Rutin soruları açık")}</div>
<h2>Sorular</h2><div class="card">{qs}</div>
<h2>Saatler</h2><div class="card">
<label>Hafta içi uyanma <input type="time" name="wake_weekday" value="{E(s["wake_weekday"])}"></label>
<label>Hafta sonu uyanma <input type="time" name="wake_weekend" value="{E(s["wake_weekend"])}"></label>
<label>Yatma saati <input type="time" name="bedtime" value="{E(s["bedtime"])}"></label></div>
<h2>Tatil</h2><div class="card">{_check("holiday", s["holiday"], "Tatil modu (okul soruları sorulmaz)")}
<p class="mut">Tatil aralıkları, satır başına bir tane: <code>2026-12-24 2027-01-02</code></p>
<textarea name="holiday_ranges" rows="3">{E(ranges)}</textarea></div>
<button>Kaydet</button></form>
<h2>Bu hafta</h2><div class="wrap"><table><tr><th>gün</th>{head}<th>yıldız</th></tr>{rows}</table></div>
<p class="mut">evet / hayır / ? = sesli cevap, ✔ / ✖ = ekrandan.</p>'''


def tab_gunluk(cfg):
    out = []
    for d, items in journal_entries(cfg["journal"]):
        out.append(f"<h2>{date.fromisoformat(d):%d.%m.%Y}</h2>")
        for when, q, text, wav in items:
            player = (f'<audio controls preload="none" src="/journal/audio/{E(wav)}"></audio>'
                      f'<form method="post" action="/journal/delete" onsubmit="return confirm(\'Bu kayıt ve sesi '
                      f'kalıcı olarak silinsin mi?\')"><input type="hidden" name="wav" value="{E(wav)}">'
                      f'<button class="alt">Sil</button></form>') if wav else ""
            out.append(f'<div class="card"><b>{E(when)}</b> <span class="mut">{E(q)}</span><p>{E(text)}</p>{player}</div>')
    return "".join(out) or '<p class="mut">Henüz kayıt yok.</p>'


def _pct(v):
    return f"{v:.0%}" if type(v) in (int, float) else "?"


def tab_tv(cfg, state):
    s = state["settings"]
    since = s.get("tv_test_since") or ""
    since = since if _is_date(since) else ""  # (a state file from before dates were checked)
    if s["tv_test"]:
        end = (f", {date.fromisoformat(since) + timedelta(days=TV_TEST_DAYS):%d.%m.%Y} tarihinde kendiliğinden kapanır"
               if since else f", açıldıktan {TV_TEST_DAYS} gün sonra kendiliğinden kapanır")
        status = (f"Açık" + (f" ({date.fromisoformat(since):%d.%m.%Y} tarihinden beri)" if since else "") + end + ".")
        button = '<input type="hidden" name="tv_test" value="off"><button class="alt">Test modunu kapat</button>'
    else:
        status = "Kapalı" + (f" (son açılış {date.fromisoformat(since):%d.%m.%Y})" if since else "") + "."
        button = (f'<input type="hidden" name="tv_test" value="on"><button>Test modunu aç ({TV_TEST_DAYS} gün)</button>')
    samples = tv_samples(cfg["tv"])
    n, acc, c = tv_stats(samples)
    rows = []
    for name, d in samples:
        lab = d.get("label")
        rows.append(
            f'<tr data-name="{E(name)}"><td>{E(name.replace("_", " "))}<br>'
            f'<audio controls preload="none" src="/tv/audio/{E(name)}.wav"></audio></td>'
            f'<td>{"TV" if d.get("guess") == "tv" else "normal"} {E(_pct(d.get("confidence")))}'
            f'<div class="mut">{E(", ".join(f"{a} {b:.2f}" for a, b in d.get("why", {}).get("top", [])))}</div></td><td>'
            + "".join(f'<button class="alt {"on" if lab == v else ""}" onclick="lab(this,\'{v}\')">{t}</button> '
                      for v, t in (("tv", "TV"), ("not_tv", "normal"))) + "</td></tr>")
    accuracy = f"{acc:.0%} ({n} etiketli örnek)" if n else "henüz etiket yok"
    return f'''{_pending(state)}<div class="card"><p>TV test modu: <b>{E(status)}</b></p>
<p class="mut">Test modunda oyuncak dakikada bir 1 saniye dinler; sürekli ses duyarsa 5 saniyelik bir örnek gönderir
(saatte en çok 6). Örnekleri dinleyip TV mi normal mi olduğunu işaretleyin.</p>
<form method="post" action="/settings"><input type="hidden" name="_tab" value="tv">{button}</form></div>
<h2>Doğruluk: {E(accuracy)}</h2>
<p class="mut">Tahmin TV → gerçekte TV {c["tv", "tv"]}, normal {c["tv", "not_tv"]}. Tahmin normal → gerçekte normal
{c["not_tv", "not_tv"]}, TV {c["not_tv", "tv"]}.</p>
<div class="wrap"><table><tr><th>örnek</th><th>tahmin</th><th>sizin etiketiniz</th></tr>{"".join(rows)}</table></div>
<script>async function lab(b,label){{const name=b.closest('tr').dataset.name;
const r=await fetch('/tv/label',{{method:'POST',headers:{{'Content-Type':'application/json'}},body:JSON.stringify({{name,label}})}});
if(r.ok)location.reload();else alert('Kaydedilemedi: '+r.status)}}</script>'''


def tab_oyuncak(state):
    s, t = state["settings"], state["toy"]
    lang = "".join(f'<option value="{E(k)}" {"selected" if k == s["lang"] else ""}>{E(k.upper())}</option>' for k in LANGS)
    vol = "".join(f'<option value="{i}" {"selected" if i == s["volume"] else ""}>{n}</option>'
                  for i, n in enumerate(["Normal", "Kısık", "Kapalı"]))
    bat = t.get("battery")
    info = [("Son görülme", _seen(state).replace("son görülme ", "")),
            ("Pil", "—" if bat in (None, -1) else f"%{bat}"),
            ("Şarjda", {True: "evet", False: "hayır"}.get(t.get("plugged"), "—")),
            ("Yazılım sürümü", t.get("fw", "—")),
            ("Güncelleme", f"sürüm {t['update']} hazır (oyuncağın ayarlarından kurulur)" if t.get("update") else "yok")]
    return f'''{_pending(state)}<form method="post" action="/settings"><input type="hidden" name="_tab" value="oyuncak">
<div class="card"><label>Dil <select name="lang">{lang}</select></label>
<label>Ses <select name="volume">{vol}</select></label>
<label>Parlaklık <input type="range" name="brightness" min="1" max="255" value="{E(str(s["brightness"]))}"></label>
<label>Oyun süresi (dk) <input type="number" name="play_min" min="1" max="240" value="{E(str(s["play_min"]))}"></label>
<label>Mola (dk) <input type="number" name="break_min" min="1" max="240" value="{E(str(s["break_min"]))}"></label>
{_check("calm", s["calm"], "Sakin mod")}{_check("shake", s["shake"], "Sallama")}</div><button>Kaydet</button></form>
<h2>Durum</h2><div class="card">{"".join(f"<label>{k} <span>{E(str(v))}</span></label>" for k, v in info)}</div>'''


def form_patch(form, state):
    """An HTML form (parse_qs dict) -> a settings patch for the tab it came from. ValueError on bad input."""
    f = {k: v[-1] for k, v in form.items()}
    tab, on = f.get("_tab"), lambda k: k in f
    if tab == "rutinler":
        ranges = [ln.split() for ln in f.get("holiday_ranges", "").replace(",", " ").splitlines() if ln.strip()]
        return {"routines": on("routines"), "questions": {k: on(f"q_{k}") for k in QKEYS},
                "wake_weekday": f.get("wake_weekday", ""), "wake_weekend": f.get("wake_weekend", ""),
                "bedtime": f.get("bedtime", ""), "holiday": on("holiday"), "holiday_ranges": ranges}
    if tab == "oyuncak":
        try:
            nums = {k: int(f.get(k, "")) for k in ("volume", "brightness", "play_min", "break_min")}
        except ValueError:
            raise ValueError("sayı bekleniyordu")
        return {"lang": f.get("lang", ""), **nums, "calm": on("calm"), "shake": on("shake")}
    if tab == "tv":
        if f.get("tv_test") == "on":
            return {"tv_test": True, "tv_test_since": date.today().isoformat()}
        return {"tv_test": False}
    raise ValueError("form")
