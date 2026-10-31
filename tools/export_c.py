"""Export sprites, icons and the pixel font to core/assets.gen.h.

Run:  uv run --with pillow python tools/export_c.py
One shared palette (index 0 = transparent) keeps every sprite one byte per pixel.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageColor

import sprites as S
import front as F
import showcase as SC

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "core" / "assets.gen.h"

CHARS = ["pamuk", "gece", "seker", "deniz"]
MOODS = ["idle", "happy", "hungry", "sleepy", "sad"]
# side states: the five moods standing, then the four event poses (props baked in)
SIDE_STATES = [("stand", m) for m in MOODS] + [("sleep", "idle"), ("eat", "idle"),
                                               ("play", "idle"), ("wash", "idle"),
                                               ("walk", "idle"), ("stand", "blink"), ("hop", "idle"),
                                               ("walk2", "idle")]
ICONS = ["apple", "heart", "star", "bubble", "moon", "ball", "strawberry", "carrot", "hand",
         "note", "cloud", "mirror", "house", "bow", "flower", "tiara"]
import i18n  # noqa: E402  (tools/i18n.py: the letters every language needs)
BASE_GLYPHS = [chr(c) for c in range(32, 127)] + list("ÇçĞğİıÖöŞşÜü·×")
GLYPHS = BASE_GLYPHS + [c for c in i18n.letters() if c not in BASE_GLYPHS]

palette = ["#000000"]          # slot 0 = transparent
index = {}


def idx(hexcol):
    rgb = ImageColor.getrgb(hexcol)[:3]
    key = "#%02X%02X%02X" % rgb
    if key not in index:
        index[key] = len(palette)
        palette.append(key)
    return index[key]


def cells_to_bytes(cells, w, h):
    out = bytearray(w * h)
    for (x, y), c in cells.items():
        if 0 <= x < w and 0 <= y < h:
            out[y * w + x] = idx(c)
    return out


def image_to_bytes(img):
    img = img.convert("RGBA")
    out = bytearray(img.width * img.height)
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))
            if a:
                out[y * img.width + x] = idx("#%02X%02X%02X" % (r, g, b))
    return out


def c_array(name, data, per_line=40):
    rows = [", ".join(str(b) for b in data[i:i + per_line]) for i in range(0, len(data), per_line)]
    return f"static const unsigned char {name}[{len(data)}] = {{\n" + ",\n".join(rows) + "\n};\n"


def font_bitmaps():
    """Render glyphs into a tall scratch cell, then crop to the rows any glyph actually inks
    (getbbox misses the dot on İ and the cedilla on Ş/Ç at 8 px)."""
    font = ImageFont.truetype(str(ROOT / "assets/fonts/PressStart2P-Regular.ttf"), 8)
    pad = 4
    imgs = []
    for ch in GLYPHS:
        img = Image.new("1", (8, 8 + 2 * pad), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((0, pad), ch, font=font, fill=1)
        imgs.append(img)
    base = imgs[:len(BASE_GLYPHS)]         # the row window comes from the original set only
    rows = [y for img in base for y in range(img.height) if any(img.getpixel((x, y)) for x in range(8))]
    top, bottom = min(rows) - 1, max(rows) + 1   # one spare row on top for the dot on İ
    data = bytearray()
    overrides = {**accented_overrides(font, pad, top, bottom), **OVERRIDES}
    for ch, img in zip(GLYPHS, imgs):
        rows = overrides.get(ch)
        for y in range(top, bottom):
            if rows:
                r = rows[y - top] if y - top < len(rows) else "........"
                data.append(sum(0x80 >> x for x, c in enumerate(r) if c == "#"))
            else:
                data.append(sum(0x80 >> x for x in range(8) if img.getpixel((x, y))))
    return bottom - top, data


# Press Start 2P squeezes İ into a short I that reads as "i"; with the spare top row it gets a
# full-height I (same as plain I) and its dot above, like every other capital.
OVERRIDES = {"İ": ["...##...", ".######.", "...##...", "...##...", "...##...", "...##...", "...##...",
                   ".######.", "........"]}


# Accented capitals: Press Start 2P squashes the letter to fit the accent into 8 rows, so they read
# as lower case. Like İ, they get the spare top row for the accent and a full-height letter below.
ACCENT_ROWS = {"acute": "....##..", "grave": "..##....", "circ": "..####..", "tilde": ".##..##.",
               "dia": ".#...#..", "ring": "...##...", "breve": "..####.."}   # all different per base letter
ACCENTED = {"À": ("A", "grave"), "Á": ("A", "acute"), "Â": ("A", "circ"), "Ã": ("A", "tilde"), "Ä": ("A", "dia"),
            "Å": ("A", "ring"), "È": ("E", "grave"), "É": ("E", "acute"), "Ê": ("E", "circ"), "Ë": ("E", "dia"),
            "Ì": ("I", "grave"), "Í": ("I", "acute"), "Î": ("I", "circ"), "Ï": ("I", "dia"), "Ñ": ("N", "tilde"),
            "Ò": ("O", "grave"), "Ó": ("O", "acute"), "Ô": ("O", "circ"), "Õ": ("O", "tilde"), "Ö": ("O", "dia"),
            "Ù": ("U", "grave"), "Ú": ("U", "acute"), "Û": ("U", "circ"), "Ü": ("U", "dia"),
            "Ё": ("Е", "dia"), "Й": ("И", "breve")}


def accented_overrides(font, pad, top, bottom):
    out = {}
    for ch, (base, mark) in ACCENTED.items():
        img = Image.new("1", (8, 8 + 2 * pad), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((0, pad), base, font=font, fill=1)
        rows = ["".join("#" if img.getpixel((x, y)) else "." for x in range(8)) for y in range(top, bottom)]
        rows[0] = ACCENT_ROWS[mark]
        out[ch] = rows
    seen = {}
    for ch, rows in out.items():           # two accents on one letter drawn alike: a silent typo on screen
        key = (ACCENTED[ch][0], tuple(rows))   # (Ё and Ë may match: they are the same shape in print too)
        assert key not in seen, f"{ch} and {seen.get(key)} would look the same"
        seen[key] = ch
    return out


def main():
    parts = []
    side = bytearray()
    heads = []                             # accessory anchor per side state and frame (same for all)
    for ci, key in enumerate(CHARS):
        p = S.PALETTES[key]
        for pose, mood in SIDE_STATES:
            for f in (0, 1):
                cells = S.with_props(S.build(p, mood, f, pose), pose, f)
                side += cells_to_bytes(cells, S.W, S.H)
                if ci == 0:
                    heads += list(S.HEAD)
    front = bytearray()
    for key in CHARS:
        for mood in MOODS:
            for f in (0, 1):
                front += cells_to_bytes(F.build(S.PALETTES[key], mood, f), F.W, F.H)
    eggs = bytearray()
    for crack, wobble in [(False, 0), (False, 1), (True, 0), (True, -1)]:
        eggs += image_to_bytes(SC.egg(1, crack, wobble))
    icons = bytearray()
    for name in ICONS:
        icons += image_to_bytes(SC.icon(name, 1))
    font_h, font = font_bitmaps()

    names = [S.PALETTES[k]["name"] for k in CHARS]
    accents = [idx(S.PALETTES[k]["mane"][4 if k == "gece" else 2]) for k in CHARS]

    parts.append("// GENERATED by tools/export_c.py - do not edit.\n#pragma once\n")
    parts.append(f"#define UP_SIDE_W {S.W}\n#define UP_SIDE_H {S.H}\n#define UP_SIDE_FEET {S.FEET}\n")
    parts.append(f"#define UP_FRONT_W {F.W}\n#define UP_FRONT_H {F.H}\n")
    parts.append("#define UP_EGG_W 20\n#define UP_EGG_H 24\n#define UP_ICON_W 9\n#define UP_ICON_H 8\n")
    parts.append(f"#define UP_CHARS {len(CHARS)}\n#define UP_SIDE_STATES {len(SIDE_STATES)}\n"
                 f"#define UP_MOODS {len(MOODS)}\n#define UP_FONT_H {font_h}\n"
                 f"#define UP_GLYPHS {len(GLYPHS)}\n#define UP_PALETTE {len(palette)}\n")
    parts.append("// side states: " + ", ".join(f"{p}/{m}" for p, m in SIDE_STATES) + "\n")
    parts.append("// icons: " + ", ".join(ICONS) + "\n")
    rgb = bytearray()
    for hx in palette:
        rgb += bytes(ImageColor.getrgb(hx)[:3])
    parts.append(c_array("UP_PAL_RGB", rgb))
    parts.append(c_array("UP_SIDE", side))
    parts.append(c_array("UP_FRONT", front))
    parts.append(c_array("UP_EGG", eggs))
    parts.append(c_array("UP_ICONS", icons))
    parts.append(c_array("UP_FONT", font))
    cps = ", ".join(str(ord(ch)) for ch in GLYPHS)
    parts.append(f"static const unsigned short UP_FONT_CP[{len(GLYPHS)}] = {{ {cps} }};\n")
    parts.append(f"static const unsigned char UP_ACCENT[{len(CHARS)}] = {{ {', '.join(map(str, accents))} }};\n")
    manes = ", ".join("{" + ", ".join(str(idx(c)) for c in S.PALETTES[k]["mane"]) + "}" for k in CHARS)
    parts.append(f"static const unsigned char UP_MANE[{len(CHARS)}][6] = {{ {manes} }};\n")
    hs = ", ".join("{{%d, %d}, {%d, %d}}" % tuple(heads[i:i + 4]) for i in range(0, len(heads), 4))
    parts.append(f"static const signed char UP_HEAD[{len(SIDE_STATES)}][2][2] = {{ {hs} }};\n")
    parts.append("static const char* const UP_NAMES[%d] = { %s };\n"
                 % (len(names), ", ".join('"%s"' % n.upper() for n in names)))
    assert len(palette) <= 256, len(palette)
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text("".join(parts), encoding="utf-8")
    print(f"ok {OUT} palette={len(palette)} side={len(side)}B front={len(front)}B font_h={font_h}")


if __name__ == "__main__":
    main()
