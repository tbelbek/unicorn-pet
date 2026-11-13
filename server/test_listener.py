"""Pure-Python checks for server/listener.py (no Whisper model, no numpy needed): `make -C test listener`."""
import array
import io
import json
import math
import os
import random
import socket
import stat
import struct
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
import wave
from http.server import ThreadingHTTPServer
from pathlib import Path

import http.client

import listener as L
import parent as P

YES = ["evet", "Evet.", "Evet fırçaladım", "EVET!", "eveeet", "Evet, fırçaladım.", "hı hı", "Hı hı.", "Hıhı",
       "hı-hı", "Tamam", "tamamdır", "he", "Heee", "aynen", "Tabii ki", "elbette", "Fırçaladım", "fırçaladık",
       "Yedim", "kahvaltımı yaptım", "kahvaltı ettim", "İçtim", "vitaminimi içtim", "Giydim", "giyindim",
       "okul kıyafetlerimi giydim", "Taradım", "saçlarımı taradım", "Topladım", "oyuncaklarımı topladım",
       "Gittim", "tuvalete gittim", "vitaminimi aldım", "bitti", "bitirdim", "Unutmadım", "hayır unutmadım",
       "Evet anne", "üstümü değiştirdim", "pijamalarımı çıkardım", "yerlerine koydum", "tuvalete uğradım",
       "sildim", "yes", "Yeah", "okay", "okey", "Mhm", "Evet. Altyazı M.K."]
NO = ["hayır", "Hayır.", "HAYIR", "hayııır", "Yok", "yok fırçalamadım", "Yok, fırçalamadım.", "yoo", "Daha değil",
      "henüz değil", "Fırçalamadım", "yemedim", "İçmedim", "giymedim", "giyinmedim", "taramadım", "toplamadım",
      "gitmedim", "Unuttum", "unuttum ya", "evet ama yemedim", "fırçaladım yok fırçalamadım", "olmadı",
      "Değil", "yapmadım", "yapamadım", "istemiyorum", "şimdi fırçalayacağım", "birazdan gideceğim",
      "yapmadık", "cık", "no", "Nope", "not yet", "Hı hı, ama yemedim.", "hı hı yok"]
UNCLEAR = ["bilmiyorum", "Bilmiyorum.", "bilmem", "Hatırlamıyorum", "emin değilim", "belki", "anlamadım", "Ne?",
           "efendim", "kırmızı balon", "hmm", "Pamuk nerede", "annem", "dişler", "", "Altyazı M.K.", "adım Pamuk",
           "galiba fırçaladım", "bilmiyorum yemedim galiba"]


def wav_bytes(samples, rate=16000, ch=1, width=2):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(ch), w.setsampwidth(width), w.setframerate(rate)
        w.writeframes(array.array("h", samples).tobytes() if width == 2 else bytes(len(samples) * width))
    return b.getvalue()


def tone(seconds, db, start=0.0, total=None, noise_db=None):
    """A 300 Hz tone of `seconds` at `db` dBFS starting at `start` inside `total` s, over optional noise."""
    total = total or seconds
    rnd = random.Random(1)
    amp, namp = 32767 * 10 ** (db / 20) * math.sqrt(2), (32767 * 10 ** (noise_db / 20) if noise_db else 0)
    out = []
    for i in range(int(total * 16000)):
        t = i / 16000
        v = amp * math.sin(2 * math.pi * 300 * t) if start <= t < start + seconds else 0
        out.append(max(-32768, min(32767, int(v + rnd.gauss(0, namp)))))
    return out


class Classify(unittest.TestCase):
    def check(self, phrases, want):
        bad = [(p, L.classify_yesno(L.clean_transcript(p))) for p in phrases
               if L.classify_yesno(L.clean_transcript(p)) != want]
        self.assertEqual(bad, [], f"should be {want}")

    def test_yes(self):
        self.check(YES, "yes")

    def test_no(self):
        self.check(NO, "no")

    def test_unclear(self):
        self.check(UNCLEAR, "unclear")

    def test_fold(self):
        self.assertEqual(L.fold("IŞIK İçtim, Hayııır!"), ["isik", "ictim", "hayir"])


