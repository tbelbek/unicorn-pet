"""Design showcase: characters, moods, egg stage and on-device game screens.

Run:  uv run --with pillow python tools/showcase.py
Out:  design/showcase.png, design/screen_<name>.png, design/idle_pamuk.gif
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageColor

import sprites as S
import front as F

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "design"
FONT = str(ROOT / "assets/fonts/PressStart2P-Regular.ttf")
BG = "#000000"                      # AMOLED: black pixels are off, so black is the battery-saving canvas
INK = "#F4EEFF"
DIM = "#A99BD6"


def font(px):                        # Press Start 2P is crisp only at multiples of 8
    return ImageFont.truetype(FONT, px)


# 9x8 icons. Letters map to palette entries below; '.' is transparent.
ICONS = {
    "apple": ["....g....", "...g.....", ".rrr.rrr.", "rrrrrrrrr", "rwrrrrrrr",
              "rrrrrrrrr", ".rrrrrrr.", "..rr.rr.."],
    "heart": [".hh...hh.", "hhhh.hhhh", "hwhhhhhhh", "hhhhhhhhh", ".hhhhhhh.",
              "..hhhhh..", "...hhh...", "....h...."],
    "star":  ["....y....", "...yyy...", "...yyy...", "yyyyyyyyy", ".yyyyyyy.",
              "..yyyyy..", ".yyy.yyy.", ".yy...yy."],
    "bubble": ["..bbbb...", ".b....b..", "b.w....b.", "b.w....b.", "b......b.",
               ".b....b..", "..bbbb.bb", ".......bb"],
    "moon":  ["..mmmm...", ".mmm.....", "mmm......", "mmm......", "mmm......",
              "mmmm...m.", ".mmmmmmm.", "..mmmmm.."],
    "ball":  ["..ppppp..", ".pwppppp.", "ppppppppp", "yyyyyyyyy", "ppppppppp",
              "ppppppppp", ".ppppppp.", "..ppppp.."],
    "strawberry": ["...ggg...", "..ggggg..", ".rrrrrrr.", ".ryrrryr.", ".rrrrrrr.",
                   "..ryrrr..", "...rrr...", "....r...."],
    "carrot": ["......gg.", ".....g.g.", "....ooo..", "...oooo..", "..oooo...",
               ".oooo....", ".oo......", "o........"],
    "hand":  ["...nn....", "..nkkn...", "..nkkn...", "..nkknnn.", "nnnkkkkkn",
              "nkkkkkkn.", ".nkkkkn..", "..nnnn..."],
    "note":  ["...hhhhhh", "...h....h", "...h....h", "...h....h", ".hhh..hhh",
              "hhhh.hhhh", "hhh..hhh.", "........."],
    "cloud": [".........", "...www...", ".wwwww...", "wwwwwwww.", "wwwwwwwww",
              ".wwwwwww.", ".........", "........."],
    "mirror": ["..mmmmm..", ".mwwwwwm.", ".mwbwwwm.", ".mwwwwwm.", "..mmmmm..",
               "....m....", "....m....", "....m...."],
    "house": ["....r....", "...rrr...", "..rrrrr..", ".rrrrrrr.", "..wwwww..",
              "..ww.ww..", "..ww.ww..", "..ww.ww.."],
    "bow":   ["hh.....hh", "hhh...hhh", "hhhh.hhhh", "hhhhwhhhh", "hhhh.hhhh",
              "hhh...hhh", "hh.....hh", "........."],
    "flower": ["..hh.hh..", ".hhhhhhh.", "..hyyyh..", ".hhyyyhh.", "..hyyyh..",
               ".hhhhhhh.", "..hh.hh..", "........."],
    "tiara": ["....y....", "y...y...y", "yy.yhy.yy", "yyyyyyyyy", "ypyyhyypy",
              "yyyyyyyyy", ".........", "........."],
}
ICON_COL = {"g": "#5EE07A", "r": "#FF5C6E", "w": "#FFFFFF", "h": "#FF5C9C", "y": "#FFD84D",
            "b": "#6FD6FF", "m": "#C9B8FF", "p": "#4CC9F0", "o": "#FF9F43", "k": "#FFE3CC", "n": "#B48AE0"}


def icon(name, scale):
    rows = ICONS[name]
    img = Image.new("RGBA", (len(rows[0]), len(rows)), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                img.putpixel((x, y), ImageColor.getrgb(ICON_COL[ch]) + (255,))
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)


def sprite(key, mood="idle", frame=0, scale=6, pose="stand"):
    cells = S.with_props(S.build(S.PALETTES[key], mood, frame, pose), pose, frame)
    return S.render(cells, scale)


NIGHT = (34, 44, 120)


def night(img, amount=.34, dim=.9):
    """Night colours: blend toward deep blue and dim, keep transparency."""
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = tuple(int((c * (1 - amount) + n * amount) * dim)
                                 for c, n in zip((r, g, b), NIGHT)) + (a,)
    return img


def sky(d, w, h, seed=7):
    """Sparse pixel stars and a crescent moon for the sleep scene."""
    import random
    rnd = random.Random(seed)
    for _ in range(26):
        x, y = rnd.randrange(8, w - 8), rnd.randrange(96, 300)
        s = rnd.choice((2, 2, 3))
        d.rectangle([x, y, x + s - 1, y + s - 1], fill=rnd.choice(("#5A5F9E", "#8C90D6", "#C9CCFF")))
    mx, my = w - 86, 104
    d.ellipse([mx, my, mx + 44, my + 44], fill="#FFE9A8")
    d.ellipse([mx + 14, my - 6, mx + 56, my + 36], fill=BG)


def egg(scale, crack=True, wobble=0):
    """Unicorn egg: cream shell, rainbow spots, a crack showing it's about to hatch."""
    w, h = 20, 24
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cells = {}
    for y in range(h):
        for x in range(w):
            # egg = narrower on top
            ry = 10.5
            rx = 7.5 if y > 11 else 7.5 - (11 - y) * .18
            if ((x + .5 - 10) / rx) ** 2 + ((y + .5 - 12.5) / ry) ** 2 <= 1:
                cells[(x, y)] = "#FFF4E2" if (x + y) % 9 else "#FFF9EF"
    for (x, y) in list(cells):                      # shade lower-right rim
        if (x + 1, y) not in cells or (x, y + 1) not in cells:
            cells[(x, y)] = "#F2D7C2"
    for (cx, cy), col in [((7, 8), "#FF5C8A"), ((12, 12), "#4CC9F0"), ((8, 16), "#FFE066"),
                          ((13, 18), "#9B6BFF"), ((11, 5), "#5EE07A")]:
        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            cells[(cx + dx, cy + dy)] = col
    if crack:
        for x, y in [(5, 10), (6, 11), (7, 10), (8, 11), (9, 10), (10, 11)]:   # crack
            cells[(x, y)] = "#7A4E9E"
    if wobble:
        cells = {(x + wobble, y): col for (x, y), col in cells.items() if 0 <= x + wobble < w}
    for (x, y) in list(cells):
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if n not in cells and 0 <= n[0] < w and 0 <= n[1] < h:
                cells[n] = "#5B2A86"
    for (x, y), c in cells.items():
        img.putpixel((x, y), ImageColor.getrgb(c) + (255,))
    return img.resize((w * scale, h * scale), Image.NEAREST)


