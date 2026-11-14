// Game rules: needs decay over powered time, care actions, a star-popping mini-game, petting,
// an egg that hatches, and the cosmetic life around it (idle wandering, particles).
// The unicorn never dies: low needs only change her face.
#include "unicorn.h"

namespace up {

static int clamp(int v) { return v < 0 ? 0 : v > MAX_STAT ? MAX_STAT : v; }

static u32 fnv(const u8* p, u32 n) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void copy(u8* dst, const u8* src, u32 n) {
    for (u32 i = 0; i < n; i++) dst[i] = src[i];
}

static u32 xorshift(u32& x) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

u32 Game::random() { return xorshift(rng); }
u32 Game::crandom() { return xorshift(crng); }      // cosmetic only: never changes stats

void Game::init(u32 seed) {
    u8* raw = (u8*)this;
    for (u32 i = 0; i < sizeof(Game); i++) raw[i] = 0;
    rng = seed ? seed : 0x9E3779B9u;
    crng = (rng ^ 0xA5A5A5A5u) | 1;
    s.version = SAVE_VERSION;
    s.character = (u8)(random() % 4);
    for (int i = 0; i < NSTATS; i++) s.stat[i] = 8000;
    s.bed10 = BED_DEFAULT10;
    s.wake10 = WAKE_DEFAULT10;
    s.wake_we10 = WAKE_WE_DEFAULT10;
    scene = SC_EGG_PICK;                   // first: choose one of four eggs
    screen_on = 1;
    dirty = 1;
    facing = 1;
    idle_next = 3000;
}

bool Game::load(const u8* data, u32 len) {
    // Older saves are prefixes of the current layout: copy what they have, zero the rest.
    if (!data || len < 1) return false;
    u32 size = data[0] == 1 ? SAVE_V1_SIZE : data[0] == 2 ? SAVE_V2_SIZE : data[0] == 3 ? SAVE_V3_SIZE
             : data[0] == 4 ? SAVE_V4_SIZE : data[0] == 5 ? SAVE_V5_SIZE : data[0] == 6 ? SAVE_V6_SIZE
             : data[0] == 7 ? SAVE_V7_SIZE : data[0] == 8 ? SAVE_V8_SIZE : data[0] == 9 ? SAVE_V9_SIZE
             : data[0] == SAVE_VERSION ? SAVE_SIZE : 0;
    if (!size || len < size) return false;
    u32 check;
    copy((u8*)&check, data + size - 4, 4);
    if (fnv(data, size - 4) != check) return false;
    Save tmp;
    for (u32 i = 0; i < SAVE_SIZE; i++) ((u8*)&tmp)[i] = 0;
    copy((u8*)&tmp, data, size - 4);
    if (data[0] < 5) { tmp.birth_flags = BIRTH_CHOSEN; tmp.accessory = ACC_NONE; tmp.last_treat = 0; }
    if (data[0] < 7) { tmp.bed10 = BED_DEFAULT10; tmp.wake10 = WAKE_DEFAULT10; tmp.tricks_seen = 0; }
    if (data[0] < 8) tmp.wake_we10 = WAKE_WE_DEFAULT10;   // v7's spare byte was 0; the routine starts afresh
    if (data[0] < 9) {                     // Q_BATH went in at bit 11: Q_HAPPY (and r_done's morning hello) move up
        auto up = [](u16 v) { return (u16)((v & 0x7FF) | (v >> 11) << 12); };
        tmp.old_r_asked = up(tmp.old_r_asked); tmp.old_r_done = up(tmp.old_r_done); tmp.old_q_off = up(tmp.old_q_off);
    }
    if (data[0] < 10) {                    // v10: 32-bit masks (the questions after Q_HAPPY); the hello bit moves to Q_COUNT
        tmp.r_asked = tmp.old_r_asked & 0x1FFFu;
        tmp.q_off = tmp.old_q_off & 0x1FFFu;
        tmp.r_done = (tmp.old_r_done & 0x1FFFu) | (u32)(tmp.old_r_done >> 13 & 1) << Q_COUNT;
    }
    tmp.old_r_asked = tmp.old_r_done = tmp.old_q_off = 0;
    if (tmp.away_act >= ACT_COUNT) tmp.away_act = ACT_NONE;   // (a cosmetic: never a reason to lose the save)
    if (!(tmp.life_flags & LIFE_UTC_AWAY)) tmp.away_until = 0;   // a local-time deadline: count powered time
    tmp.version = SAVE_VERSION;
    if (tmp.accessory >= ACC_COUNT || (tmp.birth_flags & ~BIRTH_CHOSEN) || tmp.last_treat > TREAT_COUNT)
        return false;
    if (tmp.character > 3 || tmp.care_days > CARE_DAYS_MAX || tmp.milestones >> MILESTONES) return false;
    if (tmp.volume > VOL_OFF) return false;   // indexes the settings' volume names
    if (tmp.muted) { tmp.volume = VOL_OFF; tmp.muted = 0; }   // the old child mute is now the parent's "off"
    if (tmp.bed10 >= 144 || tmp.wake10 >= 144 || tmp.tricks_seen > TRICKS || tmp.lang >= LANG_COUNT) return false;
    for (int t = 0; t < 3; t++) if ((tmp.min_ones >> 4 * t & 15) > 9) return false;
    if (tmp.min_ones >> 12 || tmp.wake_we10 >= 144 || tmp.r_asked >> Q_COUNT || tmp.r_done >> (Q_COUNT + 1) || tmp.q_off >> Q_COUNT ||
        tmp.cfg_dirty >> K_COUNT) return false;
    for (int i = 0; i < 13 + (int)(sizeof tmp.r_ans_x / 2); i++) {   // the 12 slots, Q_BATH's, the newer ones'
        u16 a = i < 12 ? tmp.r_ans[i] : i == 12 ? tmp.r_ans_bath : tmp.r_ans_x[i - 13];
        if (a >> 11 > ANS_TAP_NO || (a & 2047) >= 1440) return false;
    }
    for (int i = 0; i < HOL_MAX; i++) if (tmp.hol[i][0] > tmp.hol[i][1]) return false;
    for (int i = 0; i < NSTATS; i++) if (tmp.stat[i] > MAX_STAT) return false;
    s = tmp;
    play_ms = (u32)s.play_s * 1000;
    away_ms = (u32)s.away_s * 1000;
    go(!s.hatched ? ((s.birth_flags & BIRTH_CHOSEN) ? SC_EGG : SC_EGG_PICK)
                  : (s.away_s || s.away_until) ? SC_AWAY : SC_HOME);           // no power-cut shortcut
    screen_on = 1;
    treat = (u8)(s.last_treat ? s.last_treat - 1 : 0);   // the snack picker starts on her last one
    sync_away();                           // with an RTC, the time she was off counts too
    if (scene == SC_HOME) greet();         // switched on: hello again
    Cal c = today();                       // a question asked just before a power cut: the gap still holds
    if (c.known) r_last = c.day * 1440u + c.minute;
    return true;
}

// civil date from days since 2000-01-01 (H. Hinnant's civil_from_days, shifted to 2000-03-01 eras)
Cal calendar(u32 t) {
    Cal c = {0, 0, 0, 0, 0, 0, 0};
    if (!t) return c;
    u32 d = t / 86400;
    c.day = (u16)(d > 65534 ? 65534 : d);
    c.minute = (u16)(t % 86400 / 60);
    c.wday = (u8)((d + 5) % 7);            // 2000-01-01 was a Saturday
    u32 e = d + 10957 + 719468;            // days since 0000-03-01 (Unix epoch is 719468, 2000 is +10957)
    u32 era = e / 146097, doe = e - era * 146097;
    u32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    u32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    u32 mp = (5 * doy + 2) / 153;
    c.mday = (u8)(doy - (153 * mp + 2) / 5 + 1);
    c.month = (u8)(mp < 10 ? mp + 3 : mp - 9);
    c.year = (u16)(yoe + era * 400 + (c.month <= 2));
    c.known = 1;
    return c;
}

void Game::set_wall(u32 local_s, bool calendar, u32 utc) {
    if (want_clock) return;                // a parent's new time is waiting for the RTC write (F_CLOCK)
    wall_s = local_s;
    u32 now = utc ? utc : local_s;
    if (now && utc_s && now < utc_s) s.away_until = 0;   // the clock went back: count the break's own time
    utc_s = now;
    cal_ok = calendar;
    if (!calendar) routine_check();        // her questions need a trusted clock
    sync_away();
}

// While she is out and both a return time and a wall clock are known, the clock decides.
// A clock that went backwards (RTC lost power and restarted) is not trusted: fall back to
// counting powered time from what was left.
void Game::sync_away() {
    if ((scene != SC_AWAY && !(scene == SC_ASK && r_back == SC_AWAY)) || !s.away_until || !utc_s) return;   // (or asking from it)
    if (utc_s + break_ms() / 1000 < s.away_until) { s.away_until = 0; return; }
    u32 left = utc_s >= s.away_until ? 0 : (s.away_until - utc_s) * 1000;
    if (left != away_ms) { away_ms = left; dirty = 1; }
}

u32 Game::save(u8* out) const {
    Save tmp = s;
    bool out_now = scene == SC_LEAVE || scene == SC_AWAY || (scene == SC_ASK && r_back == SC_AWAY);   // (asking from her break)
    tmp.away_s = (u16)(scene == SC_LEAVE ? break_ms() / 1000 : out_now ? (away_ms + 999) / 1000 : 0);
    tmp.play_s = (u16)(play_ms / 1000 > 65535 ? 65535 : play_ms / 1000);
    tmp.away_until = !out_now ? 0 : scene == SC_LEAVE ? (utc_s ? utc_s + break_ms() / 1000 : 0) : s.away_until;
    tmp.life_flags |= LIFE_UTC_AWAY;
    tmp.check = fnv((const u8*)&tmp, SAVE_SIZE - 4);
    copy(out, (const u8*)&tmp, SAVE_SIZE);
    return SAVE_SIZE;
}

void Game::set_stat(int i, int v) {
    if (i >= 0 && i < NSTATS) { s.stat[i] = (u16)clamp(v); dirty = 1; }
}

// What she says with each sound (V_NONE: just the tune). Some calls pass their own line.
static const u8 VOICE_OF[SFX_COUNT] = {
    V_NONE, V_NONE, V_HELLO, V_YUM, V_PLAY, V_BATH, V_SLEEPY, V_AWAKE, V_TIRED,   // NONE TAP HATCH .. REFUSE
    V_NONE, V_GIGGLE, V_NONE, V_YAY, V_BYE, V_NONE, V_HELLO,                      // INFO PET POP CHEER BYE KNOCK HELLO
    V_NONE, V_NONE, V_NONE, V_NONE, V_NONE, V_PRETTY, V_NONE, V_REST};             // NOTE1-3 CHOOSE LAND DRESS ECHO CHARGE
enum { VOICE_GAP_MS = 1500, VOICE_REPEAT_MS = 8000 };   // never over each other; the same line not again soon

void Game::cue(int id, int line) {
    sfx = (u8)id;
    int v = line >= 0 ? line : VOICE_OF[id];
    if (v == V_NONE) return;
    u32 ago = voice_at ? since(voice_at) : 0xFFFFFFFFu, same = voice_line_at[v] ? since(voice_line_at[v]) : 0xFFFFFFFFu;
    if (ago < VOICE_GAP_MS || same < VOICE_REPEAT_MS) return;   // a tickle is not a speech
    voice = (u8)v;
    voice_at = voice_line_at[v] = stamp();
}

int Game::take_sfx(int* line, int* clip) {
    bool q = quiet();
    int id = q ? (int)SFX_NONE : (int)sfx;
    if (line) *line = q ? (int)V_NONE : (int)voice;
    if (clip) *clip = q || !rclip ? -1 : rclip - 1;
    sfx = voice = 0;
    rclip = 0;
    return id;
}

void Game::go(u8 sc) {
    scene = sc;
    scene_ms = anim_ms = 0;
    frame = 0;
    dirty = 1;
    if (sc == SC_AWAY) away_next();
    idle_act = IDLE_NONE;
    visitor = 0;                           // the butterfly flies off; its schedule restarts at home
    visit_due = 0;
    greeting = 0;
    party = 0;
    standing = 0;
    memory_at = 0;                         // a picture that was floating up starts again at home
    static const u8 DID_BY_SCENE[] = {0, 0, 0, DID_FEED, 0, DID_WASH, 0, 0, 0, DID_STARS, 0, 0, 0, 0, 0, 0, 0,
                                      DID_MUSIC, DID_CLOUD, DID_DRESS, 0, 0};   // indexed by Scene
    if (sc < sizeof DID_BY_SCENE) did |= DID_BY_SCENE[sc];
}

void Game::hatch() {
    screen_on = 1;                         // never hatch in the dark: this is the big moment
    idle_ms = 0;
    s.hatched = 1;
    want_save = 1;
    Cal c = today();                       // her birthday, if the clock knows the date
    if (c.known) { s.hatch_day = c.day; s.party_year = (u8)(c.year - 2000); }
    go(SC_HATCH);
    cue(SFX_HATCH);
    burst(P_CONFETTI, SCREEN_W / 2, 230, 10);
    burst(P_STAR, SCREEN_W / 2, 230, 4);
    hint_pet = 1;                          // a hand shows how to pet her until she is petted
}

void Game::start_leave() {                 // a friend comes by; she says goodbye and goes out
    go(SC_LEAVE);
    want_save = 1;                         // saved now: a power cut during the goodbye skips no break
    cue(SFX_BYE);
    facing = 1;
}

void Game::press(int item) {               // a menu button dips and pops back (render)
    press_item = (u8)item;
    press_end = clock_ms + PRESS_MS;
}

int Game::recommended() const {
    const u16* v = s.stat;
    if (bedtime() == BED_DROWSY) return MENU_SLEEP;   // bedtime: the moon button bounces
    if (v[HUNGER] < NEED_LOW) return MENU_FEED;
    if (v[ENERGY] < NEED_LOW) return MENU_SLEEP;
    if (v[CLEAN] < NEED_LOW) return MENU_WASH;
    if (v[HAPPY] < NEED_LOW) return MENU_PLAY;
    return -1;
}

int Game::phase() const {
    Cal c = today();
    if (!c.known) return PH_DAY;
    if (c.minute >= NIGHT_MIN || c.minute < MORNING_MIN) return PH_NIGHT;
    return c.minute >= EVENING_MIN ? PH_EVENING : PH_DAY;
}

// After a real break (or on power-up) she says hello: a stretch by day, a yawn at night, and a
// picture of the snack you last shared. Purely cosmetic: no needs, no screen time, no sound.
void Game::greet() {
    greeting = 1;
    greet_end = clock_ms + GREET_MS;
    idle_act = IDLE_NONE;
    dirty = 1;
}

// The first care of a new day (feed, wash, pet, tuck-in) puts a picture in her album. No streaks:
// skipped days cost nothing. A clock that went backwards just restarts the count from today.
void Game::remember() {
    Cal c = today();
    bool welcome_due = s.seen_day && c.day >= s.seen_day + REUNION_DAYS;   // check_life() greets first
    if (c.known && c.day > s.seen_day && !welcome_due) s.seen_day = c.day;
    if (!c.known || c.day == s.memory_day) return;
    bool first = c.day > s.memory_day || !s.memory_day;
    s.memory_day = c.day;
    if (!first) return;
    if (s.care_days < CARE_DAYS_MAX) s.care_days++;
    memory_new = 1;
    memory_at = 0;
    album_unseen = 1;
    want_save = 1;
}

int Game::stage() const {
    int d = s.care_days;
    return d >= STORYKEEPER_DAYS ? STAGE_STORYKEEPER : d >= GROWN_DAYS ? STAGE_GROWN
         : d >= YOUNG_DAYS ? STAGE_YOUNG : STAGE_FOAL;
}

//                                     hum bow twirl dance rainbow bubbles juggle balance magic show
const u16 TRICK_DAYS[TRICKS] =        {  3,  5,    7,   14,     21,     30,    45,     60,  100,  200};
const u16 TRICK_MS[TRICKS] =          {1200, 1000, 1200, 1600, 1600,  1400,  1600,   1800, 1400, 2400};
const u8 TRICK_CALM[TRICKS] =         {  1,  1,    0,    0,      1,      1,     1,      0,    1,    0};

int Game::tricks() const {
    int n = 0;
    while (n < TRICKS && s.care_days >= TRICK_DAYS[n]) n++;
    return n;
}

int Game::years() const {
    Cal h = calendar(s.hatch_day * 86400u);
    return h.known && s.party_year + 2000 > h.year ? s.party_year + 2000 - h.year : 0;
}

// Life events, checked at home when nothing else is showing. One celebration at a time; the
// rest wait for the next quiet moment. Each is marked done as it starts, so a reboot never
// repeats it, and a long absence never piles them up: one welcome, one birthday, one milestone.
void Game::check_life() {
    if (party || greeting || memory_new || petting) return;
    Cal c = today();
    if (c.known && s.hatched && !s.hatch_day) {   // an older unicorn: today becomes her birthday
        s.hatch_day = c.day;
        s.life_flags |= LIFE_ADOPTED;
        s.party_year = (u8)(c.year - 2000);
        want_save = 1;
    }
    u8 kind = PARTY_NONE, arg = 0;
    if (c.known && s.seen_day && c.day >= s.seen_day + REUNION_DAYS) {
        kind = PARTY_REUNION;
        for (int i = 0; i < NSTATS; i++) if (s.stat[i] < REUNION_NEED) s.stat[i] = REUNION_NEED;   // a gift
        s.seen_day = c.day;
    } else if (c.known && s.hatch_day && c.year - 2000 > s.party_year) {
        Cal h = calendar(s.hatch_day * 86400u);
        int hm = h.month, hd = h.mday;
        bool leap = c.year % 4 == 0 && (c.year % 100 || c.year % 400 == 0);
        if (hm == 2 && hd == 29 && !leap) hd = 28;
        if (c.month > hm || (c.month == hm && c.mday >= hd)) {   // on or after the day: one party
            kind = PARTY_BIRTHDAY;
            s.party_year = (u8)(c.year - 2000);
        }
    }
    if (!kind) {
        static const u16 DAYS[MILESTONES] = {YOUNG_DAYS, GROWN_DAYS, STORYKEEPER_DAYS, 365};
        for (int k = MILESTONES - 1; k >= 0; k--)   // only the biggest one reached, the rest are marked
            if (s.care_days >= DAYS[k] && !(s.milestones & (1 << k))) {
                kind = PARTY_MILESTONE;
                arg = (u8)k;
                s.milestones |= (u8)((2 << k) - 1);
                break;
            }
    }
    if (!kind) return;
    party = kind;
    party_arg = arg;
    party_end = clock_ms + (kind == PARTY_MILESTONE ? MILESTONE_MS : PARTY_MS);
    idle_act = IDLE_NONE;
    want_save = 1;
    cue(kind == PARTY_REUNION ? SFX_HELLO : SFX_CHEER,
        kind == PARTY_REUNION ? V_MISSED : kind == PARTY_BIRTHDAY ? V_BIRTHDAY : V_GREW);
    burst(kind == PARTY_BIRTHDAY ? P_CONFETTI : kind == PARTY_REUNION ? P_HEART : P_STAR,
          SCREEN_W / 2 + pet_x, 150, 6);
}

int Game::album() const {                  // every book holds 12 days; old books stay as they were
    int n = (int)s.care_days - (int)book * ALBUM_PICS;
    return n < 0 ? 0 : n > ALBUM_PICS ? ALBUM_PICS : n;
}

int Game::garden(int pot) const {          // pot 1 grows on the book's days 1-4, pot 2 on 5-8, pot 3 on 9-12
    int st = album() - 4 * pot;
    return st < 0 ? 0 : st > 4 ? 4 : st;
}

// Tonight's dreams: the first is something she did today (if anything), the others anything at
// all, never the same twice in a night. Chosen once, at bedtime, with the cosmetic RNG.
void Game::plan_dreams() {
    u8 pool[D_COUNT];
    int n = 0;
    for (int k = 0; k < DREAM_FROM_DAY; k++) if (did & (1 << k)) pool[n++] = (u8)k;
    for (int i = 0; i < DREAMS; i++) {
        if (i > 0 || !n) {                 // anything she has dreamt of before, or might
            n = 0;
            for (int k = 0; k < D_COUNT; k++) {
                bool used = false;
                for (int j = 0; j < i; j++) used |= dreams[j] == k;
                if (!used && (k != D_FLOWERS || s.care_days >= 2)) pool[n++] = (u8)k;
            }
        }
        dreams[i] = pool[crandom() % n];
    }
    did = 0;                               // a new day starts after this sleep
}

int Game::dream_ms() const {
    if (scene != SC_SLEEP || !tucked_in() || standing || scene_ms < DREAM_FIRST_MS) return -1;
    u32 t = (scene_ms - DREAM_FIRST_MS) % DREAM_EVERY_MS;
    return t < DREAM_MS ? (int)t : -1;
}

int Game::dream() const {                  // a dream every 8 s while someone is looking
    if (dream_ms() < 0) return 0;
    u32 k = (scene_ms - DREAM_FIRST_MS) / DREAM_EVERY_MS;
    return k < DREAMS ? 1 + dreams[k] : 0;   // three dreams a night, then just sleep
}

void Game::come_back() {                   // she walks in from the left
    s.away_until = 0;
    s.away_act = ACT_NONE;
    go(SC_RETURN);
    pet_x = -300;
    facing = 1;
    cue(SFX_HELLO);
}

// Every look at the away screen shows another game with her friends: never the same one twice running.
void Game::away_next() { away_pick = (u8)((away_pick + 1 + crandom() % (AWAY_PICKS - 1)) % AWAY_PICKS); }

void Game::wake() {
    if (scene == SC_SLEEP && bed_locked()) return;   // the night belongs to sleep: only morning wakes her
    go(plugged && !screen_on ? SC_LOUNGE : SC_HOME);   // dark, on the charger: straight back to the lounger
    if (screen_on) cue(SFX_WAKE);          // waking by herself in the dark wakes nobody else
}

// Rested enough to get up by herself: never during the night window.
bool Game::rested() const {
    return scene == SC_SLEEP && scene_ms >= TUCK_MS && s.stat[ENERGY] >= MAX_STAT && !bed_locked();
}

int Game::bedtime() const {
    Cal c = today();
    int bed = clock_min(T_BED), up = wake_min(), m = c.minute;   // later wake-up at weekends and on holidays
    if (!c.known || (s.opts & OPT_NO_BEDTIME) || bed == up) return BED_NONE;
    bool night = bed < up ? m >= bed && m < up : m >= bed || m < up;
    if (!night) return BED_NONE;
    return (m - bed + 1440) % 1440 < DROWSY_MIN ? BED_DROWSY : BED_SLEEP;
}

// Falls asleep by herself: on her feet if she was up and about, else lying down (her quilt over her).
void Game::doze(bool on_her_feet) {
    sleep_ms = 0;
    napped = 0;
    pet_x = 0;
    facing = 1;
    go(SC_SLEEP);
    standing = on_her_feet;
    tucked = 1;
    tuck_ms = 0;
    plan_dreams();
    if (screen_on) cue(SFX_SLEEP);
}

// Every simulation step, from the wall clock (the platform's input), dark fast-forward included.
void Game::check_bedtime() {
    int b = bedtime();
    bool first = !bed_seen && today().known;
    if (today().known) bed_seen = 1;
    if (scene == SC_SLEEP && night_sleep && b == BED_NONE && today().known) {   // good morning
        night_sleep = 0;
        go(plugged && !screen_on ? SC_LOUNGE : SC_HOME);   // dark, on the charger: onto the lounger
        if (screen_on) { if (!routines_on()) cue(SFX_WAKE, V_MORNING); greet(); }   // (the routine says hello)
        return;
    }
    if (scene == SC_SETTINGS && back_scene == SC_SLEEP && b != BED_NONE) back_night = 1;   // night fell meanwhile
    night_sleep = scene == SC_SLEEP && b != BED_NONE;
    if (b != BED_SLEEP) return;
    if (scene == SC_HOME && !party) { greeting = 0; doze(!first); }   // up past bedtime: asleep on her feet
    else if (scene == SC_LOUNGE) doze(false);                          // on the lounger: just lies back
}

Mood Game::mood() const {
    const u16* v = s.stat;
    if (v[HUNGER] < NEED_LOW) return M_HUNGRY;
    if (v[ENERGY] < NEED_LOW) return M_SLEEPY;
    if (v[CLEAN] < NEED_LOW || v[HAPPY] < NEED_LOW) return M_SAD;
    if (v[HUNGER] >= 7000 && v[HAPPY] >= 7000 && v[ENERGY] >= 7000 && v[CLEAN] >= 7000) return M_HAPPY;
    return M_IDLE;
}

int Game::messes() const {
    if (!s.hatched) return 0;
    int c = s.stat[CLEAN];
    return c < 1000 ? 3 : c < 1800 ? 2 : c < NEED_LOW ? 1 : 0;
}

int Game::particles() const {
    int n = 0;
    for (int i = 0; i < NPART; i++) n += part[i].kind != P_NONE;
    return n;
}

// ---- particles -----------------------------------------------------------------------------

void Game::burst(int kind, int x, int y, int n) {
    static const u8 LIFE[] = {0, 14, 6, 20, 8, 10, 16, 6, 14};
    if (calm()) n = (n + 1) / 2;
    for (int k = 0; k < n; k++) {
        int slot = 0;                      // free slot, else the one closest to expiring
        for (int i = 0; i < NPART; i++) {
            if (part[i].kind == P_NONE) { slot = i; break; }
            if (part[i].life < part[slot].life) slot = i;
        }
        Part& p = part[slot];
        p.kind = (u8)kind;
        int spread = kind == P_HEART ? 28 : 10;           // hearts fan out so each one reads
        p.x = (i16)(x + (int)(crandom() % (2 * spread + 1)) - spread);
        p.y = (i16)(y + (int)(crandom() % 11) - 5);
        p.vx = (i8)((int)(crandom() % 7) - 3);
        switch (kind) {
        case P_HEART:    p.vy = (i8)-(2 + (int)(crandom() % 2)); p.vx = (i8)(p.vx / 2); break;
        case P_BUBBLE:   p.vy = (i8)-(1 + (int)(crandom() % 2)); break;
        case P_CRUMB:    p.vy = (i8)-(1 + (int)(crandom() % 3)); break;
        case P_CONFETTI: p.vy = (i8)-(3 + (int)(crandom() % 4)); break;
        case P_RING:     p.vx = 0; p.vy = 0; p.x = (i16)x; p.y = (i16)y; break;
        case P_NOTE:     p.vy = -2; p.vx = (i8)(p.vx / 2); break;   // a sung note floats up
        default:         p.vy = (i8)((int)(crandom() % 7) - 4); break;
        }
        p.life = LIFE[kind];
        p.col = (u8)(crandom() % 7);
    }
    dirty = 1;
}

// ---- mini-game -----------------------------------------------------------------------------

void Game::spawn_target(int i, int y) {
    int x = 62 + i * 122 + (int)(crandom() % 41) - 20;
    tgt[i].x = (i16)(x < 44 ? 44 : x > SCREEN_W - 44 ? SCREEN_W - 44 : x);
    tgt[i].y = (i16)y;
    tgt[i].vy = (i8)-(4 + (int)(crandom() % 3));
    tgt[i].alive = 1;
}

bool Game::visible(int i) const { return tgt[i].alive && tgt[i].y < SCREEN_H - 20; }

int Game::highest_visible() const {
    int best = -1;
    for (int i = 0; i < GAME_TARGETS; i++)
        if (visible(i) && (best < 0 || tgt[i].y < tgt[best].y)) best = i;
    return best;
}

void Game::pop(int i) {
    if (i < 0 || i >= GAME_TARGETS || !tgt[i].alive) return;
    if (score < 99) score++;
    burst(P_RING, tgt[i].x, tgt[i].y, 1);
    burst(P_SPARKLE, tgt[i].x, tgt[i].y, 2);
    burst(P_CONFETTI, tgt[i].x, tgt[i].y, 3);
    cue(SFX_POP);
    spawn_target(i, SCREEN_H - 10 + (int)(crandom() % 30));   // the next one peeks in at the bottom
}

// ---- care ----------------------------------------------------------------------------------

void Game::pet() {
    pet_react_end = clock_ms + PET_REACT_MS;
    petting = 1;
    hint_pet = 0;
    idle_act = IDLE_NONE;
    bool foal = stage() == STAGE_FOAL;     // just above her head, a foal's is lower
    burst(P_HEART, SCREEN_W / 2 + pet_x + facing * (foal ? 28 : 42), foal ? 176 : 112, 3);   // just above her head
    cue(SFX_PET);
    remember();
    did |= DID_PET;
    pet_gain();
    dirty = 1;
}

// Hearts every time, happiness rationed. Saved too, but at most once a minute (flash wear).
void Game::pet_gain() {
    if (pet_cooling && !due(pet_gain_at)) return;
    s.stat[HAPPY] = (u16)clamp(s.stat[HAPPY] + PET_HAPPY);
    pet_gain_at = clock_ms + PET_COOLDOWN_MS;
    pet_cooling = 1;
    if (!pet_saved_at || since(pet_saved_at) >= 60000) { want_save = 1; pet_saved_at = stamp(); }
}

void Game::minute() {
    u16* v = s.stat;
    u16 old[NSTATS];
    for (int i = 0; i < NSTATS; i++) old[i] = v[i];
    if (scene == SC_LEAVE || scene == SC_AWAY || scene == SC_RETURN) return;   // out with friends
    if (scene == SC_SLEEP || scene == SC_LOUNGE) {   // resting: energy refills, other needs slow down
        v[ENERGY] = (u16)clamp(v[ENERGY] + SLEEP_ENERGY_GAIN);
        v[HUNGER] = (u16)clamp(v[HUNGER] - DECAY_HUNGER / 2);
        v[CLEAN] = (u16)clamp(v[CLEAN] - DECAY_CLEAN / 2);
    } else {
        v[HUNGER] = (u16)clamp(v[HUNGER] - DECAY_HUNGER);
        v[HAPPY] = (u16)clamp(v[HAPPY] - DECAY_HAPPY);
        v[ENERGY] = (u16)clamp(v[ENERGY] - DECAY_ENERGY);
        v[CLEAN] = (u16)clamp(v[CLEAN] - DECAY_CLEAN);
    }
    if (!screen_on)                        // left alone she gets peckish, never miserable
        for (int i = 0; i < NSTATS; i++) {
            if (v[i] < DARK_FLOOR && old[i] >= DARK_FLOOR) v[i] = DARK_FLOOR;
            else if (v[i] < old[i] && old[i] < DARK_FLOOR) v[i] = old[i];
        }
    dirty = 1;
}

void Game::eat(int t) {                    // the chosen snack: all three nourish the same
    u16* v = s.stat;
    treat = (u8)t;
    s.last_treat = (u8)(t + 1);            // she remembers it next time you meet
    remember();
    v[HUNGER] = (u16)clamp(v[HUNGER] + FEED_GAIN);
    v[HAPPY] = (u16)clamp(v[HAPPY] + TREAT_HAPPY);
    go(SC_EAT);
    cue(SFX_EAT);
    want_save = 1;
}

// Parent settings: BOOT (or tap) picks a row, PWR (or tap) changes it.
void Game::settings_input(int type, int x, int y) {
    if (set_page == PG_CLOCK) { clock_input(type, x, y); return; }
    if (set_page == PG_BED) { bed_input(type, x, y); return; }
    if (set_page == PG_UPDATE) { update_input(type, x, y); return; }
    if (set_page == PG_ROUTINE || set_page == PG_HABITS) { routine_page_input(type, x, y); return; }
    if (set_page >= PG_HUB) { wifi_input(type, x, y); return; }
    int row = set_row;
    if (type == IN_TOUCH) {
        if ((row = set_touch(y)) < 0) return;
        set_row = (u8)row;
    } else if (type == IN_NEXT) {
        set_row = (u8)((set_row + 1) % SET_ROWS);
        dirty = 1;
        return;
    } else if (type != IN_SELECT) {
        return;
    }
    static const u8 LIMITS[] = {5, 10, 15, 20}, BREAKS[] = {15, 30, 45, 60}, BRIGHT[] = {60, 120, 180, 255};
    static const i8 KEY_OF[SET_ROWS] = {K_VOLUME, K_PLAY_MIN, K_BREAK_MIN, K_LANG, -1, -1, K_BRIGHTNESS, K_CALM,
                                        K_SHAKE, -1, -1};   // what the parent page learns at the next sync
    if (row < SET_ROWS && KEY_OF[row] >= 0) touched(KEY_OF[row]);
    switch (row) {
    case SET_VOLUME: s.volume = (u8)((s.volume + 1) % 3); break;
    case SET_LIMIT: {
        int cur = s.limit_min ? s.limit_min : 10, i = 0;
        while (i < 4 && LIMITS[i] != cur) i++;
        s.limit_min = LIMITS[(i + 1) % 4];
        break;
    }
    case SET_BREAK: {
        int cur = s.break_min ? s.break_min : 30, i = 0;
        while (i < 4 && BREAKS[i] != cur) i++;
        s.break_min = BREAKS[(i + 1) % 4];
        break;
    }
    case SET_BRIGHT: {
        int cur = brightness(), i = 0;
        while (i < 4 && BRIGHT[i] != cur) i++;
        s.bright = BRIGHT[(i + 1) % 4];
        break;
    }
    case SET_CALM:  s.opts ^= OPT_CALM; break;
    case SET_SHAKE: s.opts ^= OPT_NO_SHAKE; break;
    case SET_CLOCK: set_page = PG_HUB; set_row = 0; cue(SFX_TAP); dirty = 1; return;   // the clock & WiFi hub
    case SET_SLEEP: set_page = PG_BED; set_row = 0; cue(SFX_TAP); dirty = 1; return;
    case SET_ROUTINE: set_page = PG_ROUTINE; set_row = 0; cue(SFX_TAP); dirty = 1; return;
    case SET_LANG:  s.lang = (u8)((s.lang + 1) % LANG_COUNT); break;   // every tap: the next language
    case SET_EXIT:
        leave_settings();
        break;
    }
    want_save = 1;
    dirty = 1;
    cue(SFX_TAP);
}

int Game::set_rows() const {
    switch (set_page) {
    case PG_MAIN:  return SET_ROWS;
    case PG_CLOCK: return CLK_ROWS;
    case PG_BED:   return BEDR_ROWS;
    case PG_ROUTINE: return RT_ROWS;
    case PG_HUB:   return HUB_ROWS;
    case PG_WIFI:  return WF_ROWS;
    case PG_WLIST: return wifi_count + 1;
    default:       return 0;
    }
}

int Game::set_touch(int y) {
    int n = set_rows(), slot = (y - SET_Y0) / SET_ROW_H;
    if (y < SET_Y0 || slot >= SET_SLOTS) return -1;
    int r = set_slot_row(n, set_half(n, set_row), slot);
    if (r == SLOT_MORE || r == SLOT_UP) {  // the other half of a long list
        set_row = (u8)(r == SLOT_MORE ? SET_SPLIT : 0);
        dirty = 1;
        cue(SFX_TAP);
    }
    return r < 0 ? -1 : r;
}

static bool leap(int y) { return y % 4 == 0 && (y % 100 || y % 400 == 0); }
static int month_days(int y, int m) {
    static const u8 D[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return D[m - 1] + (m == 2 && leap(y));
}
static u32 day_number(int y, int m, int d) {   // local days since 2000-01-01
    static const u16 CUM[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    u32 n = 0;
    for (int k = 2000; k < y; k++) n += leap(k) ? 366 : 365;
    return n + CUM[m - 1] + (m > 2 && leap(y)) + d - 1;
}

void Game::open_clock() {                  // start from what the clock says (or a plain guess)
    Cal c = calendar(wall_s ? wall_s : day_number(2026, 1, 1) * 86400u + 12 * 3600u);
    ed_year = c.year < 2024 ? 2024 : c.year > 2099 ? 2099 : c.year;
    ed_month = c.month; ed_day = c.mday; ed_hour = (u8)(c.minute / 60); ed_min = (u8)(c.minute % 60);
    Cal b = s.hatch_day ? calendar(s.hatch_day * 86400u) : c;
    ed_bmonth = b.month; ed_bday = b.mday;
    set_page = 1;
    set_row = 0;
    dirty = 1;
}

void Game::bed_input(int type, int x, int y) {   // bedtime page: changes apply at once
    int row = set_row, step = 1;
    if (type == IN_TOUCH) {
        if ((row = set_touch(y)) < 0) return;
        set_row = (u8)row;
        step = x < SCREEN_W / 2 ? -1 : 1;
    } else if (type == IN_NEXT) {
        set_row = (u8)((set_row + 1) % BEDR_ROWS);
        dirty = 1;
        return;
    } else if (type != IN_SELECT) {
        return;
    }
    switch (row) {
    case BEDR_ON:   s.opts ^= OPT_NO_BEDTIME; break;
    case BEDR_BED:  set_clock_min(T_BED, (clock_min(T_BED) + 1440 + step * 10) % 1440); touched(K_BEDTIME); break;
    case BEDR_WAKE: set_clock_min(T_WAKE, (clock_min(T_WAKE) + 1440 + step * 10) % 1440); touched(K_WAKE_WEEKDAY); break;
    case BEDR_WAKE_WE: set_clock_min(T_WAKE_WE, (clock_min(T_WAKE_WE) + 1440 + step * 10) % 1440); touched(K_WAKE_WEEKEND); break;
    case BEDR_BACK: set_page = 0; set_row = SET_SLEEP; break;
    }
    want_save = 1;
    dirty = 1;
    cue(SFX_TAP);
}

void Game::clock_input(int type, int x, int y) {
    int row = set_row, step = 1;
    if (type == IN_TOUCH) {
        if ((row = set_touch(y)) < 0) return;
        set_row = (u8)row;
        step = x < SCREEN_W / 2 ? -1 : 1;
    } else if (type == IN_NEXT) {
        set_row = (u8)((set_row + 1) % CLK_ROWS);
        dirty = 1;
        return;
    } else if (type != IN_SELECT) {
        return;
    }
    // each field wraps around, so the far end is never more than half the range away
    auto spin = [&](int v, int lo, int hi) { return v + step > hi ? lo : v + step < lo ? hi : v + step; };
    switch (row) {
    case CLK_YEAR:   ed_year = (u16)spin(ed_year, 2024, 2099); break;
    case CLK_MONTH:  ed_month = (u8)spin(ed_month, 1, 12); break;
    case CLK_DAY:    ed_day = (u8)spin(ed_day, 1, month_days(ed_year, ed_month)); break;
    case CLK_HOUR:   ed_hour = (u8)spin(ed_hour, 0, 23); break;
    case CLK_MIN:    ed_min = (u8)spin(ed_min, 0, 59); break;
    case CLK_BMONTH: if (s.hatched) ed_bmonth = (u8)spin(ed_bmonth, 1, 12); break;
    case CLK_BDAY:   if (s.hatched) ed_bday = (u8)spin(ed_bday, 1, month_days(2000, ed_bmonth)); break;
    case CLK_SAVE:   save_clock(); break;
    case CLK_BACK:   set_page = PG_HUB; set_row = HUB_CLOCK; break;
    }
    if (ed_day > month_days(ed_year, ed_month)) ed_day = (u8)month_days(ed_year, ed_month);
    if (ed_bday > month_days(2000, ed_bmonth)) ed_bday = (u8)month_days(2000, ed_bmonth);
    dirty = 1;
    cue(SFX_TAP);
}

// Set the clock (seconds = 0) and, once she has hatched, her birthday. The platform writes the
// RTC on F_CLOCK. A birthday set to today still gets its party today; one already past this year
// does not throw a late party.
void Game::save_clock() {
    u32 today_n = day_number(ed_year, ed_month, ed_day);
    wall_s = today_n * 86400u + ed_hour * 3600u + ed_min * 60u;
    cal_ok = 1;
    want_clock = 1;
    s.seen_day = (u16)today_n;             // a big jump in the date is not a long absence
    if (s.hatched) {
        Cal h = s.hatch_day ? calendar(s.hatch_day * 86400u) : Cal{0, 0, 0, 0, 0, 0, 0};
        int y = h.known ? h.year : ed_year;
        auto on = [&](int yr) { int d = ed_bday > month_days(yr, ed_bmonth) ? month_days(yr, ed_bmonth) : ed_bday;
                                return day_number(yr, ed_bmonth, d); };
        bool feb29 = ed_bmonth == 2 && ed_bday == 29;
        auto fits = [&](int yr) { return !feb29 || leap(yr); };   // a 29 February birthday needs a leap year
        while (!fits(y) || on(y) > today_n) y--;   // she cannot be born in the future
        s.hatch_day = (u16)on(y);
        s.life_flags &= (u8)~LIFE_ADOPTED;
        int last = on(ed_year) < today_n ? ed_year : ed_year - 1;   // the last birthday before today
        if (y == ed_year) last = ed_year;  // born this year: the first party is next year
        if (last - 2000 > s.party_year) s.party_year = (u8)(last - 2000);
    }
    want_save = 1;
    set_page = PG_HUB;
    set_row = HUB_CLOCK;
}

// ---- WiFi setup screens (the platform does the radio work and reports back) -----------------

static void copy_name(char* dst, const char* src, int n) {
    int i = 0;
    for (; src && src[i] && i < n - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

void Game::wifi_request(int req, int arg) {
    wifi_req = (u8)req;
    wifi_arg = (u8)arg;
    if (req == WREQ_START) { wifi_stage = WST_STARTING; qr_side = 0; wifi_net[0] = 0; }
    if (req == WREQ_STOP) { wifi_stage = WST_OFF; qr_side = 0; }
    dirty = 1;
}

// The toy's own network is up: "WIFI:T:WPA;S:<name>;P:<password>;;" as a QR code a phone camera
// joins in one go. Special characters are escaped (the platform avoids them anyway).
void Game::wifi_session(const char* ap, const char* pass) {
    copy_name(wifi_ap, ap, sizeof wifi_ap);
    copy_name(wifi_pass, pass, sizeof wifi_pass);
    char t[128];
    int k = 0;
    auto put = [&](const char* s, bool esc) {
        for (; *s && k < (int)sizeof t - 3; s++) {
            if (esc && (*s == ';' || *s == ',' || *s == ':' || *s == '\\' || *s == '"')) t[k++] = '\\';
            t[k++] = *s;
        }
    };
    put("WIFI:T:WPA;S:", false); put(wifi_ap, true); put(";P:", false); put(wifi_pass, true); put(";;", false);
    t[k] = 0;
    qr_side = (u8)qr_encode(t, qr, QR_MAX_SIDE_);
    wifi_stage = WST_WAIT;
    dirty = 1;
}

void Game::wifi_status(int stage, const char* network) {
    wifi_stage = (u8)stage;
    if (network) copy_name(wifi_net, network, sizeof wifi_net);
    if (stage == WST_OFF) {                // the radio went off by itself: leave its screens
        qr_side = 0;
        if (scene == SC_SETTINGS && (set_page == PG_WSESSION || set_page == PG_WHELP)) { set_page = PG_WIFI; set_row = WF_ADD; }
    }
    dirty = 1;
}

void Game::wifi_list_add(const char* name, bool built_in) {
    if (wifi_count >= WIFI_LIST_MAX) return;
    if (built_in) wifi_fixed |= (u16)(1 << wifi_count);
    copy_name(wifi_names[wifi_count++], name, WIFI_NAME);
    dirty = 1;
}

// Two big buttons along the bottom (left 0, right 1) on the WiFi pages.
static int wifi_button(int x, int y) {
    if (y < WBTN_Y0 || y > WBTN_Y1) return -1;
    return x < SCREEN_W / 2 ? 0 : 1;
}

void Game::wifi_input(int type, int x, int y) {
    int row = set_row, rows = set_rows();
    int btn = -1;
    if (rows) {                            // list pages: BOOT moves, PWR or a tap picks
        if (type == IN_NEXT) { set_row = (u8)((set_row + 1) % rows); dirty = 1; return; }
        if (type == IN_TOUCH) {
            if ((row = set_touch(y)) < 0) return;
            set_row = (u8)row;
        } else if (type != IN_SELECT) return;
    } else {                               // button pages: BOOT switches, PWR or a tap presses
        if (type == IN_NEXT) { wifi_sel ^= 1; dirty = 1; return; }
        if (type == IN_TOUCH) btn = wifi_button(x, y); else if (type == IN_SELECT) btn = wifi_sel;
        if (btn < 0) return;
    }
    bool result = wifi_stage >= WST_OK, under_way = wifi_stage >= WST_JOIN && !result;
    switch (set_page) {
    case PG_HUB:
        if (row == HUB_CLOCK) { open_clock(); break; }
        if (row == HUB_WIFI) { set_page = PG_WIFI; set_row = 0; break; }
        if (row == HUB_UPDATE) {           // a stale result is not shown again; a known newer version is
            if (!update_busy()) update_stage = UST_IDLE;
            set_page = PG_UPDATE; wifi_sel = 1;
            break;
        }
        set_page = PG_MAIN; set_row = SET_CLOCK;
        break;
    case PG_WIFI:
        if (row == WF_ADD) { set_page = wifi_busy() ? PG_WSESSION : PG_WSTART; wifi_sel = 1; break; }
        if (row == WF_LIST) { set_page = PG_WLIST; set_row = 0; break; }
        set_page = PG_HUB; set_row = HUB_WIFI;
        break;
    case PG_WSTART:                        // GERİ | BAŞLAT
        if (btn == 1) { wifi_request(WREQ_START); set_page = PG_WSESSION; wifi_sel = 1; }
        else { set_page = PG_WIFI; set_row = WF_ADD; }
        break;
    case PG_WSESSION:
        if (result) {                      // TAMAM / BİTİR | TEKRAR
            bool ok = wifi_stage == WST_OK || wifi_stage == WST_OK_NOCLOCK || wifi_stage == WST_FULL;
            if (btn == 1 && !ok) { wifi_request(wifi_stage == WST_TIMEOUT ? WREQ_START : WREQ_RETRY); break; }
            if (btn == 1 || btn == 0) { wifi_request(WREQ_STOP); set_page = PG_WIFI; set_row = WF_LIST; }
            break;
        }
        if (btn == 0 && !under_way) { set_page = PG_WHELP; break; }   // YARDIM
        if (btn == 1) leave_settings();    // OYUNA DÖN: play on, the setup carries on
        break;
    case PG_WHELP:
        set_page = PG_WSESSION;
        break;
    case PG_WLIST:
        if (row >= wifi_count) { set_page = PG_WIFI; set_row = WF_LIST; break; }
        if (wifi_fixed & (1 << row)) { cue(SFX_REFUSE, V_NONE); return; }   // built into the firmware
        set_page = PG_WFORGET; wifi_sel = 0; wifi_arg = (u8)row;
        break;
    case PG_WFORGET:                       // GERİ | UNUT
        if (btn == 1) wifi_request(WREQ_FORGET, wifi_arg);
        set_page = PG_WLIST; set_row = 0;
        break;
    }
    dirty = 1;
    cue(SFX_TAP);
}

// ---- firmware update page (the platform checks, downloads and verifies; see fw/src/ota.cpp) --------

void Game::update_status(int stage, int percent, int version) {
    update_stage = (u8)stage;
    update_pct = (u8)(percent < 0 ? 0 : percent > 100 ? 100 : percent);
    if (version > 0) update_ver = (u16)version;
    dirty = 1;
}

int Game::update_view() const {
    bool rest = update_stage == UST_IDLE || update_stage == UST_LATEST || update_stage == UST_AVAILABLE;
    if (rest && update_waiting()) return UST_AVAILABLE;
    return update_stage == UST_AVAILABLE ? UST_LATEST : update_stage;   // AVAILABLE needs a newer version
}

// GERİ | the action: KONTROL ET, YÜKLE (only on USB power or >= 50 % battery) or TEKRAR (a new check).
// While it downloads, verifies and restarts there is nothing to press: the page stays until the restart.
void Game::update_input(int type, int x, int y) {
    int v = update_view();
    if (v >= UST_DOWNLOADING && v <= UST_RESTARTING) return;
    bool action = v != UST_CHECKING && (v != UST_AVAILABLE || update_allowed());
    int btn = -1;
    if (type == IN_NEXT) { if (action) wifi_sel ^= 1; dirty = 1; return; }
    if (type == IN_TOUCH) btn = wifi_button(x, y); else if (type == IN_SELECT) btn = action ? wifi_sel : 0;
    if (btn < 0) return;
    if (btn == 1 && !action) { if (v == UST_AVAILABLE) cue(SFX_REFUSE, V_NONE); return; }   // the battery rule
    if (btn == 0) { set_page = PG_HUB; set_row = HUB_UPDATE; }
    else if (v == UST_AVAILABLE) { update_req = UREQ_INSTALL; update_stage = UST_DOWNLOADING; update_pct = 0; }
    else { update_req = UREQ_CHECK; update_stage = UST_CHECKING; }
    dirty = 1;
    cue(SFX_TAP);
}

// Out of the settings, back to where the grown-up found her: an egg stays an egg, a sleeper sleeps.
void Game::leave_settings() {
    if (!s.hatched) go((s.birth_flags & BIRTH_CHOSEN) ? SC_EGG : SC_EGG_PICK);
    else if (back_scene == SC_SLEEP && back_night && !bedtime() && today().known) {   // morning came meanwhile
        go(plugged && !screen_on ? SC_LOUNGE : SC_HOME);   // closed in the dark: up quietly, as at check_bedtime
        if (screen_on) { if (!routines_on()) cue(SFX_WAKE, V_MORNING); greet(); }   // (the routine says hello)
    } else if (back_scene == SC_SLEEP) {   // the same sleep goes on: no new nap bonus, no new dreams
        go(SC_SLEEP);
        scene_ms = back_ms;
        standing = back_standing;
        night_sleep = back_night || bedtime() != BED_NONE;
    }
    else go(plugged && !screen_on ? SC_LOUNGE : SC_HOME);   // closed in the dark on the charger: the lounger
}

void Game::choose_egg() {                  // this one is mine: her colours are set for life
    s.character = sel;
    s.birth_flags |= BIRTH_CHOSEN;
    want_save = 1;
    taps = 0;
    go(SC_EGG);
    cue(SFX_CHOOSE);
}

bool Game::foam_window() const {
    return scene == SC_WASH && scene_ms >= FOAM_FROM_MS && scene_ms < FOAM_TO_MS;
}

void Game::pop_foam(int i) {               // bath: each puff pops once; the first becomes a crown
    if (i < 0 || i > 2 || (foam_mask & (1 << i)) || !foam_window()) return;
    static const short FX[3] = {104, 184, 264}, FY[3] = {190, 150, 190};
    foam_mask |= (u8)(1 << i);
    foam_at[i] = (u16)scene_ms;
    burst(P_RING, FX[i], FY[i], 1);
    burst(P_BUBBLE, FX[i], FY[i], 2);
    cue(SFX_POP);
    for (int k = 1; k <= 3; k++)           // next unpopped puff for the buttons
        if (!(foam_mask & (1 << ((i + k) % 3)))) { foam_sel = (u8)((i + k) % 3); break; }
}

void Game::start_stars() {                 // the star-popping game (needs a bit of energy)
    u16* v = s.stat;
    pet_x = 0;                             // staged centre-front, whichever way she was wandering
    facing = 1;
    if (v[ENERGY] < PLAY_MIN_ENERGY) { go(SC_REFUSE); cue(SFX_REFUSE); return; }
    v[HAPPY] = (u16)clamp(v[HAPPY] + PLAY_HAPPY);
    v[ENERGY] = (u16)clamp(v[ENERGY] - PLAY_COST);
    v[HUNGER] = (u16)clamp(v[HUNGER] - PLAY_HUNGER);
    v[CLEAN] = (u16)clamp(v[CLEAN] - PLAY_DIRT);
    score = 0;
    sel = 0;
    for (int i = 0; i < GAME_TARGETS; i++) spawn_target(i, 250 + i * 70);
    go(SC_GAME);
    cue(SFX_PLAY);
    want_save = 1;
}

void Game::open_toy(int toy) {             // Oyna's toy box
    switch (toy) {
    case TOY_STARS: start_stars(); return;
    case TOY_MUSIC:
        note_count = 0; echo_i = 0; sel = 1;
        for (int i = 0; i < 3; i++) flower_at[i] = 0xFFFF;
        go(SC_MUSIC);
        break;
    case TOY_CLOUD: {
        static const short LANES[3] = {64, 184, 304};
        cloud_x = walk_to = SCREEN_W / 2;  // walk_to doubles as the steered target here
        tilt = tilt_f = 0;
        tilt_at = 0;
        landed = 0;
        for (int i = 0; i < FEATHERS; i++) {
            fea[i].x = LANES[(i * 2 + (int)(crandom() % 2)) % 3];
            fea[i].y = 120;
            fea[i].state = 0;
        }
        go(SC_CLOUD);
        break;
    }
    case TOY_DRESS: preview = s.accessory; go(SC_DRESS); break;
    default: go(SC_HOME); return;
    }
    cue(SFX_TAP);
}

void Game::play_note(int f) {              // a flower sings; she will sing the tune back
    if (f < 0 || f > 2) return;
    flower_at[f] = (u16)(scene_ms > 60000 ? 60000 : scene_ms);
    if (note_count == NOTES) {
        for (int i = 1; i < NOTES; i++) notes[i - 1] = notes[i];
        note_count--;
    }
    notes[note_count++] = (u8)f;
    last_note_ms = scene_ms;
    echo_i = 0;                            // a new note cancels a pending echo
    cue(SFX_NOTE1 + f);
}

// The cable. A sample only counts once the next one agrees (a wobbly connector), and the first
// sample after power-up just says how things are: no fanfare for booting on the charger.
void Game::set_plugged(bool usb) {
    u8 p = usb ? 1 : 0;
    if (!plug_known) { plug_known = 1; plugged = plug_raw = p; return; }
    bool agree = p == plug_raw;
    plug_raw = p;
    if (!agree || p == plugged) return;
    plugged = p;
    if (!p) unplug_at = stamp();
    // Only at home (or on the lounger) does the cable light the screen and change the scene: not
    // while she sleeps, not out with friends, not an egg, not mid-action or in the settings.
    if (scene != SC_HOME && scene != SC_INFO && scene != SC_LOUNGE) return;
    if (phase() != PH_NIGHT) { screen_on = 1; idle_ms = 0; }   // at night the room stays dark
    dirty = 1;
    if (!p) {
        if (scene == SC_LOUNGE) { go(SC_HOME); cue(SFX_WAKE); burst(P_SPARKLE, SCREEN_W / 2 + pet_x, 200, 4); }
        return;
    }
    if (scene == SC_LOUNGE) return;
    bool quiet = unplug_at && since(unplug_at) < PLUG_QUIET_MS;
    pet_x = 0;
    facing = 1;
    if (phase() == PH_NIGHT && s.stat[ENERGY] < NEED_LOW) { act(MENU_SLEEP); return; }   // tired: to bed
    go(SC_LOUNGE);                         // otherwise: a lie-down on the lounger (sun by day, a mask at night)
    if (!quiet && screen_on) cue(SFX_CHARGE);   // the room stays quiet when the screen does
}

// BOOT, sampled every loop. Taps are counted and turned into IN_NEXT only when the sequence is
// over; three quick presses with the last one held become IN_PARENT instead, with no cursor moves.
void Game::set_boot(bool down) {
    u32 now = clock_ms + acc_ms;
    if (down != (bool)boot_down) {
        if (boot_t && now + 1 - boot_t < KEY_BOUNCE_MS) return;   // contact bounce (unsigned: wrap-safe)
        if (down && boot_taps && now + 1 - boot_t > TAP_GAP_MS) {  // a late press: the old taps first
            int n = boot_taps;
            boot_taps = 0;
            while (n--) input(IN_NEXT, 0, 0);
        }
        boot_down = down;
        if (!down && !boot_fired) boot_taps++;
        boot_fired = 0;
        boot_t = now + 1;
    }
    if (boot_down && !boot_fired && boot_taps >= PARENT_TAPS - 1 && now + 1 - boot_t >= PARENT_HOLD_MS) {
        boot_fired = 1;                    // the grown-up gesture
        boot_taps = 0;
        input(IN_PARENT, 0, 0);
    }
    if (!boot_down && boot_taps && now + 1 - boot_t >= TAP_GAP_MS) {   // just taps: move along
        int n = boot_taps;
        boot_taps = 0;
        while (n--) input(IN_NEXT, 0, 0);
    }
}

void Game::set_tilt(int v) {
    tilt = (i16)(v < -100 ? -100 : v > 100 ? 100 : v);
    tilt_at = clock_ms ? clock_ms : 1;
}

void Game::mode_input(int type, int x, int y) {
    if (type == IN_TOUCH && x >= HOUSE_X0 && y <= HOUSE_Y1) { go(SC_HOME); cue(SFX_TAP); return; }   // house
    switch (scene) {
    case SC_TOYS:
        if (type == IN_TOUCH) {
            for (int i = 0; i < TOY_COUNT; i++) {
                int cx = i == TOY_HOME ? SCREEN_W / 2 : 116 + (i % 2) * 136, cy = 148 + (i / 2) * 116;
                if (iabs(x - cx) <= 52 && iabs(y - cy) <= 52) { press(i); open_toy(i); return; }
            }
        } else if (type == IN_NEXT) { sel = (u8)((sel + 1) % TOY_COUNT); dirty = 1; cue(SFX_TAP); }
        else { press(sel); open_toy(sel); }                         // PWR or shake: the highlighted toy
        break;
    case SC_MUSIC:
        if (type == IN_TOUCH) {
            for (int i = 0; i < 3; i++)
                if (iabs(x - (64 + i * 120)) <= 48 && iabs(y - 270) <= 56) { sel = (u8)i; play_note(i); return; }
        } else if (type == IN_NEXT) { sel = (u8)((sel + 1) % 3); dirty = 1; }
        else if (type == IN_SELECT) play_note(sel);
        else if (type == IN_SHAKE) { sel = (u8)((sel + 1) % 3); play_note(sel); }
        break;
    case SC_CLOUD:
        if (type == IN_TOUCH) walk_to = (i16)(x < 123 ? 64 : x > 245 ? 304 : 184);   // steer by thirds
        else if (type == IN_NEXT) walk_to = (i16)(walk_to >= 304 ? 64 : walk_to + 120);
        else if (type == IN_SELECT) walk_to = SCREEN_W / 2;
        else if (type == IN_SHAKE) burst(P_SPARKLE, cloud_x, CLOUD_Y, 3);   // a cosmetic puff
        dirty = 1;
        break;
    case SC_DRESS:
        if (type == IN_TOUCH) {
            if (y >= 340 && y <= 440) {
                int i = (x - 16) / 86;
                if (x >= 16 && i >= 0 && i < ACC_COUNT) {
                    preview = (u8)i;
                    if (s.accessory != i) want_save = 1;
                    s.accessory = (u8)i;
                    dress_at = clock_ms ? clock_ms : 1;
                    burst(P_SPARKLE, SCREEN_W / 2 + 40, 140, 3);
                    cue(SFX_DRESS);
                }
            }
        } else if (type == IN_NEXT || type == IN_SHAKE) { preview = (u8)((preview + 1) % ACC_COUNT); dirty = 1; }
        else if (type == IN_SELECT) {
            if (s.accessory != preview) want_save = 1;
            s.accessory = preview;
            dress_at = clock_ms ? clock_ms : 1;
            burst(P_SPARKLE, SCREEN_W / 2 + 40, 140, 3);
            cue(SFX_DRESS);
        }
        break;
    }
}

void Game::act(int item) {
    u16* v = s.stat;
    pet_x = 0;                             // actions are staged centre-front
    facing = 1;
    switch (item) {
    case MENU_FEED:                        // she gets to choose: three big pictures
        sel = treat;
        go(SC_FEED);
        cue(SFX_TAP);
        return;
    case MENU_PLAY:                        // the toy box: stars, music, cloud, mirror
        sel = 0;
        go(SC_TOYS);
        cue(SFX_TAP);
        return;
    case MENU_SLEEP:
        sleep_ms = 0;
        napped = 0;
        tucked = 0;
        tuck_ms = 0;
        remember();
        plan_dreams();
        go(SC_SLEEP);
        cue(SFX_SLEEP);
        break;
    case MENU_WASH:
        wash_messes = (u8)messes();        // shown dissolving during the bath
        foam_mask = 0;
        foam_sel = 0;
        v[CLEAN] = MAX_STAT;
        v[HAPPY] = (u16)clamp(v[HAPPY] + TREAT_HAPPY);
        remember();
        celebrated = 0;
        go(SC_WASH);
        cue(SFX_WASH);
        break;
    }
    want_save = 1;
}

// ---- input ---------------------------------------------------------------------------------

void Game::input(int type, int x, int y) {
    if (type == IN_PARENT) {               // a grown-up: call her back early, or open settings
        screen_on = 1;
        idle_ms = 0;
        if (scene == SC_AWAY || scene == SC_LEAVE) { away_ms = 0; come_back(); }
        // (the egg picker too: it is a new toy's first screen, where a grown-up sets language, WiFi and clock)
        else if (scene == SC_HOME || scene == SC_EGG || scene == SC_EGG_PICK || scene == SC_INFO || scene == SC_LOUNGE ||
                 scene == SC_SLEEP) {
            back_scene = scene;
            back_ms = scene_ms;
            back_standing = standing;
            back_night = night_sleep;
            set_row = 0;
            // a WiFi setup or an update under way: straight back to it
            set_page = wifi_busy() ? PG_WSESSION : update_busy() ? PG_UPDATE : PG_MAIN;
            wifi_sel = 1;
            go(SC_SETTINGS);
        }
        return;
    }
    if (type == IN_SHAKE && (s.opts & OPT_NO_SHAKE)) return;
    if (!screen_on) {                      // first touch only wakes the screen
        screen_on = 1;
        idle_ms = 0;
        dirty = 1;
        // deadlines set before a long dark spell would read as "in the future" after ~25 days of
        // dark (u32 wrap): start the cosmetic ones afresh
        pet_cooling = 0;
        wobbling = 0;
        idle_act = IDLE_NONE;
        idle_next = clock_ms + 1000;
        trick_at = 0;
        visitor = 0;
        visit_due = 0;
        if (scene == SC_AWAY) { if (away_ms == 0) come_back(); else away_next(); }   // she's back! (or still out)
        else if (scene == SC_HOME && dark_ms >= REST_RESET_MS) greet();   // after a real break
        return;
    }
    if (scene != SC_AWAY) idle_ms = 0;     // tapping does not keep the away screen lit
    switch (scene) {
    case SC_AWAY:
        if (away_ms == 0) { come_back(); break; }
        burst(P_HEART, SCREEN_W / 2, 170, 2);   // the friends wave back; nothing to win by tapping
        if (type == IN_TOUCH) cue(SFX_POP);     // (not SFX_HELLO: that is her coming-home tune)
        break;
    case SC_SETTINGS:
        settings_input(type, x, y);
        break;
    case SC_ASK:
        routine_input(type, x, y);
        break;
    case SC_TOYS: case SC_MUSIC: case SC_CLOUD: case SC_DRESS:
        mode_input(type, x, y);
        break;
    case SC_WASH:                          // pop the foam puffs (the bath finishes either way)
        if (!foam_window()) break;
        if (type == IN_TOUCH) {
            static const short FX[3] = {104, 184, 264}, FY[3] = {190, 150, 190};
            for (int i = 0; i < 3; i++)
                if (iabs(x - FX[i]) <= 36 && iabs(y - FY[i]) <= 36) { pop_foam(i); break; }
        } else if (type == IN_NEXT) {
            for (int k = 1; k <= 3; k++)
                if (!(foam_mask & (1 << ((foam_sel + k) % 3)))) { foam_sel = (u8)((foam_sel + k) % 3); break; }
            dirty = 1;
        } else {
            pop_foam(foam_sel);
        }
        break;
    case SC_EGG_PICK: {                    // tap an egg to highlight it, tap again (or the heart) to choose
        static const short EX[4] = {100, 268, 100, 268}, EY[4] = {170, 170, 310, 310};
        if (type == IN_TOUCH) {
            if (iabs(x - SCREEN_W / 2) <= 36 && iabs(y - 410) <= 28 && preview) { choose_egg(); break; }
            for (int i = 0; i < 4; i++)
                if (iabs(x - EX[i]) <= 60 && iabs(y - EY[i]) <= 60) {
                    if (preview && sel == i) choose_egg();
                    else { sel = (u8)i; preview = 1; cue(SFX_TAP); dirty = 1; }
                    break;
                }
        } else if (type == IN_SELECT) {
            choose_egg();
        } else {                           // BOOT or shake: highlight the next egg
            sel = (u8)((sel + 1) % 4);
            preview = 1;
            cue(SFX_TAP);
            dirty = 1;
        }
        break;
    }
    case SC_FEED:
        if (type == IN_TOUCH) {
            if (y >= 110 && y <= 300) {    // the three cards
                int t = x / (SCREEN_W / TREAT_COUNT);
                t = t < TREAT_COUNT ? t : TREAT_COUNT - 1;
                press(t);
                eat(t);
            }
        } else if (type == IN_NEXT) {
            sel = (u8)((sel + 1) % TREAT_COUNT);
            dirty = 1;
            cue(SFX_TAP);
        } else if (type == IN_SELECT) {
            press(sel);
            eat(sel);
        }
        break;
    case SC_EGG:                           // taps and shakes both nudge the egg
        dirty = 1;
        wobble_end = clock_ms + WOBBLE_MS;
        wobbling = 1;
        if (++taps >= EGG_TAPS) hatch();
        else cue(SFX_TAP);
        break;
    case SC_LOUNGE:                        // a touch on her: a happy wave; anything else, as at home
        if (type == IN_TOUCH && y >= HEADER_Y1 && y < MENU_Y0) {
            burst(P_HEART, SCREEN_W / 2 + pet_x + facing * 60, 200, 2);
            cue(SFX_PET);
            pet_gain();
            remember();
            break;
        }
        go(SC_HOME);
        [[fallthrough]];
    case SC_HOME:
        if (play_ms >= play_limit_ms()) { if (bedtime()) doze(true); else start_leave(); break; }   // time's up
        if (type == IN_NEXT) {
            menu = (u8)((menu + 1) % MENU_COUNT);
            dirty = 1;
            cue(SFX_TAP);
        } else if (type == IN_SHAKE) {     // shaking the toy = straight to the stars
            menu = MENU_PLAY;
            start_stars();
        } else if (type == IN_SELECT) {
            press(menu);
            act(menu);
        } else if (type == IN_TOUCH) {
            if (y >= MENU_Y0) {            // the whole bottom strip: the nearest button, gutters too
                int item = x / MENU_STEP;
                if (item < 0) item = 0;
                if (item >= MENU_COUNT) item = MENU_COUNT - 1;
                menu = (u8)item;
                press(item);
                act(item);
            } else if (y < HEADER_Y1) {
                go(SC_INFO);
                cue(SFX_INFO);
            } else if (x >= PET_X0 + pet_x && x <= PET_X1 + pet_x && y >= PET_Y0 && y <= PET_Y1) {
                pet();
            }
        }
        break;
    case SC_GAME:
        if (type == IN_TOUCH && x >= HOUSE_X0 && y <= HOUSE_Y1) { go(SC_HOME); cue(SFX_TAP); break; }   // house
        if (type == IN_TOUCH) {
            int best = -1, bestd = 1 << 30;
            for (int i = 0; i < GAME_TARGETS; i++) {   // where the star is drawn right now
                int ty = tgt[i].y + tgt[i].vy * (int)acc_ms / STEP_MS;
                int dx = iabs(x - tgt[i].x), dy = iabs(y - ty);
                if (tgt[i].alive && dx <= TARGET_HIT && dy <= TARGET_HIT && dx + dy < bestd) {
                    best = i;
                    bestd = dx + dy;
                }
            }
            if (best >= 0) pop(best);
            else { burst(P_SPARKLE, x, y, 2); cue(SFX_TAP); }   // a miss still answers
        } else if (type == IN_NEXT) {
            sel = (u8)((sel + 1) % GAME_TARGETS);
            dirty = 1;
            cue(SFX_TAP);
        } else if (type == IN_SELECT) {    // the selected star, or else the highest one in view
            pop(visible(sel) ? sel : highest_visible());
        } else if (type == IN_SHAKE) {     // shake pops the highest star in view
            pop(highest_visible());
        }
        break;
    case SC_ALBUM:                         // look and leaf through the books; the house goes home
        if (type == IN_NEXT || (type == IN_TOUCH && y >= 380 && (x < 38 || x >= SCREEN_W - 38))) {
            int dir = type == IN_TOUCH && x < 38 ? -1 : 1;
            int b = (int)book + dir;
            if (type == IN_NEXT) b = b % books();
            if (b >= 0 && b < books()) { book = (u16)b; cue(SFX_TAP); dirty = 1; }
            break;
        }
        if (type != IN_TOUCH || (x >= HOUSE_X0 && y <= HOUSE_Y1)) { go(SC_HOME); break; }
        if (y >= 82 && y < 82 + 3 * 76 && x >= 12 && x < 12 + 4 * 86) {   // a picture twinkles when touched
            int i = (y - 82) / 76 * 4 + (x - 12) / 86;
            if (i < album()) { burst(P_SPARKLE, 12 + (i % 4) * 86 + 40, 82 + (i / 4) * 76 + 34, 3); cue(SFX_TAP); }
        }
        break;
    case SC_INFO:
        if ((type == IN_TOUCH && x <= BOOK_X1 && y <= BOOK_Y1) || type == IN_SELECT) {   // the album
            album_unseen = 0;
            book = (u16)(books() - 1);     // the newest book
            go(SC_ALBUM);
            cue(SFX_INFO);
            break;
        }
        go(SC_HOME);                       // sound is the parent's setting (volume), not a button here
        break;
    case SC_SLEEP:                         // first the blanket, then waking her up early is allowed
        if (!tucked_in()) {
            tucked = 1;
            tuck_ms = (u16)scene_ms;
            burst(P_HEART, 52, 280, 2);
            cue(SFX_PET);
            dirty = 1;
            break;
        }
        if (bed_locked()) {                // night: a sleepy murmur, and she sleeps on
            burst(P_NOTE, SCREEN_W / 2 + 60, standing ? 170 : 230, 1);
            dirty = 1;
            break;
        }
        wake();
        break;
    case SC_REFUSE:                        // the big pillow: let her sleep right away
        if (type == IN_SELECT || (type == IN_TOUCH && x >= 40 && x <= 328 && y >= 352)) {   // pillow button
            menu = MENU_SLEEP;
            act(MENU_SLEEP);
        } else {
            go(SC_HOME);
        }
        break;
    default:                               // hatching and care animations play out in full
        break;
    }
}

// ---- cosmetic simulation, one STEP_MS at a time ------------------------------------------

void Game::step() {
    bool changed = false;
    for (int i = 0; i < NPART; i++) {
        Part& p = part[i];
        if (p.kind == P_NONE) continue;
        changed = true;
        p.x = (i16)(p.x + p.vx * 2);
        p.y = (i16)(p.y + p.vy * 2);
        if (p.kind == P_CRUMB || p.kind == P_CONFETTI) { if (p.vy < 6) p.vy++; }       // gravity
        if (p.kind == P_BUBBLE) p.x = (i16)(p.x + (((p.life >> 1) & 1) ? 1 : -1));      // wobble
        if (--p.life == 0 || p.y < -24 || p.y > SCREEN_H + 24) p.kind = P_NONE;
    }

    vel_x = 0;
    switch (scene) {
    case SC_EGG:
        if (wobbling) { changed = true; if (due(wobble_end)) wobbling = 0; }
        break;
    case SC_LEAVE:                         // a word first, then off to the right
        facing = 1;
        if (scene_ms > 1200 + FRIEND_MS) { vel_x = 12; pet_x = (i16)(pet_x + vel_x); }
        if (scene_ms == 1200 + FRIEND_MS - 400) burst(P_HEART, SCREEN_W / 2 + 90, 150, 3);   // hug
        changed = true;
        break;
    case SC_RETURN:                        // in from the left, then a happy hello
        facing = 1;
        if (pet_x < 0) {
            vel_x = (i8)(pet_x + 15 > 0 ? -pet_x : 15);
            pet_x = (i16)(pet_x + vel_x);
            if (pet_x == 0) burst(P_HEART, SCREEN_W / 2 + 42, 112, 4);
        }
        changed = true;
        break;
    case SC_HOME: {
        if (!visit_due) visit_due = clock_ms + VISIT_FIRST_MS;       // a butterfly drops by now and then
        if (!visitor && due(visit_due)) { visitor = 1; visit_start = clock_ms; did |= DID_BUTTERFLY; }
        if (visitor) {
            u32 t = clock_ms - visit_start;
            if (t >= 1000 && t < 1100 && idle_act == IDLE_NONE) { idle_act = IDLE_BLINK; idle_end = clock_ms + 160; }
            if (t >= VISIT_MS) { visitor = 0; visit_due = clock_ms + VISIT_EVERY_MS; }
            changed = true;
        }
        if (greeting) { if (due(greet_end)) greeting = 0; changed = true; }
        if (memory_new) {                  // the day's picture floats up to the album (via the name)
            if (!memory_at) memory_at = stamp();
            if (since(memory_at) >= MEMORY_SHOW_MS) memory_new = 0;
            changed = true;
        }
        if (petting && due(pet_react_end)) { petting = 0; changed = true; }
        if (party && due(party_end)) party = 0;
        check_life();
        if (party) changed = true;
        if (petting || greeting || party) break;
        Mood m = mood();
        bool lively = (m == M_HAPPY || m == M_IDLE) && !bedtime();   // needy or sleepy: stay put and blink
        if (!lively && idle_act >= IDLE_WALK) { idle_act = IDLE_NONE; changed = true; }
        // Every second lively idle moment can be a trick she has learned, taking turns, at most
        // one per TRICK_GAP_MS. A trick she has just learned comes first (with a burst of stars).
        // Calm mode skips the hopping and spinning ones.
        // (Turning is drawn by render from idle_end; `facing` itself never changes during a trick.)
        u8 can[TRICKS];
        int known = 0;
        for (int k = 0; k < tricks(); k++) if (!calm() || TRICK_CALM[k]) can[known++] = (u8)k;
        if (idle_act == IDLE_NONE && due(idle_next) && lively && known && !visitor && (++idle_count & 1) == 0 &&
            (!trick_at || since(trick_at) >= TRICK_GAP_MS)) {
            (void)crandom();               // the same random sequence as an ordinary idle moment
            int k = can[trick_next++ % known];
            if (s.tricks_seen < tricks()) {        // show off the newest one first
                for (int j = known - 1; j >= 0; j--) if (can[j] >= s.tricks_seen) { k = can[j]; break; }   // the newest
                s.tricks_seen = (u8)tricks();
                want_save = 1;
                burst(P_STAR, SCREEN_W / 2 + pet_x, 150, 5);
            }
            idle_act = (u8)(IDLE_HUM + k);
            idle_end = clock_ms + TRICK_MS[k];
            trick_at = stamp();
            changed = true;
        } else if (idle_act == IDLE_NONE && due(idle_next)) {
            u32 r = crandom() % 100;
            if (r < 40 || !lively) {
                idle_act = IDLE_BLINK;
                idle_end = clock_ms + 160;
            } else if (r < 80) {
                int t = (int)(crandom() % (2 * WALK_RANGE / WALK_SPEED + 1)) * WALK_SPEED - WALK_RANGE;
                if (iabs(t - pet_x) < 12) t = pet_x > 0 ? -WALK_RANGE : WALK_RANGE;
                walk_to = (i16)t;
                facing = (i8)(t > pet_x ? 1 : -1);
                idle_act = IDLE_WALK;
            } else {
                idle_act = IDLE_HOP;
                idle_end = clock_ms + 600;
            }
            changed = true;
        }
        if (idle_act == IDLE_WALK) {
            int d = walk_to - pet_x;
            vel_x = (i8)(d > 0 ? (d < WALK_SPEED ? d : WALK_SPEED) : (d < -WALK_SPEED ? -WALK_SPEED : d));
            pet_x = (i16)(pet_x + vel_x);
            if (pet_x == walk_to) idle_act = IDLE_NONE;
            changed = true;
        } else if (idle_act >= IDLE_HUM && !due(idle_end)) {   // a trick in progress
            int k = idle_act - IDLE_HUM, len = TRICK_MS[k];
            u32 p = clock_ms - (idle_end - len);
            bool foal = stage() == STAGE_FOAL;
            int mx = SCREEN_W / 2 + pet_x + facing * (foal ? 52 : 78), my = foal ? 230 : 190;   // her mouth
            switch (idle_act) {
            case IDLE_HUM:     if (p % 400 == 0) burst(P_NOTE, mx, my, 1); break;   // notes float up
            case IDLE_BUBBLES: if (p % 200 == 0) burst(P_BUBBLE, mx, my + 10, 1); break;
            case IDLE_BOW:     if (p == (u32)len - 200) burst(P_SPARKLE, mx, my + 40, 3); break;
            case IDLE_DANCE:   if (p == (u32)len / 2 - 100 || p == (u32)len - 200) burst(P_SPARKLE, SCREEN_W / 2 + pet_x, 300, 3); break;
            case IDLE_RAINBOW: if (p == (u32)len - 300) burst(P_SPARKLE, SCREEN_W / 2 + pet_x, 150, 4); break;
            case IDLE_SHOW:    if (p == (u32)len / 2) burst(P_CONFETTI, SCREEN_W / 2 + pet_x, 200, 8); break;
            }
            changed = true;
        } else if (idle_act != IDLE_NONE && due(idle_end)) {
            idle_act = IDLE_NONE;
            changed = true;
        }
        if (idle_act == IDLE_NONE && changed && due(idle_next))
            idle_next = clock_ms + 3000 + crandom() % 4000;
        break;
    }
    case SC_MUSIC:                         // after a pause she sings the little tune back, once
        if (note_count && !echo_i && scene_ms - last_note_ms >= ECHO_WAIT_MS) {
            echo_i = 1;
            echo_next = scene_ms;
            set_echo(notes, note_count);   // the whole tune as one cue: no note lost to a long tick
            cue(SFX_ECHO);
        }
        if (echo_i && scene_ms >= echo_next) {
            if (echo_i <= note_count) {
                burst(P_NOTE, SCREEN_W / 2 + 30, 130, 1);
                echo_i++;
                echo_next = scene_ms + ECHO_GAP_MS;
            } else {
                echo_i = 0;
                note_count = 0;
            }
        }
        changed = true;
        break;
    case SC_CLOUD: {
        // Steered by touch/buttons, or by tilting the toy. Tilt maps continuously past a small dead zone (no
        // jump at its edge), the smoothing is light (it lagged ~400 ms), and the cloud closes half the gap a
        // step, up to CLOUD_SPEED (it crawled at 80 px/s, a feather could not be caught across the screen).
        int target = walk_to;
        bool fresh = tilt_at && (u32)(clock_ms - tilt_at) <= TILT_STALE_MS;
        tilt_f = (i16)((tilt_f + (fresh ? tilt : 0)) / 2);
        if (tilt_f > TILT_DEAD || tilt_f < -TILT_DEAD) {
            int t = tilt_f > 0 ? tilt_f - TILT_DEAD : tilt_f + TILT_DEAD;
            target = SCREEN_W / 2 + t * 120 / (100 - TILT_DEAD);
        }
        int d = target - cloud_x, v = d / 2;
        if (!v && d) v = d > 0 ? 1 : -1;
        if (v > CLOUD_SPEED) v = CLOUD_SPEED;
        if (v < -CLOUD_SPEED) v = -CLOUD_SPEED;
        int nx = cloud_x + v;
        if (nx < 64) nx = 64;
        if (nx > 304) nx = 304;
        cloud_vx = (i16)(nx - cloud_x);
        cloud_x = (i16)nx;
        for (int i = 0; i < FEATHERS; i++) {
            Feather& f = fea[i];
            if (f.state == 0 && scene_ms >= (u32)i * FEATHER_EVERY_MS) f.state = 1;
            if (f.state != 1) continue;
            f.y = (i16)(f.y + FEATHER_FALL);
            if (f.y >= CLOUD_Y - 10 && f.y < CLOUD_Y - 10 + FEATHER_FALL && iabs(f.x - cloud_x) <= CLOUD_CATCH) {
                f.state = 2;               // lands softly on the cloud
                landed++;
                burst(P_SPARKLE, f.x, CLOUD_Y - 12, 2);
                if (!land_cue_at || (u32)(clock_ms - land_cue_at) >= 500) { cue(SFX_LAND); land_cue_at = clock_ms; }
            } else if (f.y >= 380) {
                f.state = 3;               // drifts to the grass: no loss, just a feather on the floor
            }
        }
        changed = true;
        break;
    }
    case SC_SLEEP:
        if (!tucked && scene_ms >= TUCK_MS) { tucked = 1; tuck_ms = TUCK_MS; burst(P_HEART, 150, 250, 2); }
        if (dream()) changed = true;
        break;
    case SC_EAT:                           // crumbs fly on every bite
        if ((scene_ms / STEP_MS) % 3 == 0) burst(P_CRUMB, 278, 326, 2);
        break;
    case SC_WASH:
        if ((scene_ms / STEP_MS) % 2 == 0) burst(P_BUBBLE, 110 + (int)(crandom() % 150), 300, 1);
        if (!celebrated && scene_ms + 700 >= WASH_MS) {   // squeaky clean
            burst(P_SPARKLE, SCREEN_W / 2, 220, 5);
            celebrated = 1;
        }
        changed = true;
        break;
    case SC_GAME:
        for (int i = 0; i < GAME_TARGETS; i++) {
            tgt[i].y = (i16)(tgt[i].y + tgt[i].vy);
            if (tgt[i].y < 70) spawn_target(i, SCREEN_H + 20 + (int)(crandom() % 40));
        }
        changed = true;
        break;
    }
    if (changed) dirty = 1;
}

// Life time: whatever is left after the egg hatches counts as the unicorn's life.
bool Game::counts_as_play() const {
    return s.hatched && scene != SC_EGG && scene != SC_HATCH && scene != SC_LEAVE && scene != SC_AWAY &&
           scene != SC_RETURN && scene != SC_SETTINGS && scene != SC_ASK;   // her question is not play
}

void Game::advance(u32 ms) {
    clock_ms += ms;
    scene_ms += ms;
    anim_ms += ms;
    idle_ms += ms;
    save_ms += ms;
    if (screen_on) {                       // screen time, reset by a long enough break
        dark_ms = 0;
        if (counts_as_play() && !greeting) play_ms += ms;   // a hello is not play
    } else {
        dark_ms += ms;
        if (dark_ms >= REST_RESET_MS) play_ms = 0;
    }
    bool out = scene == SC_AWAY || (scene == SC_ASK && r_back == SC_AWAY);   // (a question from her break: still out)
    if (out && away_ms && !(s.away_until && utc_s))   // no clock: count powered time
        away_ms = away_ms > ms ? away_ms - ms : 0;
    u32 grow = 0;
    if (!s.hatched) {
        if (scene == SC_EGG) s.egg_ms += ms;   // the picker does not age the egg
        if (s.egg_ms >= EGG_HATCH_MS && scene == SC_EGG) { grow = s.egg_ms - EGG_HATCH_MS; hatch(); }
    } else {
        grow = ms;
    }
    minute_ms += grow;
    while (minute_ms >= 60000) {
        minute_ms -= 60000;
        minute();
        if (rested()) {                    // slept enough: wakes up by herself
            s.stat[HAPPY] = (u16)clamp(s.stat[HAPPY] + TREAT_HAPPY);
            want_save = 1;
            wake();
        }
    }
    sec_ms += grow;
    while (sec_ms >= 1000) { sec_ms -= 1000; s.age_s++; }
    if (scene == SC_SLEEP) {               // a short nap already helps
        sleep_ms += ms;
        if (!napped && sleep_ms >= NAP_MS) {
            napped = 1;
            s.stat[ENERGY] = (u16)clamp(s.stat[ENERGY] + NAP_BONUS);
            dirty = 1;
            if (s.stat[ENERGY] >= MAX_STAT && !bed_locked()) { want_save = 1; wake(); }
        }
    }
}

void Game::scene_timers() {
    switch (scene) {
    case SC_EAT: case SC_WASH:
        if (scene_ms >= (scene == SC_WASH ? WASH_MS : ACTION_MS)) {
            bool washed = scene == SC_WASH;
            go(SC_HOME);
            if (washed) bath_question();
        }
        break;
    case SC_GAME:
        if (scene_ms >= GAME_MS) {         // joy does not depend on the score: stars are just counted
            want_save = 1;
            go(SC_PLAY);                   // celebration: jump after the ball, star count shown
            cue(SFX_CHEER);
            burst(P_CONFETTI, SCREEN_W / 2, 180, 8);
        }
        break;
    case SC_HOME:                          // enough screen time: she has things to do (or, at bedtime, sleep)
        if (play_ms >= play_limit_ms()) { if (bedtime()) doze(true); else start_leave(); }
        else if (plugged && idle_ms >= LOUNGE_IDLE_MS && !party && !greeting && !memory_new && !petting)
            go(SC_LOUNGE);                 // still on the charger: back to the lounger
        break;
    case SC_LOUNGE:
        if (play_ms >= play_limit_ms()) { if (bedtime()) doze(false); else start_leave(); }
        break;
    case SC_LEAVE:
        if (scene_ms >= LEAVE_MS) {
            away_ms = break_ms();
            s.away_until = utc_s ? utc_s + break_ms() / 1000 : 0;   // in UTC: summer time cannot cut it
            want_save = 1;                 // saved right away, so a power cut is no shortcut
            idle_ms = 0;
            pet_x = 0;
            go(SC_AWAY);
        }
        break;
    case SC_AWAY:                          // back as soon as someone is looking
        if (away_ms == 0 && screen_on) { come_back(); }
        break;
    case SC_RETURN:
        if (scene_ms >= RETURN_MS) {
            play_ms = 0;
            want_save = 1;
            go(SC_HOME);
            idle_next = clock_ms + 3000;   // settle in before wandering off again
        }
        break;
    case SC_PLAY:   if (scene_ms >= CELEBRATE_MS) go(SC_HOME); break;
    case SC_FEED:   if (scene_ms >= FEED_PICK_MS) eat(sel); break;
    case SC_TOYS: case SC_MUSIC: case SC_CLOUD: case SC_DRESS:
        if (play_ms >= play_limit_ms() ||                                  // time's up / nobody playing
            (scene != SC_CLOUD && idle_ms >= MODE_IDLE_MS) ||              // (tilting counts as playing)
            (scene == SC_MUSIC && scene_ms >= MUSIC_MS)) { go(SC_HOME); break; }
        if (scene == SC_CLOUD && scene_ms >= CLOUD_MS) {                  // the same happy end for all
            burst(P_CONFETTI, cloud_x, CLOUD_Y - 20, 6);
            go(SC_HOME);
        }
        break;
    case SC_SETTINGS: {                     // a grown-up walked away: closed once the screen is dark, so the
        bool setup = wifi_stage > WST_OFF && wifi_stage < WST_OK;   // child never wakes into them (not while
        if (screen_on || setup || update_busy()) break;             // a WiFi setup or an update is under way)
        if (wifi_stage >= WST_OK) wifi_request(WREQ_STOP);          // a result nobody read: as if TAMAM
        leave_settings();
        break;
    }
    case SC_REFUSE: if (scene_ms >= REFUSE_MS) go(SC_HOME); break;
    case SC_ASK:                            // nobody answered in time: the buttons; nobody pressed: as if silent
        if (r_stage <= RS_LISTEN && since(r_at) >= R_LISTEN_MS) {
            listen_req = LREQ_CANCEL;
            r_stage = RS_BUTTONS; r_at = stamp(); r_sel = 0; dirty = 1;
        } else if (r_stage == RS_BUTTONS && since(r_at) >= R_BUTTONS_MS) listen_result(ANS_SILENT);
        else if (r_stage == RS_REPLY && since(r_at) >= R_REPLY_MS) routine_done();
        break;
    case SC_ALBUM:  if (idle_ms >= ALBUM_IDLE_MS || play_ms >= play_limit_ms()) go(SC_HOME); break;
    case SC_HATCH:  if (scene_ms >= HATCH_MS) go(SC_HOME); break;
    case SC_INFO:   if (scene_ms >= INFO_MS) go(SC_HOME); break;
    case SC_SLEEP:                          // rested (or topped up by the debug slider), once tucked in
        if (rested()) { want_save = 1; wake(); }
        break;
    }
}

// Fixed-step simulation: the world only moves in whole STEP_MS steps, so one big tick and any
// mix of small ones end in exactly the same state. While the screen is dark at home or asleep
// nothing visible can happen, so that time is fast-forwarded in one go (hours of light-sleep).
u32 Game::tick(u32 dt) {
    for (u32 left = dt;;) {                // in pieces of at most an hour: acc_ms can never wrap
    u32 d = left > 3600000u ? 3600000u : left;
    left -= d;
    acc_ms += d;
    while (acc_ms >= STEP_MS) {
        if (s.hatched) { check_bedtime(); check_routine(); check_tv(); }   // every step (dark fast-forward too)
        if (!screen_on && s.hatched && (scene == SC_HOME || scene == SC_SLEEP || scene == SC_AWAY ||
                                        scene == SC_LOUNGE)) {
            u32 n = acc_ms - acc_ms % STEP_MS;
            if (scene == SC_SLEEP && !napped && sleep_ms < NAP_MS && n > NAP_MS - sleep_ms)
                n = NAP_MS - sleep_ms;     // stop exactly at the nap, like small steps would
            advance(n);
            acc_ms -= n;
            continue;
        }
        advance(STEP_MS);
        acc_ms -= STEP_MS;
        scene_timers();
        if (screen_on) step();             // cosmetics only run while someone can see them
        bool busy = scene == SC_EAT || scene == SC_PLAY || scene == SC_WASH || scene == SC_HATCH ||
                    scene == SC_GAME || scene == SC_LEAVE || scene == SC_RETURN || scene == SC_ASK ||
                    (scene == SC_SETTINGS && set_page == PG_WSESSION && wifi_stage > WST_OFF &&
                     wifi_stage < WST_OK) ||      // QR up or joining: stay lit; a result may go dark
                    (scene == SC_SETTINGS && set_page == PG_UPDATE && update_busy());   // checking, downloading
        u32 off = scene == SC_AWAY ? AWAY_SCREEN_OFF_MS : SCREEN_OFF_MS;
        if (screen_on && !busy && idle_ms >= off) { screen_on = 0; visitor = 0; visit_due = 0; }
    }
    if (!left) break;
    }

    u32 period = 900;                      // breathing
    switch (scene) {
    case SC_EAT: case SC_WASH: case SC_PLAY: case SC_GAME: case SC_HATCH: period = 300; break;
    case SC_EGG: period = 500; break;
    case SC_HOME: if (idle_act == IDLE_WALK) period = 250; else if (idle_act >= IDLE_HOP) period = 300; break;
    }
    u8 f = (u8)((anim_ms / period) & 1);
    if (f != frame) { frame = f; dirty = 1; }

    u32 flags = 0;
    if (save_ms >= AUTOSAVE_MS) { save_ms = 0; want_save = 1; }
    if (want_save) { flags |= F_SAVE; want_save = 0; }
    if (want_clock) { flags |= F_CLOCK; want_clock = 0; }
    if (wifi_req) flags |= F_WIFI;         // the platform clears wifi_req once it has taken it
    if (update_req) flags |= F_UPDATE;     // the same for update_req
    if (listen_req) flags |= F_LISTEN;     // the platform clears listen_req once it has taken it
    if (quiet()) { sfx = voice = 0; rclip = 0; }   // muted, or her bedtime (bed time to wake-up): silent
    if (sfx || voice || rclip) flags |= F_SOUND;
    if (screen_on) {
        flags |= F_SCREEN_ON;
        // render() interpolates between steps (acc_ms), so anything that moves wants a fresh
        // frame on every call; the info screen is a still picture
        // the update page while flash is written: only when the progress moves (writes stall PSRAM)
        bool still = scene == SC_INFO ||
                     (scene == SC_SETTINGS && set_page == PG_UPDATE && update_stage >= UST_DOWNLOADING &&
                      update_stage <= UST_RESTARTING);
        if (dirty || !still) { flags |= F_REDRAW; dirty = 0; }
    }
    return flags;
}


// ---- her daily routine ----------------------------------------------------------------------------
// tools/routines.py SCHEDULE: the hour after waking, 90 to 30 minutes before bedtime, set times, and the
// open question late in the evening. Each question once a day (asked = saved at once, so a power cut does
// not ask again), one more try 10 minutes later if the answer was unclear or silent; at random moments at
// least Q_GAP_MIN apart; only at home or on the lounger, never at bedtime, muted, or with an untrusted clock.

static u32 mix(u32 x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
static int bits(u32 v) { int n = 0; for (; v; v &= v - 1) n++; return n; }
enum { BAG_YES = Q_COUNT, BAG_NO = 2 * Q_COUNT, BAG_UNCLEAR = 3 * Q_COUNT, BAG_REPLY, BAG_SILENT, BAG_GREET, BAGS };
static_assert(BAGS <= (int)(sizeof(((Game*)0)->bag) / sizeof(u16)), "a shuffle bag per list");
static const u32 GREET_BIT = 1u << Q_COUNT;   // r_done: the morning hello is done

// day: a local day, -1 = unknown (then only holiday mode counts)
bool Game::holiday_on(int day) const {
    if (s.opts & OPT_HOLIDAY) return true;
    for (int i = 0; day >= 0 && i < HOL_MAX; i++)
        if (s.hol[i][0] && day >= s.hol[i][0] && day <= s.hol[i][1]) return true;
    return false;
}
bool Game::free_on(int day) const { return (day >= 0 && calendar((u32)day * 86400u + 1).wday >= 5) || holiday_on(day); }
int Game::wake_on(int day) const { return clock_min(free_on(day) ? T_WAKE_WE : T_WAKE); }
static int day_or_unknown(const Cal& c) { return c.known ? c.day : -1; }
bool Game::holiday() const { return holiday_on(day_or_unknown(today())); }
bool Game::free_day() const { return free_on(day_or_unknown(today())); }
int Game::wake_min() const { return wake_on(day_or_unknown(today())); }

// Her routine day runs from one wake-up to the next, so a wake-up or a bedtime near midnight keeps its whole
// morning (or evening) on one day. -1: no trusted clock. *min: minutes since that day's midnight, so after
// midnight 1440 and more.
int Game::rday_now(int* min) const {
    Cal c = today();
    if (!c.known) return -1;
    bool before = c.minute < wake_on(c.day);
    if (min) *min = c.minute + (before ? 1440 : 0);
    return c.day - (before ? 1 : 0);
}
int Game::clock_min(int t) const {
    int tens = t == T_BED ? s.bed10 : t == T_WAKE ? s.wake10 : s.wake_we10;
    return tens * 10 + (s.min_ones >> 4 * t & 15);
}
void Game::set_clock_min(int t, int m) {
    u8 tens = (u8)(m / 10);
    if (t == T_BED) s.bed10 = tens; else if (t == T_WAKE) s.wake10 = tens; else s.wake_we10 = tens;
    s.min_ones = (u16)((s.min_ones & ~(15 << 4 * t)) | (m % 10) << 4 * t);
}
int Game::stars_today() const { return rday_now() >= 0 && rday_now() == s.r_day ? bits(s.week[s.r_day % 7]) : 0; }

// When question q may be asked today, [from, to) in minutes of the day; false if not today.
static bool q_window(const Game& g, int q, int day, int* from, int* to) {
    int wake = g.wake_on(day), bed = g.clock_min(T_BED);
    if (bed < wake) bed += 1440;           // a bedtime after midnight: her evening runs on past 24:00
    switch (Q_WHEN[q]) {
    case QW_AM: *from = wake; *to = wake + 60; break;
    case QW_PM: case QW_PM_LAST: *from = bed - 90; *to = bed - 30; break;
    case QW_AT: *from = Q_AT[q]; *to = Q_AT[q] + Q_AT_WINDOW_MIN; break;
    case QW_WASH: case QW_SUGGEST: case QW_CHAT: return false;   // not on the clock (bath_question, now_and_then)
    default: *from = bed - 30 > 19 * 60 ? bed - 30 : 19 * 60; *to = bed - 5; break;   // what made you happy
    }
    bool school = !g.free_on(day);
    int wday = calendar((u32)day * 86400u).wday;   // 0 = Monday
    if ((Q_DAYS[q] == QD_SCHOOL && !school) || (Q_DAYS[q] == QD_FREE && school) || (Q_DAYS[q] == QD_WEEKEND && wday < 5) ||
        (Q_DAYS[q] == QD_SUNDAY && wday != 6) || (g.s.q_off >> q & 1)) return false;
    // on the routine day's axis, [wake, wake + 1440): a window before the wake-up is after the next midnight
    if (*to <= wake) { *from += 1440; *to += 1440; }
    if (*from < wake) *from = wake;
    if (*to > wake + 1440) *to = wake + 1440;
    return *from < *to;
}
static int q_group(int q) { return Q_WHEN[q] == QW_PM_LAST ? QW_PM : Q_WHEN[q]; }

// The minute question q is first due today: the questions of the morning (or the evening) come in an order
// shuffled by the day, at random moments at least Q_GAP_MIN apart, the last-of-the-evening one last; the
// others at their time. Worked out from the day alone, so a reboot gets the same plan.
static int q_plan(const Game& g, int q, u32 day) {
    int from, to;
    if (!q_window(g, q, (int)day, &from, &to)) return -1;
    int grp = q_group(q);
    if (grp == QW_AT) return from;
    if (grp == QW_OPEN) return from + (int)(mix(day * 131u + 7) % (u32)((to - from) / 2 + 1));
    int mem[Q_COUNT], n = 0, a, b;
    u32 key[Q_COUNT];
    for (int k = 0; k < Q_COUNT; k++)
        if (q_group(k) == grp && q_window(g, k, (int)day, &a, &b)) {
            u32 kk = Q_WHEN[k] == QW_PM_LAST ? 0xFFFFFFFFu : mix(day * 977u + (u32)k) >> 1;
            int j = n++;
            for (; j > 0 && key[j - 1] > kk; j--) { key[j] = key[j - 1]; mem[j] = mem[j - 1]; }
            key[j] = kk; mem[j] = k;
        }
    int start = from + (grp == QW_AM ? 3 : 0);   // the morning hello comes first
    int slack = to - 1 - start - (n - 1) * Q_GAP_MIN;
    if (slack < 0) slack = 0;
    int off[Q_COUNT];
    for (int i = 0; i < n; i++) {               // n sorted random offsets: random moments, the gaps kept
        int o = (int)(mix(day * 31u + (u32)(i * 7 + grp)) % (u32)(slack + 1)), j = i;
        for (; j > 0 && off[j - 1] > o; j--) off[j] = off[j - 1];
        off[j] = o;
    }
    for (int i = 0; i < n; i++) if (mem[i] == q) return start + off[i] + i * Q_GAP_MIN;
    return -1;
}

void Game::routine_day(int day) {          // a new day: fresh questions; the week's stars move along
    u32 gap = s.r_day && day > s.r_day ? (u32)(day - s.r_day) : 7;   // the clock went back, or a first day: all
    for (u32 k = 1; k <= gap && k <= 7; k++) s.week[(s.r_day + k) % 7] = 0;
    s.r_day = (u16)day;
    s.r_asked = s.r_done = 0;
    for (int i = 0; i < Q_COUNT; i++) { set_ans(i, 0); r_first[i] = 0; }
    r_last = 0;
    r_waiting = 0;
    want_save = 1;
}

// now: the absolute minute (spacing, retries); day and m: the routine day and its minute (the windows)
int Game::question_due(u32 now, int day, int m) {
    if (r_last && now < r_last + Q_GAP_MIN) return -1;   // (a clock set back waits too: never two at once)
    for (int q = 0; q < Q_COUNT; q++) {
        u16 bit = (u16)(1 << q);
        int from, to;
        if ((s.r_done & bit) || !q_window(*this, q, day, &from, &to)) continue;
        if (r_waiting & bit) to += Q_LATE_MIN;   // it came due while she was busy: its window stays open longer
        if (s.r_asked & bit) {             // one more try, ten minutes after the first
            if (!r_first[q]) r_first[q] = now;   // asked before a reboot: ten minutes from now
            if (now >= r_first[q] + Q_REASK_MIN && m < to + Q_REASK_MIN) return q;
            continue;
        }
        int at = q_plan(*this, q, (u32)day);
        bool last = Q_WHEN[q] == QW_PM_LAST;
        if (m < at || m >= to + (last ? Q_REASK_MIN + Q_GAP_MIN : 0)) continue;   // (late: after the others' retries)
        if (last) {                        // the last of the evening waits while another may still come, retries too
            bool others = false;
            for (int k = 0; k < Q_COUNT; k++) {
                int a, b;
                others |= k != q && q_group(k) == QW_PM && !(s.r_done & (1 << k)) && q_window(*this, k, day, &a, &b) &&
                          m < b + ((s.r_asked >> k & 1) ? Q_REASK_MIN : 0) + ((r_waiting >> k & 1) ? Q_LATE_MIN : 0);
            }
            if (others) continue;
        }
        return q;
    }
    return -1;
}

void Game::check_routine() {
    routine_check();
    Cal c = today();
    int m, day = rday_now(&m);
    if (!routines_on() || day < 0) return;
    if (day != s.r_day) {
        if (day + 1 == s.r_day) return;    // the clock went back across a wake-up: wait, nothing is asked twice
        routine_day(day);
    }
    if (quiet()) return;                   // muted, or bedtime: nothing is asked, late or not
    u32 now = c.day * 1440u + c.minute;
    // Home, on the lounger or out with her friends (she pops in to ask, then goes back out). In a game,
    // a menu or the settings she waits: a question that comes due then is kept for later (r_waiting).
    if ((scene != SC_HOME && scene != SC_LOUNGE && scene != SC_AWAY) || (scene == SC_AWAY && scene_ms < 5000) ||
        party || greeting || memory_new || petting) {   // (not the moment she has said goodbye)
        if (scene != SC_LEAVE && scene != SC_ASK) { int q = question_due(now, day, m); if (q >= 0) r_waiting |= 1u << q; }
        return;
    }
    int wake = wake_on(day);
    if (!(s.r_done & GREET_BIT) && m >= wake && m < wake + 60) {   // good morning, with her name
        s.r_done |= GREET_BIT;
        want_save = 1;
        if (!screen_on) { screen_on = 1; idle_ms = dark_after(R_DARK_LINGER_MS + GREET_MS); }   // briefly
        say_clip(R_GREET, BAG_GREET);
        if (scene == SC_HOME) greet();
        return;
    }
    int q = question_due(now, day, m);
    if (q >= 0) ask(q);
    else if (scene == SC_HOME || scene == SC_LOUNGE) now_and_then(day, m, free_on(day));
}

// Now and then, never on the clock: one suggestion a day (coloring, Lego, a book, her dolls) in her free afternoon,
// and "how are you?" at most twice in any 7 days; both only at home (the platform looks for a known network, at most
// HOME_PROBES times a day), never right before a set-time question or right after another one.
bool Game::now_and_then(int day, int m, bool free) {
    Cal c = today();
    u32 now = c.day * 1440u + c.minute;
    if (r_last && now < r_last + Q_GAP_MIN) return false;
    int bed = clock_min(T_BED);
    if (bed < wake_on(day)) bed += 1440;
    int from = free ? 10 * 60 : 15 * 60, to = bed - 90 < 18 * 60 ? bed - 90 : 18 * 60;   // done before the evening ones
    if (to - from < 60 || m < from || m >= to) return false;
    for (int k = 0; k < Q_COUNT; k++) {    // the half hour before a set-time question (sport, water, dinner) stays clear
        int a, b;
        if (Q_WHEN[k] == QW_AT && !(s.r_done >> k & 1) && q_window(*this, k, day, &a, &b) && m >= a - 30 && m < b) return false;
    }
    int pick = -1, span = to - from - 30;
    if (s.suggest_day != day && m >= from + (int)(mix((u32)day * 613u + 5) % (u32)span)) {
        int cand[ACT_COUNT], n = 0;
        for (int k = 0; k < Q_COUNT; k++) if (q_activity(k) && !(s.q_off >> k & 1)) cand[n++] = k;
        if (n) pick = cand[mix((u32)day * 29u + 1) % (u32)n];
    }
    bool chat_ok = s.chat_days[0] != day && (!s.chat_days[1] || day >= s.chat_days[1] + 7) && !(s.q_off >> Q_HOWAREYOU & 1);
    if (pick < 0 && chat_ok && mix((u32)day * 977u + 3) % 7 < 3 && m >= from + (int)(mix((u32)day * 331u + 9) % (u32)span))
        pick = Q_HOWAREYOU;                // about three days in seven, and the 7-day cap keeps it to two
    if (pick < 0) return false;
    if (!home_at || clock_ms - home_at >= HOME_FRESH_MS) {   // at home? ask the platform (and try again later)
        if (home_probe_day != day) { home_probe_day = (u16)day; home_probes = 0; }
        if (!home_req && home_probes < HOME_PROBES && (!home_req_at || clock_ms - home_req_at >= 30 * 60000u)) {
            home_req = 1; home_req_at = clock_ms ? clock_ms : 1; home_probes++;
        }
        return false;
    }
    if (pick == Q_HOWAREYOU) { s.chat_days[1] = s.chat_days[0]; s.chat_days[0] = (u16)day; }
    else s.suggest_day = (u16)day;
    ask(pick);
    s.r_done |= 1u << pick;                // one try: it is not on the clock
    return true;
}

// The one rule for a question under way: it goes on only while she may still ask it (routines on, that
// question on, not muted or at bedtime, a trusted clock, the same routine day). Checked every step, on an
// answer and after a page config; otherwise it ends, with its listening and her routine speech.
bool Game::question_may_continue() const {
    return routines_on() && !quiet() && !(s.q_off >> r_q & 1) && rday_now() >= 0 && rday_now() == s.r_day;
}
void Game::routine_check() {
    if (scene == SC_ASK ? !question_may_continue() : (rclip || r_said) && (!routines_on() || quiet() || rday_now() < 0))
        routine_cancel();
}

// One of the 10 variants of a list, never the same twice until all ten were said (a shuffle bag).
void Game::say_clip(int first, int list) {
    u16 used = bag[list];
    if ((used & 0x3FF) == 0x3FF) used = 0;   // all ten said: a new round, but not the last one first
    int pool[R_VARIANTS], n = 0;
    for (int i = 0; i < R_VARIANTS; i++)
        if (!(used >> i & 1) && !(used == 0 && bag_last[list] == i + 1)) pool[n++] = i;
    int v = pool[crandom() % (u32)n];
    bag[list] = (u16)(used | 1 << v);
    bag_last[list] = (u8)(v + 1);
    rclip = (u16)(first + v + 1);
    sfx = SFX_NONE;
    voice = V_NONE;
    voice_at = stamp();                    // her other lines keep their distance
    r_said = stamp();
}

static int bag_of(const u16* firsts, int q, int base) {   // a list two questions share has one bag
    int k = 0;
    while (firsts[k] != firsts[q]) k++;
    return base + k;
}

void Game::ask(int q) {
    u32 bit = 1u << q;
    Cal c = today();
    u32 now = c.day * 1440u + c.minute;
    if (s.r_asked & bit) s.r_done |= bit;  // the second try is the last
    else { s.r_asked |= bit; r_first[q] = now; }
    r_last = now;
    want_save = 1;
    r_back = scene;
    r_was_dark = !screen_on;
    screen_on = 1;
    idle_ms = 0;
    pet_x = 0;
    facing = 1;
    go(SC_ASK);
    r_q = (u8)q;
    r_stage = RS_ASK;
    r_at = stamp();
    r_sel = 0;
    say_clip(Q_ASK[q], q);
    listen_req = LREQ_LISTEN;              // the platform listens once she has finished asking
    listen_kind = q_open(q) ? LK_OPEN : LK_YESNO;
    listen_q = (u8)q;
}

void Game::listen_started() {
    if (scene == SC_ASK && r_stage == RS_ASK) { r_stage = RS_LISTEN; dirty = 1; }   // (the time limit runs on)
}

void Game::listen_result(int a) {
    if (scene != SC_ASK || r_stage == RS_REPLY) return;
    if (!question_may_continue()) { routine_cancel(); return; }
    if (a == ANS_NO_LISTENER) {            // nobody to understand her: the child answers with the buttons
        if (r_stage != RS_BUTTONS) { r_stage = RS_BUTTONS; r_at = stamp(); r_sel = 0; dirty = 1; }
        return;
    }
    if (a < ANS_YES || a > ANS_TAP_NO) return;
    int q = r_q;
    u32 bit = 1u << q;
    Cal c = today();
    if (c.known) set_ans(q, (u16)(a << 11 | c.minute));
    bool yes = a == ANS_YES || a == ANS_TAP_YES || (q_open(q) && a == ANS_SAID);
    if (q_open(q)) {                       // any answer is a good one; silence gets a kind word too
        s.r_done |= bit;
        say_clip(yes ? Q_YES[q] : Q_NO[q], yes ? (q == Q_HAPPY ? BAG_REPLY : bag_of(Q_YES, q, BAG_YES))
                                               : (q == Q_HAPPY ? BAG_SILENT : bag_of(Q_NO, q, BAG_NO)));
        if (yes) burst(P_HEART, SCREEN_W / 2, 150, 4);
    } else if (yes) {                      // a star for today (the habits), hearts for the rest
        s.r_done |= bit;
        say_clip(Q_YES[q], bag_of(Q_YES, q, BAG_YES));
        if (q_star(q)) {
            s.week[s.r_day % 7] |= (u16)bit;
            burst(P_STAR, SCREEN_W / 2, 150, 6);
            burst(P_CONFETTI, SCREEN_W / 2, 150, 4);
        } else burst(P_HEART, SCREEN_W / 2, 150, 4);
        r_act = (u8)q_activity(q);         // a yes to a suggestion: off she goes to do it too, once her reply is said
    } else if (a == ANS_NO || a == ANS_TAP_NO) {
        s.r_done |= bit;
        say_clip(Q_NO[q], bag_of(Q_NO, q, BAG_NO));
    } else {
        say_clip(R_UNCLEAR, BAG_UNCLEAR);  // she asks once more later (unless that was the second try)
    }
    if (r_stage <= RS_LISTEN) listen_req = LREQ_NONE;   // an answer that arrived ends any request still waiting
    r_stage = RS_REPLY;
    r_at = stamp();
    want_save = 1;
    dirty = 1;
}

// She may not go on (routine_check): the question ends without a word (its second try may still come), no
// routine clip is said, and LREQ_CANCEL tells the platform to stop listening and any routine clip it plays.
void Game::routine_cancel() {
    bool speaking = rclip || (r_said && since(r_said) < R_CLIP_MAX_MS);
    rclip = 0;
    r_said = 0;
    if (scene == SC_ASK || speaking) listen_req = LREQ_CANCEL;
    if (scene != SC_ASK) return;
    routine_done();
}

// Clean from her own bath she sometimes asks the child about theirs: one try, at most once a day and twice in
// any 7 days, never at bedtime or straight after another question.
void Game::bath_question() {
    int day = rday_now();
    if (!routines_on() || quiet() || day < 0 || day != s.r_day || (s.q_off >> Q_BATH & 1) || (s.r_asked >> Q_BATH & 1) ||
        party || greeting || memory_new || petting) return;
    Cal c = today();
    u32 now = c.day * 1440u + c.minute;
    if (r_last && now < r_last + Q_GAP_MIN) return;
    if (s.bath_days[1] && c.day < (u32)s.bath_days[1] + 7) return;   // twice this week already
    if (crandom() % 2) return;             // now and then, not after every bath
    s.bath_days[1] = s.bath_days[0];
    s.bath_days[0] = (u16)c.day;
    ask(Q_BATH);
    s.r_done |= 1 << Q_BATH;               // no second try: it is not on the clock
}

void Game::routine_done() {
    if (r_act) {                           // she said yes to coloring, Lego, a book or her dolls: so does the unicorn,
        s.away_act = r_act;                // and her break shows her at it (the child goes off to play too)
        r_act = 0;
        start_leave();
        return;
    }
    if (r_back == SC_AWAY) { sync_away(); if (away_ms) go(SC_AWAY); else come_back(); }   // back out to her friends, or home
    else go(r_back == SC_LOUNGE && plugged ? SC_LOUNGE : SC_HOME);
    if (r_was_dark) idle_ms = dark_after(R_DARK_LINGER_MS);   // she woke the screen: dark again soon
    idle_next = clock_ms + 3000;
}

void Game::routine_input(int type, int x, int y) {   // only the buttons answer; the question plays out
    if (r_stage != RS_BUTTONS) return;
    int btn = -1;
    if (type == IN_TOUCH && y >= ASK_BTN_Y0) btn = x < SCREEN_W / 2 ? 0 : 1;
    else if (type == IN_NEXT) { r_sel ^= 1; dirty = 1; cue(SFX_TAP); return; }
    else if (type == IN_SELECT) btn = r_sel;
    if (btn < 0) return;
    r_sel = (u8)btn;
    listen_result(btn == 0 ? ANS_TAP_YES : ANS_TAP_NO);
}

// Settings > GÜNLÜK RUTİN: the questions on/off, holiday mode, TV test mode, the week's stars.
void Game::routine_page_input(int type, int x, int y) {
    (void)x;
    if (set_page == PG_HABITS) {           // the table: any press goes back
        if (type == IN_TOUCH || type == IN_SELECT || type == IN_NEXT) { set_page = PG_ROUTINE; set_row = RT_HABITS; dirty = 1; cue(SFX_TAP); }
        return;
    }
    int row = set_row;
    if (type == IN_TOUCH) {
        if ((row = set_touch(y)) < 0) return;
        set_row = (u8)row;
    } else if (type == IN_NEXT) {
        set_row = (u8)((set_row + 1) % RT_ROWS);
        dirty = 1;
        return;
    } else if (type != IN_SELECT) {
        return;
    }
    switch (row) {
    case RT_ON:      s.opts ^= OPT_NO_ROUTINES; touched(K_ROUTINES); break;
    case RT_HOLIDAY: s.opts ^= OPT_HOLIDAY; touched(K_HOLIDAY); break;
    case RT_TV:
        s.opts ^= OPT_TV_TEST;
        s.tv_day = tv_on() && today().known ? today().day : 0;   // seven days from now
        touched(K_TV_TEST);
        break;
    case RT_HABITS:  set_page = PG_HABITS; break;
    case RT_BACK:    set_page = PG_MAIN; set_row = SET_ROUTINE; break;
    }
    want_save = 1;
    dirty = 1;
    cue(SFX_TAP);
}

// TV test mode: by day, a ~1 s level check once a minute, on the toy only; a 5 s sample for the listener
// only when the sound lasts (two loud, sustained checks in a row: not a slammed door), at most TV_PER_HOUR
// an hour and none while the same sound simply goes on (TV_SAME_MIN, unless its level changed). It
// switches itself off TV_DAYS after it was switched on.
void Game::check_tv() {
    if (!tv_on()) return;
    Cal c = today();
    if (c.known && !s.tv_day) { s.tv_day = c.day; want_save = 1; }
    if (c.known && c.day >= s.tv_day + TV_DAYS) {
        s.opts &= (u8)~OPT_TV_TEST;
        s.tv_day = 0;
        touched(K_TV_TEST);
        want_save = 1;
        return;
    }
    if (!tv_may_listen()) return;
    if (tv_check_at && since(tv_check_at) < TV_CHECK_MS) return;
    tv_check_at = stamp();
    listen_req = LREQ_LISTEN;
    listen_kind = LK_LEVEL;
}

// The microphone is free and it is the TV test's time: by day, never at bedtime or with an untrusted clock.
bool Game::tv_may_listen() const {
    return today().known && phase() == PH_DAY && !bedtime() && scene != SC_ASK && !listen_req;
}

void Game::level_result(int db, int loud_pct) {
    if (!tv_on()) return;
    if (!tv_may_listen()) { tv_loud = 0; return; }   // a late result: the evening (or a question) came meanwhile
    bool loud = db >= TV_LEVEL_DB && loud_pct >= TV_SUSTAIN_PCT;
    tv_db = (i8)(db < -127 ? -127 : db > 0 ? 0 : db);
    if (!loud) { tv_loud = 0; tv_quiet_since = 1; return; }
    if (++tv_loud < 2) return;             // one loud check is a door, a shout, a dropped toy
    int n = 0, oldest = 0, newest = -1;
    for (int i = 0; i < TV_PER_HOUR; i++) {
        if (tv_samples[i] && since(tv_samples[i]) < 3600000u) n++;
        if (!tv_samples[i] || (tv_samples[oldest] && since(tv_samples[i]) > since(tv_samples[oldest]))) oldest = i;
        if (tv_samples[i] && (newest < 0 || since(tv_samples[i]) < since(tv_samples[newest]))) newest = i;
    }
    if (n >= TV_PER_HOUR) return;
    bool same = newest >= 0 && since(tv_samples[newest]) < TV_SAME_MIN * 60000u && !tv_quiet_since &&
                iabs(tv_db - tv_sample_db) < TV_CHANGE_DB;
    if (same) return;
    tv_samples[oldest] = stamp();
    tv_sample_db = tv_db;
    tv_quiet_since = 0;
    tv_loud = 0;
    listen_req = LREQ_LISTEN;
    listen_kind = LK_TV;
}

// What the radio should do (fw: net.cpp). The WiFi setup owns it while it runs; on USB power the link stays
// up (settings sync, journal, samples, answers without a join); on battery only for her question (the join
// starts while she is still asking) or a TV sample waiting to be sent.
int Game::link(bool sample_waiting) const {
    if (wifi_busy()) return LINK_OFF;
    if (plugged) return LINK_STAY;
    if ((scene == SC_ASK && r_stage <= RS_LISTEN) || sample_waiting) return LINK_NOW;
    return LINK_OFF;
}

// ---- settings sync with the parent page ------------------------------------------------------------
Settings Game::settings() const {
    Settings t;
    for (int k = 0; k < K_COUNT; k++) t.v[k] = 0;
    int nh = 0;
    for (int i = 0; i < HOL_MAX; i++) {
        t.hol[i][0] = t.hol[i][1] = 0;
        if (s.hol[i][0]) { t.hol[nh][0] = s.hol[i][0]; t.hol[nh][1] = s.hol[i][1]; nh++; }
    }
    t.v[K_ROUTINES] = !(s.opts & OPT_NO_ROUTINES);
    u32 on = ~s.q_off & ((1u << Q_COUNT) - 1);
    t.v[K_QUESTIONS] = (u16)on;
    t.v[K_QUESTIONS2] = (u16)(on >> 16);
    t.v[K_WAKE_WEEKDAY] = (u16)clock_min(T_WAKE);
    t.v[K_WAKE_WEEKEND] = (u16)clock_min(T_WAKE_WE);
    t.v[K_BEDTIME] = (u16)clock_min(T_BED);
    t.v[K_HOLIDAY] = (s.opts & OPT_HOLIDAY) != 0;
    t.v[K_HOLIDAY_RANGES] = (u16)nh;
    t.v[K_TV_TEST] = tv_on();
    t.v[K_LANG] = s.lang;
    t.v[K_VOLUME] = (u16)volume();
    t.v[K_BRIGHTNESS] = (u16)brightness();
    t.v[K_PLAY_MIN] = (u16)(play_limit_ms() / 60000);
    t.v[K_BREAK_MIN] = (u16)(break_ms() / 60000);
    t.v[K_CALM] = calm();
    t.v[K_SHAKE] = !(s.opts & OPT_NO_SHAKE);
    t.tv_day = s.tv_day;
    return t;
}

SyncMark Game::sync_mark() const {
    SyncMark m;
    m.dirty = s.cfg_dirty;
    for (int k = 0; k < K_COUNT; k++) m.gen[k] = cfg_gen[k];
    return m;
}

void Game::settings_synced(const SyncMark& sent) {   // the page has these: clear what was not edited since
    u16 was = s.cfg_dirty;
    for (int k = 0; k < K_COUNT; k++)
        if ((sent.dirty >> k & 1) && cfg_gen[k] == sent.gen[k]) s.cfg_dirty &= (u16)~(1 << k);
    if (s.cfg_dirty != was) want_save = 1;   // (on USB power a sync comes every minute: no save for nothing)
}

Settings merge_settings(const Settings& mine, u16 dirty, const Settings& page, u16 present) {
    static const u16 LO[K_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0};
    static const u16 HI[K_COUNT] = {1, 0xFFFF, 1439, 1439, 1439, 1, HOL_MAX, 1, LANG_COUNT - 1, VOL_OFF,
                                    255, 240, 240, 1, 1, (1 << (Q_COUNT - 16)) - 1};
    Settings out = mine;
    for (int k = 0; k < K_COUNT; k++) {
        if (!(present >> k & 1) || (dirty >> k & 1) || page.v[k] < LO[k] || page.v[k] > HI[k]) continue;
        if (k == K_HOLIDAY_RANGES) {
            bool ok = true;
            for (int i = 0; i < page.v[k]; i++) ok &= page.hol[i][0] > 0 && page.hol[i][0] <= page.hol[i][1];
            if (!ok) continue;
            for (int i = 0; i < HOL_MAX; i++) {
                out.hol[i][0] = i < page.v[k] ? page.hol[i][0] : 0;
                out.hol[i][1] = i < page.v[k] ? page.hol[i][1] : 0;
            }
        }
        if (k == K_TV_TEST) out.tv_day = page.tv_day;
        out.v[k] = page.v[k];
    }
    return out;
}

// A page config newer than (well, other than) the one the settings are based on: the toy's own changes
// since the last sync keep their value (the person holding the toy is right there).
void Game::apply_settings(u32 rev, const Settings& page, u16 present) {
    if (rev == s.cfg_rev) return;
    Settings m = merge_settings(settings(), s.cfg_dirty, page, present);
    auto opt = [&](u8 bit, bool on) { s.opts = (u8)(on ? s.opts | bit : s.opts & ~bit); };
    opt(OPT_NO_ROUTINES, !m.v[K_ROUTINES]);
    s.q_off = ~((u32)m.v[K_QUESTIONS] | (u32)m.v[K_QUESTIONS2] << 16) & ((1u << Q_COUNT) - 1);
    set_clock_min(T_WAKE, m.v[K_WAKE_WEEKDAY]);
    set_clock_min(T_WAKE_WE, m.v[K_WAKE_WEEKEND]);
    set_clock_min(T_BED, m.v[K_BEDTIME]);
    opt(OPT_HOLIDAY, m.v[K_HOLIDAY]);
    for (int i = 0; i < HOL_MAX; i++) { s.hol[i][0] = m.hol[i][0]; s.hol[i][1] = m.hol[i][1]; }
    opt(OPT_TV_TEST, m.v[K_TV_TEST]);
    // the seven days count from the day it was switched on (the page's date, even if it arrives late)
    Cal c = today();
    s.tv_day = !tv_on() ? 0 : m.tv_day && (!c.known || m.tv_day <= c.day) ? m.tv_day : c.known ? c.day : 0;
    s.lang = (u8)m.v[K_LANG];
    s.volume = (u8)m.v[K_VOLUME];
    s.muted = 0;
    s.bright = (u8)m.v[K_BRIGHTNESS];
    s.limit_min = (u8)m.v[K_PLAY_MIN];
    s.break_min = (u8)m.v[K_BREAK_MIN];
    opt(OPT_CALM, m.v[K_CALM]);
    opt(OPT_NO_SHAKE, !m.v[K_SHAKE]);
    routine_check();                       // switched off, muted, that question off or bedtime from the page
    s.cfg_rev = rev;
    want_save = 1;
    dirty = 1;
}

// The name pack's version of a line (Turkish only, the platform checks the language and the pack).
int name_line(int sfx, int line) {
    switch (line) {
    case V_AWAKE:   return NL_AWAKE;
    case V_MORNING: return NL_MORNING;
    case V_MISSED:  return NL_MISSED;
    case V_HELLO:   return sfx == SFX_HELLO ? NL_BACK : NL_HELLO;   // back from her friends, or just hello
    default:        return -1;
    }
}

}  // namespace up