class Transcript(unittest.TestCase):
    def test_hallucinations_dropped(self):
        for t in ["Altyazı M.K.", "[Müzik]", "(Müzik)", "İzlediğiniz için teşekkür ederim.", "♪♪", "...",
                  "Abone olmayı unutmayın!", "Thanks for watching!", " ", "*alkış*", "Müzik.", "MÜZİK"]:
            self.assertEqual(L.clean_transcript(t), "", t)

    def test_real_text_kept(self):
        self.assertEqual(L.clean_transcript("Evet fırçaladım. Altyazı M.K."), "Evet fırçaladım.")
        self.assertEqual(L.clean_transcript("Bugün parkta salıncağa bindim."), "Bugün parkta salıncağa bindim.")
        self.assertEqual(L.clean_transcript("Müzik dinledim."), "Müzik dinledim.")  # music, said by the child
        self.assertEqual(L.clean_transcript("Müzik. Dans ettim."), "Dans ettim.")

    def test_same_name_never_shared(self):
        with tempfile.TemporaryDirectory() as d:
            cfg = {"journal": Path(d), "name": "Test"}
            when = L.datetime(2026, 11, 13, 19, 5, 7)
            ts = [threading.Thread(target=L.write_journal, args=(cfg, "HAPPY", when, f"cevap {i}", b"W%d" % i))
                  for i in range(8)]
            [t.start() for t in ts]
            [t.join() for t in ts]
            wavs = sorted(Path(d).glob("*.wav"))
            self.assertEqual(len(wavs), 8)
            self.assertEqual(sorted(w.read_bytes() for w in wavs), sorted(b"W%d" % i for i in range(8)))
            items = P.journal_entries(d)[0][1]
            self.assertEqual(sorted(i[3] for i in items), [w.name for w in wavs], "each entry its own recording")

    def test_delete_and_append_share_one_lock(self):
        with tempfile.TemporaryDirectory() as d:
            cfg = {"journal": Path(d), "name": "Test"}
            L.write_journal(cfg, "HAPPY", L.datetime(2026, 11, 13, 19, 5, 7), "bir", b"x")
            with P.JOURNAL_LOCK:
                dl = threading.Thread(target=P.journal_delete, args=(d, "2026-11-13_190507.wav"))
                ap = threading.Thread(target=L.write_journal,
                                      args=(cfg, "HAPPY", L.datetime(2026, 11, 13, 19, 6, 0), "iki", b"y"))
                dl.start(), ap.start()
                time.sleep(0.2)
                self.assertTrue(dl.is_alive() and ap.is_alive(), "both wait for the journal lock")
            dl.join(), ap.join()
            self.assertEqual([i[2] for i in P.journal_entries(d)[0][1]], ["iki"])


class Audio(unittest.TestCase):
    def test_wav_validation(self):
        self.assertEqual(len(L.read_wav(wav_bytes([0] * 16000))), 16000)
        for bad in [wav_bytes([0] * 100, rate=44100), wav_bytes([0] * 100, ch=2), wav_bytes([0] * 100, width=1),
                    wav_bytes([0] * 16000 * 11), b"RIFF....nope", b"",
                    b"RIFF" + struct.pack("<I", 36) + b"WAVEJUNK" + struct.pack("<I", 100) + b"\0" * 24]:
            with self.assertRaises(ValueError):
                L.read_wav(bad)

    def test_voice_activity(self):
        self.assertFalse(L.has_voice(array.array("h", [0] * 80000)), "digital silence")
        self.assertFalse(L.has_voice(array.array("h", tone(0, -60, total=5, noise_db=-55))), "room hiss")
        self.assertFalse(L.has_voice(array.array("h", tone(0.1, -20, 1, 5, noise_db=-60))), "a click")
        self.assertTrue(L.has_voice(array.array("h", tone(0.6, -25, 1, 5, noise_db=-60))), "a short 'evet'")
        self.assertTrue(L.has_voice(array.array("h", tone(5, -25, total=5))), "talking the whole clip")
        self.assertTrue(L.has_voice(array.array("h", tone(0.8, -30, 1, 5, noise_db=-45))), "over a fan")

    def test_tv_guess_shape(self):
        g, c, why = L.tv_guess(array.array("h", tone(5, -25, total=5, noise_db=-40)))
        self.assertIn(g, ("tv", "not_tv"))
        self.assertTrue(0.5 <= c <= 1, c)
        g, _, _ = L.tv_guess(array.array("h", [0] * 80000))
        self.assertEqual(g, "not_tv", "silence is not TV")


