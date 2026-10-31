"""Front-facing unicorn face, same palettes as sprites.py. Symmetric about x = 24.

Mirror pairs: column x <-> 47 - x.
"""
import math
from PIL import Image, ImageColor

from sprites import PALETTES

W, H = 48, 50


def _cells(test):
    for y in range(H):
        for x in range(W):
            if test(x + .5, y + .5):
                yield x, y


def ellipse(cx, cy, rx, ry):
    return _cells(lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1)


def tri(a, b, c):
    def side(p, q, r):
        return (p[0] - r[0]) * (q[1] - r[1]) - (q[0] - r[0]) * (p[1] - r[1])
    def inside(x, y):
        p = (x, y)
        d = side(p, a, b), side(p, b, c), side(p, c, a)
        return not (min(d) < 0 < max(d))
    return _cells(inside)


def strand(points, width):
    r = width / 2
    def near(px, py):
        for (ax, ay), (bx, by) in zip(points, points[1:]):
            dx, dy = bx - ax, by - ay
            t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
            if math.hypot(px - ax - t * dx, py - ay - t * dy) <= r:
                return True
        return False
    return _cells(near)


def mirror(points):
    return [(48 - x, y) for x, y in points]


SIDE_LOCK = [(17, 13), (11, 17), (8, 25), (7.5, 33), (9, 41)]
EYE = {  # 5 wide x 6 tall, top-left anchored; E eye, W catch-light, I iris glint
    "idle":   [".EEE.", "EWWEE", "EWEEE", "EEEEE", "EEEIE", ".EEE."],
    "hungry": [".EEE.", "EWWEE", "EWEEE", "EEEEE", "EEEIE", ".EEE."],
    "happy":  [".....", ".EEE.", "E...E", ".....", ".....", "....."],
    "sleepy": [".....", ".....", ".....", "E...E", ".EEE.", "....."],
    "sad":    [".EEE.", "EWWEE", "EWEEE", "EEEEE", "EEEIE", ".EEE."],
}
MOUTH = {
    "idle":   [(22, 41), (23, 42), (24, 42), (25, 41)],
    "happy":  [(21, 40), (22, 40), (23, 40), (24, 40), (25, 40), (26, 40),
               (22, 41), (25, 41), (23, 42), (24, 42)],
    "hungry": [(23, 40), (24, 40), (22, 41), (25, 41), (22, 42), (25, 42), (23, 43), (24, 43)],
    "sleepy": [(23, 42), (24, 42)],
    "sad":    [(22, 42), (23, 41), (24, 41), (25, 42)],
}


def build(p, mood="idle", frame=0):
    px, kind = {}, {}

    def put(cells, colour, k):
        for c in cells:
            px[c] = colour
            kind[c] = k

    bob = 1 if frame == 1 else 0
    mane = p["mane"]
    # side locks behind the head: outer strand first
    for k, col in reversed(list(enumerate(mane))):
        off = k * 1.0
        left = [(x - off, y + off * .25 + bob) for x, y in SIDE_LOCK]
        put(strand(left, 2.4), col, "mane")
        put(strand(mirror(left), 2.4), col, "mane")
    # ears
    for a in ([(9.5, 21), (10.5, 7.5), (18, 15.5)],):
        put(tri(*[(x, y + bob) for x, y in a]), p["body"], "ear")
        put(tri(*[(48 - x, y + bob) for x, y in a]), p["body"], "ear")
    for x, y in [(12, 12), (12, 13), (13, 14), (12, 14)]:
        for xx in (x, 47 - x):
            px[(xx, y + bob)] = p["blush"]; kind[(xx, y + bob)] = "inner"
    # head + muzzle
    put(ellipse(24, 29 + bob, 15, 13.5), p["body"], "head")
    put(ellipse(24, 38.5 + bob, 9, 5.8), p["muzzle"], "muzzle")
    # horn, centred, with spiral stripes
    for c in tri((20.2, 16.5 + bob), (27.8, 16.5 + bob), (24, 1.2 + bob)):
        x, y = c
        px[c] = p["horn2"] if (2 * y + x) % 6 < 2 else p["horn"]      # diagonal spiral bands
        kind[c] = "horn"
    # forelock sweeping across the forehead, three bands
    f0, f1 = p.get("forelock", (0, 1))
    for i, col in enumerate([mane[f0], mane[f1], mane[(f1 + 1) % len(mane)]]):
        pts = [(15 + i * 1.6, 15.5 + bob), (22 + i * 1.4, 17.2 + i * 1.2 + bob),
               (28 + i * .8, 21 + i * 1.3 + bob)]
        put(strand(pts, 2.6), col, "mane")
    # shading: bottom rim of head and muzzle
    for (x, y), k in list(kind.items()):
        if k in ("head", "muzzle") and kind.get((x, y + 1)) in (None, "mane"):
            px[(x, y)] = p["shade"]
    # eyes
    for ry, row in enumerate(EYE[mood]):
        for rx, ch in enumerate(row):
            if ch == ".":
                continue
            col = {"E": p["eye"], "W": "#FFFFFF", "I": mane[-1]}[ch]
            px[(13 + rx, 24 + ry + bob)] = col
            # mirror keeps the catch-light on the same (upper-left) side for both eyes
            px[(30 + rx, 24 + ry + bob)] = col
    # cheeks, nostrils, mouth
    for x in (11, 12, 13):
        px[(x, 31 + bob)] = p["blush"]; px[(47 - x, 31 + bob)] = p["blush"]
    px[(21, 37 + bob)] = p["shade"]; px[(26, 37 + bob)] = p["shade"]
    for x, y in MOUTH[mood]:
        px[(x, y + bob)] = p["eye"]
    if mood == "happy":
        px[(23, 41 + bob)] = p["blush"]; px[(24, 41 + bob)] = p["blush"]   # tongue
    if mood == "hungry":
        px[(23, 41 + bob)] = "#3A1030"; px[(24, 41 + bob)] = "#3A1030"
        px[(26, 44 + bob)] = "#9FE7FF"                                      # drool
    if mood == "sad":                                       # tears under both eyes
        for x, y in [(14, 30), (14, 31), (33, 30), (33, 31)]:
            px[(x, y + bob)] = "#9FE7FF"
    if p.get("sparkle"):
        for x, y in [(7, 30), (40, 28), (9, 38), (39, 37)]:
            if kind.get((x, y + bob)) == "mane":
                px[(x, y + bob)] = p["sparkle"]
    # selective outline
    out = dict(px)
    for (x, y) in px:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if n not in px and 0 <= n[0] < W and 0 <= n[1] < H:
                out[n] = p["outline"]
    return out


def render(px, scale):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for (x, y), c in px.items():
        if 0 <= x < W and 0 <= y < H:
            img.putpixel((x, y), ImageColor.getrgb(c) + (255,))
    return img.resize((W * scale, H * scale), Image.NEAREST)


def sprite(key, mood="idle", frame=0, scale=5):
    return render(build(PALETTES[key], mood, frame), scale)


if __name__ == "__main__":
    from pathlib import Path
    out = Path(__file__).resolve().parent.parent / "design"
    keys = list(PALETTES)
    moods = ["idle", "happy", "hungry", "sleepy"]
    sheet = Image.new("RGBA", (4 * W * 5, 2 * H * 5), "#000000")
    for i, k in enumerate(keys):
        sheet.alpha_composite(sprite(k), (i * W * 5, 0))
    for i, m in enumerate(moods):
        sheet.alpha_composite(sprite("pamuk", m), (i * W * 5, H * 5))
    sheet.save(out / "draft_front.png")
    print("ok")