def zz(d, x, y):
    d.text((x, y), "Z", font=font(24), fill="#C9B8FF")
    d.text((x + 26, y - 20), "z", font=font(16), fill="#A99BD6")


def sparkle(d, x, y, s, col="#FFE066"):
    """Four-point pixel sparkle."""
    for i in range(-2, 3):
        d.rectangle([x + i * s, y, x + i * s + s - 1, y + s - 1], fill=col)
        d.rectangle([x, y + i * s, x + s - 1, y + i * s + s - 1], fill=col)


def thought(img, x, y, scale):
    """Thought bubble holding an apple: 'I'm hungry'."""
    d = ImageDraw.Draw(img)
    r = 15 * scale // 3
    d.ellipse([x, y, x + 2 * r, y + 2 * r], fill="#F4EEFF")
    d.ellipse([x - 10, y + 2 * r - 4, x + 2, y + 2 * r + 8], fill="#F4EEFF")
    d.ellipse([x - 20, y + 2 * r + 12, x - 14, y + 2 * r + 18], fill="#F4EEFF")
    a = icon("apple", scale)
    img.alpha_composite(a, (x + r - a.width // 2, y + r - a.height // 2))


def meter(d, x, y, value, col):
    for i in range(5):
        fill = col if i < value else "#2A2440"
        d.rectangle([x + i * 12, y, x + i * 12 + 8, y + 8], fill=fill)


def screen(key, mood="idle", stats=(4, 3, 5, 2), selected=0, age=3, frame=0, style="side",
           pose="stand", subtitle=None):
    """One frame of the game at the 1.8" AMOLED's native 368x448."""
    W, H = 368, 448
    img = Image.new("RGBA", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.fontmode = "1"
    p = S.PALETTES[key]
    # status row: hunger, happiness, energy, cleanliness
    cols = ["#FF5C6E", "#FF5C9C", "#FFD84D", "#6FD6FF"]
    for i, (ic, v) in enumerate(zip(["apple", "heart", "star", "bubble"], stats)):
        x = 14 + i * 88
        img.alpha_composite(icon(ic, 2), (x, 16))
        meter(d, x + 22, 20, v, cols[i])
    # name + age
    name = p["name"].upper()
    d.text((W // 2, 52), name, font=font(16), fill=p["mane"][len(p["mane"]) // 2], anchor="mt")
    d.text((W // 2, 74), subtitle or f"{age}. GÜN", font=font(8), fill=DIM, anchor="mt")
    is_night = pose == "sleep"
    if is_night:
        sky(d, W, H)
    # ground: a soft pixel cloud the unicorn stands on
    ground = [(120, "#10142E"), (96, "#161B3D")] if is_night else [(120, "#1B1530"), (96, "#251D42")]
    for i, (rx, col) in enumerate(ground):
        d.ellipse([W // 2 - rx, 330 - i * 2, W // 2 + rx, 352 - i * 2], fill=col)
    if style == "front":
        spr = F.sprite(key, mood, frame, 5)
        sx, sy = (W - spr.width) // 2, 344 - spr.height
    else:
        spr = sprite(key, mood, frame, 6, pose)
        if is_night:
            spr = night(spr)
        sx, sy = (W - spr.width) // 2 + 6, 346 - S.FEET * 6
    img.alpha_composite(spr, (sx, sy))
    if mood == "happy":
        sparkle(d, sx + 34, sy + 40, 4); sparkle(d, sx + 230, sy + 30, 3, "#FF8FB8")
    if mood == "sleepy" or is_night:
        zz(d, sx + 220, sy + (110 if is_night else 20) - 12 * frame)
    if mood == "hungry":
        thought(img, sx + 196, sy - 6, 4)
    if pose == "play":
        sparkle(d, sx + 40, sy + 60, 4); sparkle(d, sx + 210, sy + 120, 3, "#FF8FB8")
    # menu: feed, play, sleep, wash
    items = [("apple", "BESLE"), ("ball", "OYNA"), ("moon", "UYUT"), ("bubble", "YIKA")]
    for i, (ic, label) in enumerate(items):
        cx = 46 + i * 92
        if i == selected:
            d.rounded_rectangle([cx - 36, 368, cx + 36, 434], radius=12, fill="#241C40",
                                outline=p["mane"][2], width=2)
        ico = icon(ic, 3)
        img.alpha_composite(ico, (cx - ico.width // 2, 378))
        d.text((cx, 414), label, font=font(8), fill=INK if i == selected else DIM, anchor="mt")
    return img


def device(scr):
    """Place a screen inside a rounded bezel, like the Waveshare case."""
    pad = 26
    img = Image.new("RGBA", (scr.width + 2 * pad, scr.height + 2 * pad), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, img.width - 1, img.height - 1], radius=58, fill="#1A1A1F",
                        outline="#3A3A44", width=3)
    mask = Image.new("L", scr.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, scr.width - 1, scr.height - 1], radius=36, fill=255)
    img.paste(scr, (pad, pad), mask)
    return img


def label(d, x, y, text, size=16, fill=INK, anchor="mt"):
    d.text((x, y), text, font=font(size), fill=fill, anchor=anchor)


def decorate(d, img, mood, x, y, w):
    """Mood props placed relative to a sprite's top-left and width."""
    if mood == "happy":
        sparkle(d, x + w // 12, y + 34, 4); sparkle(d, x + w - 14, y + 24, 3, "#FF8FB8")
    if mood == "sleepy":
        zz(d, x + w - 40, y + 6)
    if mood == "hungry":
        thought(img, x + w - 52, y - 22, 4)


def build_showcase():
    W = 1880
    sheet = Image.new("RGBA", (W, 2600), "#0B0816")
    d = ImageDraw.Draw(sheet)
    d.fontmode = "1"
    label(d, W // 2, 48, "UNICORN PET", 32, "#FFE066")
    label(d, W // 2, 96, "Karakter tasarımları · 1.8\" AMOLED için piksel sanat", 16, DIM)
    keys = list(S.PALETTES)
    col_x = [W // 8 + i * W // 4 for i in range(4)]

    def name_row(y, scale_note=None):
        for cx, key in zip(col_x, keys):
            p = S.PALETTES[key]
            label(d, cx, y, p["name"].upper(), 16, p["mane"][4 if key == "gece" else 2])

    # 1. side view
    label(d, 60, 160, "1 · YANDAN GÖRÜNÜŞ", 16, INK, "lt")
    for cx, key in zip(col_x, keys):
        spr = sprite(key, "idle", 0, 6)
        sheet.alpha_composite(spr, (cx - spr.width // 2, 196))
    name_row(462)
    for cx, key in zip(col_x, keys):
        for j, c in enumerate(S.PALETTES[key]["mane"]):
            d.rectangle([cx - 72 + j * 24, 492, cx - 72 + j * 24 + 19, 511], fill=c)

    # 2. front view
    label(d, 60, 570, "2 · ÖNDEN GÖRÜNÜŞ (SADECE YÜZ)", 16, INK, "lt")
    for cx, key in zip(col_x, keys):
        spr = F.sprite(key, "idle", 0, 5)
        sheet.alpha_composite(spr, (cx - spr.width // 2, 610))
    name_row(878)

    # 3. moods, both styles
    label(d, 60, 950, "3 · RUH HÂLLERİ", 16, INK, "lt")
    moods = [("idle", "NORMAL"), ("happy", "MUTLU"), ("hungry", "AÇ"), ("sleepy", "UYKULU")]
    for cx, (m, name) in zip(col_x, moods):
        side = sprite("pamuk", m, 0, 4)
        fr = F.sprite("seker", m, 0, 4)
        x1 = cx - side.width - 6
        x2 = cx + 10
        sheet.alpha_composite(side, (x1, 1030))
        sheet.alpha_composite(fr, (x2, 1010))
        decorate(d, sheet, m, x2, 1010, fr.width)
        label(d, cx, 1236, name, 16, DIM)

    # 4. egg + idle animation
    label(d, 60, 1310, "4 · YUMURTADAN ÇIKIŞ VE NEFES ANİMASYONU", 16, INK, "lt")
    e = egg(8)
    sheet.alpha_composite(e, (150, 1360))
    label(d, 230, 1570, "YUMURTA", 16, DIM)
    d.polygon([(380, 1450), (420, 1470), (380, 1490)], fill="#FFE066")
    for i in range(2):
        spr = sprite("pamuk", "idle", i, 5)
        sheet.alpha_composite(spr, (460 + i * 230, 1360))
        label(d, 460 + i * 230 + spr.width // 2, 1580, f"KARE {i + 1}", 16, DIM)
    for i in range(2):
        spr = F.sprite("pamuk", "idle", i, 4)
        sheet.alpha_composite(spr, (1010 + i * 230, 1370))
        label(d, 1010 + i * 230 + spr.width // 2, 1580, f"KARE {i + 1}", 16, DIM)
    label(d, 1450, 1430, "Boştayken iki kare", 16, INK, "lt")
    label(d, 1450, 1462, "döner: unicorn 1 piksel", 16, DIM, "lt")
    label(d, 1450, 1494, "iner, nefes alıyor", 16, DIM, "lt")
    label(d, 1450, 1526, "gibi görünür.", 16, DIM, "lt")

    # 5. on-device screens
    label(d, 60, 1650, "5 · OYUN EKRANI (368×448, GERÇEK ÇÖZÜNÜRLÜK)", 16, INK, "lt")
    shots = [screen("pamuk", "happy", (4, 5, 4, 3), 1, 3),
             screen("gece", "sleepy", (3, 4, 1, 4), 2, 12),
             screen("seker", "hungry", (1, 3, 4, 4), 0, 7, style="front"),
             screen("deniz", "idle", (4, 4, 3, 2), 3, 5, style="front")]
    for i, sc in enumerate(shots):
        dv = device(sc)
        sheet.alpha_composite(dv, (30 + i * 462, 1700))
    label(d, 30 + 462, 2230, "YANDAN", 16, DIM)
    label(d, 30 + 3 * 462, 2230, "ÖNDEN", 16, DIM)
    return sheet.crop((0, 0, W, 2290)), shots


EVENTS = [("sleep", "UYU", "UYUT", 2, "Yere yatar, kıvrılır;\nekran gece renklerine döner."),
          ("eat", "YE", "BESLE", 0, "Başını kaba eğer;\nkare kare yemek azalır."),
          ("play", "OYNA", "OYNA", 1, "Topa zıplar; yele ve\nkuyruk rüzgârda uçuşur."),
          ("wash", "YIKAN", "YIKA", 3, "Köpükler yükselir,\nunicorn silkelenir.")]


def build_events():
    W = 1880
    sheet = Image.new("RGBA", (W, 2080), "#0B0816")
    d = ImageDraw.Draw(sheet)
    d.fontmode = "1"
    label(d, W // 2, 48, "UNICORN PET · OLAYLAR", 32, "#FFE066")
    label(d, W // 2, 96, "Her olayda tüm duruş değişir; renkler buna eşlik eder", 16, DIM)
    keys = list(S.PALETTES)
    col_x = [420 + i * 380 for i in range(4)]
    for cx, key in zip(col_x, keys):
        p = S.PALETTES[key]
        label(d, cx, 150, p["name"].upper(), 16, p["mane"][4 if key == "gece" else 2])
    for r, (pose, title, _, _, desc) in enumerate(EVENTS):
        y = 190 + r * 290
        if r:
            d.line([60, y - 14, W - 60, y - 14], fill="#1E1834", width=2)
        label(d, 60, y + 70, title, 32, INK, "lt")
        for j, line in enumerate(desc.split("\n")):
            label(d, 60, y + 124 + j * 18, line, 8, "#C9BFF0", "lt")
        for cx, key in zip(col_x, keys):
            spr = sprite(key, frame=0, scale=5, pose=pose)
            if pose == "sleep":
                tile = Image.new("RGBA", (260, 250), (0, 0, 0, 0))
                ImageDraw.Draw(tile).rounded_rectangle([0, 0, 259, 249], radius=16, fill="#070A1C")
                sheet.alpha_composite(tile, (cx - 130, y))
                spr = night(spr)
            sheet.alpha_composite(spr, (cx - spr.width // 2, y + 8))
            if pose == "sleep":
                zz(d, cx + 66, y + 70)
    top = 190 + 4 * 290 + 20
    label(d, 60, top, "OYUN EKRANINDA (368×448, GERÇEK ÇÖZÜNÜRLÜK)", 16, INK, "lt")
    shots = [screen("gece", stats=(3, 4, 1, 4), selected=2, frame=0, pose="sleep",
                    subtitle="UYUYOR · İYİ GECELER"),
             screen("pamuk", stats=(2, 4, 4, 3), selected=0, frame=0, pose="eat", subtitle="YEMEK YİYOR"),
             screen("seker", stats=(4, 5, 3, 3), selected=1, frame=0, pose="play", subtitle="OYNUYOR"),
             screen("deniz", stats=(4, 4, 3, 2), selected=3, frame=0, pose="wash", subtitle="BANYO ZAMANI")]
    for i, sc in enumerate(shots):
        sheet.alpha_composite(device(sc), (30 + i * 462, top + 46))
    return sheet.crop((0, 0, W, top + 46 + 500 + 30)), shots


if __name__ == "__main__":
    sheet, shots = build_showcase()
    sheet.convert("RGB").save(OUT / "showcase.png")
    for key, sc in zip(["pamuk", "gece", "seker_onden", "deniz_onden"], shots):
        sc.convert("RGB").save(OUT / f"screen_{key}.png")
    for style, key in (("side", "gece"), ("front", "pamuk")):
        frames = [screen(key, "idle", (4, 4, 4, 3), 1, 12, f, style).convert("RGB") for f in (0, 1)]
        frames[0].save(OUT / f"idle_{key}_{style}.gif", save_all=True,
                       append_images=frames[1:], duration=600, loop=0)
    ev, ev_shots = build_events()
    ev.convert("RGB").save(OUT / "events.png")
    for (pose, *_), (key, stats, sel, sub) in zip(EVENTS, [
            ("gece", (3, 4, 1, 4), 2, "UYUYOR · İYİ GECELER"), ("pamuk", (2, 4, 4, 3), 0, "YEMEK YİYOR"),
            ("seker", (4, 5, 3, 3), 1, "OYNUYOR"), ("deniz", (4, 4, 3, 2), 3, "BANYO ZAMANI")]):
        frames = [screen(key, stats=stats, selected=sel, frame=f, pose=pose, subtitle=sub).convert("RGB")
                  for f in (0, 1)]
        frames[0].save(OUT / f"event_{pose}_{key}.gif", save_all=True, append_images=frames[1:],
                       duration=450 if pose != "sleep" else 900, loop=0)
    print("ok", OUT / "showcase.png", OUT / "events.png")