class Sync(unittest.TestCase):
    def toy(self, state, rev, **changes):
        settings = {**state["toy_settings"], **changes}
        return P.toy_report(state, {"rev": rev, "settings": settings}, today="2026-11-13")

    def test_first_contact_toy_wins(self):
        s = P.new_state()
        self.toy(s, 0, bedtime="20:00", volume=1)
        self.assertEqual((s["settings"]["bedtime"], s["settings"]["volume"], s["rev"]), ("20:00", 1, 0))
        self.assertIsNone(P.config_for(s, 0), "the toy has it all: 304")

    def test_page_change_reaches_toy_and_survives_a_stale_report(self):
        s = P.new_state()
        self.assertTrue(P.page_change(s, {"bedtime": "20:30"}))
        self.assertFalse(P.page_change(s, {"bedtime": "20:30"}), "no change, no new rev")
        self.assertEqual(s["rev"], 1)
        self.assertEqual(P.config_for(s, 0), {"rev": 1, "settings": s["settings"]})
        self.toy(s, 0)  # posted before it fetched the config: the page's change must not be undone
        self.assertEqual(s["settings"]["bedtime"], "20:30")
        self.assertEqual(s["rev"], 2, "newer than both, so the toy fetches it")
        self.toy(s, 2, bedtime="20:30")  # applied it
        self.assertEqual(s["rev"], 2)
        self.assertIsNone(P.config_for(s, 2))

    def test_conflict_toy_wins_per_key(self):
        s = P.new_state()
        P.page_change(s, {"volume": 1, "brightness": 90})
        self.toy(s, 0, volume=2)  # changed on the toy's own screen meanwhile, before it fetched rev 1
        self.assertEqual((s["settings"]["volume"], s["settings"]["brightness"]), (2, 90))
        self.assertEqual(s["rev"], 2)
        self.assertEqual(P.config_for(s, 1)["settings"]["brightness"], 90)

    def test_late_status_acknowledges_not_overrides(self):
        s = P.new_state()
        P.page_change(s, {"volume": 1})  # rev 1
        fetched = P.config_for(s, 0)
        P.page_change(s, {"volume": 2})  # rev 2, while the toy's status for rev 1 is still on its way
        P.toy_report(s, {"rev": fetched["rev"], "settings": fetched["settings"]}, today="2026-11-13")
        self.assertEqual(s["settings"]["volume"], 2, "the toy only acknowledged rev 1")
        self.assertEqual(P.config_for(s, 1)["settings"]["volume"], 2)
        P.toy_report(s, {"rev": 1, "settings": {**fetched["settings"], "calm": True}}, today="2026-11-13")
        self.assertEqual((s["settings"]["volume"], s["settings"]["calm"]), (2, True), "a real toy edit still wins")

    def test_rev_stays_a_uint32(self):
        s = P.new_state()
        for bad in (2**32, -1, 2**64):
            with self.assertRaises(ValueError):
                P.toy_report(s, {"rev": bad})
        s["rev"] = P.REV_MAX
        P.page_change(s, {"calm": True})
        self.assertEqual(s["rev"], 0, "wraps instead of leaving the toy's range")
        with tempfile.TemporaryDirectory() as d:  # a state file an older server wrote with a bad rev
            st = P.Store(Path(d) / "state.json")
            st.save({**P.new_state(), "rev": 2**32, "toy": {"rev": 2**32}})
            self.assertEqual(st.load()["rev"], 1)

    def test_hostile_values_are_escaped_everywhere(self):
        x = '<script>alert(1)</script>"\''
        s = P.new_state()
        today = P.date.today().isoformat()
        s["days"][today] = {"answers": [{"q": k, "a": x[:10], "t": x} for k in P.QKEYS], "stars": 1}
        s["toy"].update(last_seen="2026-11-13T10:00:00+01:00", fw=x, battery=x, plugged=x, update=x, rev=x)
        s["settings"].update(wake_weekday=x, wake_weekend=x, bedtime=x, brightness=x, play_min=x, break_min=x,
                             holiday_ranges=[[x, x]], tv_test=True, tv_test_since=x)
        with tempfile.TemporaryDirectory() as d:
            j, tv = Path(d) / "j", Path(d) / "tv"
            j.mkdir(), tv.mkdir()
            (j / "2026-11-13.md").write_text(f"# 2026-11-13\n\n## {x} — {x}\n\n{x}\n\n[ses]({x})\n")
            (tv / "2026-11-13_200000.json").write_text(json.dumps(
                {"guess": x, "confidence": x, "label": x, "why": {"top": [[x, 0.5]]}}))
            cfg = {"journal": j, "tv": tv}
            for html in (P.tab_rutinler(s), P.tab_gunluk(cfg), P.tab_tv(cfg, s), P.tab_oyuncak(s),
                         P.page(x, "", "tv").decode(), P.login_page(x).decode()):
                self.assertEqual(html.count("<script"), html.count("<script>async"), "only the page's own script")
                self.assertNotIn('"\'', html)

    def test_toy_change_shows_on_page(self):
        s = P.new_state()
        self.toy(s, 4, calm=True, questions={"TEETH_AM": False})
        self.assertTrue(s["settings"]["calm"])
        self.assertFalse(s["settings"]["questions"]["TEETH_AM"])
        self.assertTrue(s["settings"]["questions"]["HAIR"], "a partial questions map keeps the others")
        self.assertEqual(s["rev"], 4)

    def test_questions_merge_per_question(self):
        s = P.new_state()
        P.page_change(s, {"questions": {**s["settings"]["questions"], "TEETH_AM": False}})
        self.toy(s, 0, questions={**s["toy_settings"]["questions"], "WATER": False})
        q = s["settings"]["questions"]
        self.assertEqual((q["TEETH_AM"], q["WATER"], q["HAIR"]), (False, False, True))
        self.assertFalse(P.config_for(s, 0)["settings"]["questions"]["TEETH_AM"], "the toy gets the page's change")

    def test_validation(self):
        s = P.new_state()
        self.toy(s, 1, volume=9, lang="xx", bedtime="25:00", nonsense=1, brightness=200)
        self.assertEqual((s["settings"]["volume"], s["settings"]["lang"], s["settings"]["brightness"]), (0, "tr", 200))
        self.assertNotIn("nonsense", s["settings"])
        for bad in ({"bedtime": "7:00"}, {"brightness": 0}, {"holiday_ranges": [["2026-12-02", "2026-12-01"]]},
                    {"questions": {"NOPE": True}}, {"calm": "yes"}, {"play_min": True}):
            with self.assertRaises(ValueError, msg=bad):
                P.page_change(s, bad)
        for bad in ([], {"rev": -1}, {"rev": "3"}, {"day": {"answers": None}}, {"day": {"answers": 5}}):
            with self.assertRaises(ValueError):
                P.toy_report(s, bad)
        for bad in ("2026-99-99", "2026-02-30", "26-11-13"):
            with self.assertRaises(ValueError, msg=bad):
                P.page_change(s, {"tv_test_since": bad})
            with self.assertRaises(ValueError, msg=bad):
                P.page_change(s, {"holiday_ranges": [[bad, "2027-01-01"]]})
        self.toy(s, 1, tv_test=True, tv_test_since="2026-99-99")
        self.assertEqual(s["settings"]["tv_test_since"], "", "a bad date from the toy is dropped")
        s["settings"]["tv_test_since"] = "2026-99-99"  # a state file from before: the page still renders
        self.assertIn("TV test modu", P.tab_tv({"tv": Path("/nonexistent")}, s))
        self.toy(s, 2, tv_test=True, tv_test_since="9999-12-31")  # its end date cannot even be written
        self.assertNotEqual(s["settings"]["tv_test_since"], "9999-12-31")
        s["settings"]["tv_test_since"] = "9999-12-31"
        self.assertIn("TV test modu", P.tab_tv({"tv": Path("/nonexistent")}, s))
        self.toy(s, 3, holiday_ranges=[{"2026-01-01": 0, "2026-01-02": 0}])  # not a pair: dropped, no KeyError
        self.assertEqual(s["settings"]["holiday_ranges"], [])
        today = P.date.today().isoformat()  # (the weekly table shows the last seven days)
        P.toy_report(s, {"rev": 3, "day": {"stars": 10 ** 30}}, today=today)
        self.assertEqual(s["days"][today]["stars"], 0, "more stars than questions: not kept")
        s["days"][today]["stars"] = 10 ** 30  # a state file from before: the page still renders
        self.assertIn("Bu hafta", P.tab_rutinler(s))

    def test_status_and_day(self):
        s = P.new_state()
        P.toy_report(s, {"rev": 0, "battery": 55, "plugged": False, "fw": 12, "update": 13, "day": {
            "answers": [{"q": "TEETH_AM", "a": "yes", "t": "07:12"}, {"q": "BAD", "a": "no", "t": "1"}], "stars": 3}},
            today="2026-11-13")
        self.assertEqual({k: s["toy"][k] for k in ("battery", "plugged", "fw", "update", "rev")},
                         {"battery": 55, "plugged": False, "fw": 12, "update": 13, "rev": 0})
        self.assertEqual(s["days"]["2026-11-13"], {"answers": [{"q": "TEETH_AM", "a": "yes", "t": "07:12"}], "stars": 3})
        P.toy_report(s, {"rev": 0, "battery": 500})
        self.assertEqual(s["toy"]["battery"], 55, "out-of-range status is ignored")

    def test_forms(self):
        s = P.new_state()
        f = {"_tab": ["rutinler"], "routines": ["on"], "q_HAIR": ["on"], "wake_weekday": ["06:45"],
             "wake_weekend": ["08:00"], "bedtime": ["20:00"], "holiday_ranges": ["2026-12-24 2027-01-02\n"]}
        patch = P.form_patch(f, s)
        self.assertEqual((patch["questions"]["HAIR"], patch["questions"]["TEETH_AM"], patch["holiday"]),
                         (True, False, False))
        self.assertEqual(patch["holiday_ranges"], [["2026-12-24", "2027-01-02"]])
        self.assertTrue(P.page_change(s, patch))
        tv = P.form_patch({"_tab": ["tv"], "tv_test": ["on"]}, s)
        self.assertTrue(tv["tv_test"])
        self.assertRegex(tv["tv_test_since"], r"^\d{4}-\d{2}-\d{2}$")
        P.page_change(s, tv)
        self.assertIn("kendiliğinden kapanır", P.tab_tv({"tv": Path("/nonexistent")}, s))

    def test_login_rate_limit(self):
        a = P.Auth("123456")
        P.time.sleep, sleep = (lambda s: None), P.time.sleep
        try:
            for _ in range(5):
                self.assertIsNone(a.login("10.0.0.9", "000000"))
            self.assertTrue(a.locked("10.0.0.9"))
            self.assertIsNone(a.login("10.0.0.9", "123456"), "locked even with the right PIN")
            sid = a.login("10.0.0.8", "123456")
            self.assertTrue(a.valid(sid))
            self.assertFalse(a.valid("forged"))
        finally:
            P.time.sleep = sleep


class FakeWhisper:
    text = "Evet fırçaladım"

    def transcribe(self, wav):
        return self.text


class Server(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        d = Path(cls.tmp.name)
        L.PERSONAL = d / "personal.json"
        L.PERSONAL.write_text(json.dumps({"name": "Test", "listener": "x.local:0",
                                          "journal_dir": str(d / "j"), "tv_dir": str(d / "tv")}))
        L.setup()
        L.setup()  # idempotent
        cfg = json.loads(L.PERSONAL.read_text())
        cls.token, cls.pin = cfg["token"], cfg["parent_pin"]
        os.environ["UNICORN_STATE"] = str(d / "state.json")
        L.Handler.log_message = lambda *a: None
        cls.srv = L.serve(L.load_config(), FakeWhisper(), 0)
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.url = f"http://127.0.0.1:{cls.srv.server_address[1]}"

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.tmp.cleanup()

    def call(self, path, body=None, token=True, **headers):
        h = {"X-Token": self.token} if token else {}
        h.update({k.replace("_", "-"): v for k, v in headers.items()})
        if body is not None:
            h["Content-Type"] = "audio/wav"
        try:
            with urllib.request.urlopen(urllib.request.Request(self.url + path, body, h), timeout=5) as r:
                return r.status, json.loads(r.read()) if "json" in r.headers["Content-Type"] else r.read()
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    def raw(self, method, path, body=None, host="lan.example", **h):
        """-> (status, Location, body, Set-Cookie), redirects not followed."""
        c = http.client.HTTPConnection(*self.srv.server_address[:1], self.srv.server_address[1], timeout=5)
        c.request(method, path, body, {"Host": host, **{k.replace("_", "-"): v for k, v in h.items()}})
        r = c.getresponse()
        out = r.status, r.getheader("Location"), r.read(), r.getheader("Set-Cookie")
        c.close()
        return out

    def login(self):
        st, _, _, cookie = self.raw("POST", "/login", f"pin={self.pin}".encode(),
                                 Content_Type="application/x-www-form-urlencoded")
        self.assertEqual(st, 303)
        self.assertIn("HttpOnly", cookie)
        self.assertIn("SameSite=Strict", cookie)
        return cookie.split(";")[0]

    def test_parent_page(self):
        self.assertEqual(self.raw("GET", "/")[:2], (303, "/login"))
        self.assertEqual(self.raw("POST", "/login", b"pin=000000x")[0], 401)
        sid = self.login()
        for tab in ("rutinler", "gunluk", "tv", "oyuncak"):
            st, _, body, _ = self.raw("GET", f"/?t={tab}", Cookie=sid)
            self.assertEqual(st, 200, tab)
            self.assertIn("Unicorn Pet", body.decode())
        form = "_tab=oyuncak&lang=en&volume=1&brightness=90&play_min=15&break_min=20&shake=on"
        self.assertEqual(self.raw("POST", "/settings", form.encode(), Cookie=sid)[:2], (303, "/?t=oyuncak"))
        st, _, body, _ = self.raw("GET", "/v1/config", X_Token=self.token, X_Config_Rev="0")
        cfg = json.loads(body)
        self.assertEqual((st, cfg["settings"]["lang"], cfg["settings"]["calm"], cfg["settings"]["shake"]),
                         (200, "en", False, True))
        self.assertEqual(self.raw("GET", "/v1/config", X_Token=self.token, X_Config_Rev=str(cfg["rev"]))[0], 304)
        self.assertEqual(self.raw("GET", "/v1/config", X_Config_Rev="0")[0], 401)
        # another site cannot post the form with the parent's cookie
        self.assertEqual(self.raw("POST", "/settings", form.encode(), Cookie=sid, Origin="http://evil.example")[0],
                         403)
        bad = "_tab=oyuncak&lang=xx&volume=1&brightness=90&play_min=15&break_min=20"
        self.assertIn("e=", self.raw("POST", "/settings", bad.encode(), Cookie=sid)[1])
        st, *_ = self.raw("POST", "/v1/status", json.dumps({"rev": cfg["rev"], "settings": cfg["settings"],
                                                              "battery": 80, "plugged": True, "fw": 7}).encode(),
                            X_Token=self.token)
        self.assertEqual(st, 200)
        self.assertEqual(self.raw("POST", "/v1/status", b"{nope", X_Token=self.token)[0], 400)
        for shape in ({"day": {"answers": None}}, {"settings": []}, {"rev": 1, "day": {"answers": [None]}}):
            self.assertIn(self.raw("POST", "/v1/status", json.dumps(shape).encode(), X_Token=self.token)[0], (200, 400),
                          shape)
        self.assertEqual(self.raw("POST", "/v1/status", b'{"day": {"answers": null}}', X_Token=self.token)[0], 400)
        self.assertIn("%80", self.raw("GET", "/?t=oyuncak", Cookie=sid)[2].decode())
        self.assertEqual(self.raw("POST", "/logout", b"", Cookie=sid)[0], 303)
        self.assertEqual(self.raw("GET", "/", Cookie=sid)[:2], (303, "/login"))

    def test_slow_clients_time_out_and_are_bounded(self):
        old, L.Handler.timeout = L.Handler.timeout, 0.5
        srv = L.Server(("127.0.0.1", 0), L.Handler, max_connections=2)
        threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True).start()
        try:
            slow = [socket.create_connection(srv.server_address, timeout=3) for _ in range(2)]
            for c in slow:  # one body byte of the promised hundred, then nothing
                c.sendall(b"POST /login HTTP/1.1\r\nContent-Length: 100\r\n\r\nx")
            time.sleep(0.2)
            with socket.create_connection(srv.server_address, timeout=3) as c:
                c.sendall(b"GET /v1/ping HTTP/1.1\r\n\r\n")
                try:
                    self.assertEqual(c.recv(100), b"", "a third at once is closed, not queued")
                except ConnectionResetError:
                    pass
            t0 = time.monotonic()
            for c in slow:
                self.assertEqual(c.recv(100), b"", "dropped after the socket timeout")
                c.close()
            self.assertLess(time.monotonic() - t0, 2)
            time.sleep(0.1)
            h = http.client.HTTPConnection("127.0.0.1", srv.server_address[1], timeout=3)
            h.request("GET", "/v1/ping", headers={"X-Token": self.token})
            self.assertEqual(h.getresponse().status, 200, "the slots are free again")
            h.close()
        finally:
            srv.shutdown()
            srv.server_close()
            L.Handler.timeout = old

    def test_setup_file(self):
        self.assertEqual(stat.S_IMODE(os.stat(L.PERSONAL).st_mode), 0o600)
        cfg = json.loads(L.PERSONAL.read_text())
        self.assertGreaterEqual(len(cfg["token"]), 24)
        self.assertRegex(cfg["parent_pin"], r"^\d{6}$")
        self.assertEqual(cfg["name"], "Test")

    def test_ping_and_token(self):
        self.assertEqual(self.call("/v1/ping"), (200, {"ok": True, "server": "unicorn-listener", "version": 1}))
        self.assertEqual(self.call("/v1/ping", token=False)[0], 401)
        self.assertEqual(self.call("/v1/ping", X_Token="wrong", token=False)[0], 401)

    def test_answer(self):
        speech = wav_bytes(tone(0.8, -25, 1, 4))
        self.assertEqual(self.call("/v1/answer", speech, X_Question="TEETH_AM", X_Kind="yesno"),
                         (200, {"answer": "yes", "text": "Evet fırçaladım"}))
        self.assertEqual(self.call("/v1/answer", wav_bytes([0] * 64000), X_Question="TEETH_AM", X_Kind="yesno"),
                         (200, {"answer": "silent", "text": ""}))
        self.assertEqual(self.call("/v1/answer", speech, X_Question="NOPE", X_Kind="yesno")[0], 400)
        self.assertEqual(self.call("/v1/answer", wav_bytes([0] * 100, rate=8000), X_Question="TEETH_AM",
                                   X_Kind="yesno")[0], 400)
        # over 1 MB is refused from the header alone, before the body is read
        with socket.create_connection(self.srv.server_address) as c:
            c.sendall(f"POST /v1/answer HTTP/1.1\r\nX-Token: {self.token}\r\nContent-Length: 2000000\r\n\r\n".encode())
            self.assertIn(b" 413 ", c.recv(200))
        self.assertEqual(self.call("/v1/answer", speech, token=False, X_Question="TEETH_AM", X_Kind="yesno")[0], 401)

    def test_journal(self):
        speech = wav_bytes(tone(0.8, -25, 1, 4))
        st, res = self.call("/v1/answer", speech, X_Question="HAPPY", X_Kind="open",
                            X_Recorded_At="2026-11-13T19:05:07+01:00")
        self.assertEqual((st, res["answer"]), (200, "said"))
        st, res = self.call("/v1/journal", speech, X_Question="HAPPY", X_Recorded_At="2026-11-13T19:05:07+01:00")
        self.assertEqual((st, res), (200, {"ok": True, "text": "Evet fırçaladım"}))
        j = L.Handler.cfg["journal"]
        md = (j / "2026-11-13.md").read_text()
        self.assertEqual(md.count("## 19:05 — Test, bugün seni en çok ne mutlu etti?"), 2, md)
        self.assertTrue((j / "2026-11-13_190507.wav").exists() and (j / "2026-11-13_190507-1.wav").exists())
        self.assertIn("[ses](2026-11-13_190507-1.wav)", md)
        self.assertEqual([len(items) for _, items in P.journal_entries(j)], [2])
        self.assertFalse(P.journal_delete(j, "../personal.json"))
        self.assertTrue(P.journal_delete(j, "2026-11-13_190507.wav"))
        self.assertFalse((j / "2026-11-13_190507.wav").exists())
        self.assertEqual(P.journal_entries(j)[0][1][0][3], "2026-11-13_190507-1.wav")
        self.assertTrue(P.journal_delete(j, "2026-11-13_190507-1.wav"))
        self.assertEqual(P.journal_entries(j), [], "an empty day's file goes too")

    def test_tv(self):
        st, res = self.call("/v1/tv-sample", wav_bytes(tone(5, -25, total=5)), X_Recorded_At="2026-11-13T20:00:00")
        self.assertEqual(st, 200)
        self.assertIn(res["guess"], ("tv", "not_tv"))
        meta = json.loads((L.Handler.cfg["tv"] / "2026-11-13_200000.json").read_text())
        self.assertIsNone(meta["label"])
        st, _, page, _ = self.raw("GET", "/tv", host="localhost")  # on the Mac itself: no login
        self.assertEqual(st, 200)
        self.assertIn("2026-11-13_200000", page.decode())
        # from the LAN, or a DNS-rebinding page under another name: login first
        self.assertEqual(self.raw("GET", "/tv", host="evil.example")[:2], (303, "/login"))
        self.assertEqual(self.raw("GET", "/tv/audio/2026-11-13_200000.wav", host="evil.example")[0], 401)
        lab = json.dumps({"name": "2026-11-13_200000", "label": "tv"}).encode()
        self.assertEqual(self.raw("POST", "/tv/label", lab, host="localhost")[0], 200)
        self.assertEqual(self.raw("POST", "/tv/label", lab, host="evil.example")[0], 401)
        bad = json.dumps({"name": "../personal", "label": "tv"}).encode()
        self.assertEqual(self.raw("POST", "/tv/label", bad, host="localhost")[0], 400)
        n, acc, c = P.tv_stats(P.tv_samples(L.Handler.cfg["tv"]))
        self.assertEqual(n, 1)
        self.assertEqual(c[meta["guess"], "tv"], 1)


if __name__ == "__main__":
    unittest.main()
