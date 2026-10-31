"""Unicorn pixel-art sprites, built from shapes so palettes and moods stay consistent.

Run:  uv run --with pillow python tools/sprites.py
Out:  design/*.png
"""
import math
from pathlib import Path
from PIL import Image, ImageColor

W, H = 40, 46          # sprite grid; unicorn faces right
OY = 4                 # vertical offset so the horn tip has room for its outline

PALETTES = {
    "pamuk": dict(  # classic: white coat, rainbow mane, gold horn
        name="Pamuk", body="#FFF7FB", shade="#F4C9E3", far="#E9B6D6", muzzle="#FFE0EE",
        outline="#5B2A86", hoof="#B48AE0", horn="#FFD84D", horn2="#E9A50F",
        eye="#2B1840", blush="#FF8FB8",
        mane=["#FF5C8A", "#FF9F43", "#FFE066", "#5EE07A", "#4CC9F0", "#9B6BFF"],
        sparkle=None),
    "gece": dict(  # galaxy: violet coat, night-sky mane with stars, crystal horn
        name="Gece Yıldızı", body="#8E7CFF", shade="#6A55E0", far="#5B46C8", muzzle="#B4A8FF",
        outline="#3A2C9C", hoof="#3B2A8F", horn="#D9FBFF", horn2="#7FE3FF",
        eye="#170C3A", blush="#FF7AD9",
        mane=["#2E22A6", "#5A35D6", "#8E4BEA", "#C463E8", "#F28AE0", "#FFD1F2"],
        sparkle="#FFFFFF", forelock=(3, 4), wings=("#FFFFFF", "#C9BEFF")),
    "seker": dict(  # candy: pink coat, mint/lemon mane, peppermint horn
        name="Şeker", body="#FFC2E0", shade="#F48DC0", far="#E879B0", muzzle="#FFDDEE",
        outline="#8A1F5C", hoof="#FF6FAE", horn="#FFFFFF", horn2="#FF4F9A",
        eye="#3A0F2B", blush="#FF5C9C",
        mane=["#3DDBB0", "#7FF0CF", "#C7FFE9", "#FFF6A3", "#FFE066", "#FFFFFF"],
        sparkle=None),
    "deniz": dict(  # sea: sky-blue coat, ocean mane, pearl horn
        name="Deniz", body="#BFEFFF", shade="#86D3F2", far="#6CC2E8", muzzle="#DDF7FF",
        outline="#0E4A7A", hoof="#2E7FC0", horn="#FFF4E0", horn2="#FFB6A0",
        eye="#0B2540", blush="#FF9DB0",
        mane=["#0077B6", "#0096C7", "#00B4D8", "#48CAE4", "#90E0EF", "#CAF0F8"],
        sparkle=None),
}


def ellipse(cx, cy, rx, ry):
    for y in range(H):
        for x in range(W):
            if ((x + .5 - cx) / rx) ** 2 + ((y + .5 - cy - OY) / ry) ** 2 <= 1:
                yield x, y


def rect(x0, x1, y0, y1):
    for y in range(y0 + OY, y1 + OY + 1):
        for x in range(x0, x1 + 1):
            yield x, y


def tri(a, b, c):
    def side(p, q, r):
        return (p[0] - r[0]) * (q[1] - r[1]) - (q[0] - r[0]) * (p[1] - r[1])
    for y in range(H):
        for x in range(W):
            p = (x + .5, y + .5 - OY)
            d1, d2, d3 = side(p, a, b), side(p, b, c), side(p, c, a)
            if not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0)):
                yield x, y


def strand(points, width):
    """Thick polyline: every pixel within width/2 of the path."""
    r = width / 2
    for y in range(H):
        for x in range(W):
            px, py = x + .5, y + .5 - OY
            for (ax, ay), (bx, by) in zip(points, points[1:]):
                dx, dy = bx - ax, by - ay
                t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
                if math.hypot(px - ax - t * dx, py - ay - t * dy) <= r:
                    yield x, y
                    break


# Mane and tail are parallel strands, one per rainbow colour, offset outward.
MANE = [(25, 3.5), (21.5, 5), (19, 8.5), (17.5, 13), (17.5, 17.5), (18.5, 21)]
TAIL = [(9, 22), (6, 22.5), (4, 25), (3.5, 29), (5, 33)]
FEET = 38 + OY          # first pixel row below the hooves' outline: the ground line

# Each pose moves whole body groups; legs are (start, end, width, far?) strands so they can bend.
STAND_LEGS = [((11, 29), (11, 35.5), 3, True), ((22, 29), (22, 35.5), 3, True),
              ((14, 30), (14, 36.5), 3, False), ((25, 30), (25, 36.5), 3, False)]
POSES = {
    "stand": dict(body=(17, 26, 9.5, 6.5), neck=(22.5, 20, 4, 6), head=(0, 0), lift=0,
                  legs=STAND_LEGS, tail=TAIL,
                  wings=[(9, 13), (8.5, 16.5), (9.5, 20)], wroot=(17.5, 22)),
    # lying down, legs folded under, head resting on the ground, tail curled round
    "sleep": dict(body=(17, 31.5, 10.5, 6), neck=(24.5, 28.5, 4.5, 4.5), head=(2, 15), lift=0,
                  legs=[((10, 36), (15, 36), 3, True), ((18, 37.5), (25, 37.5), 2.6, False)],
                  tail=[(8, 30.5), (4.5, 31.5), (3.5, 34), (6.5, 36), (12, 36.5)],
                  wings=[(10.5, 26), (9.5, 28.5), (11.5, 30.5)], wroot=(18, 28.5)),
    # head down into the food bowl
    "eat": dict(body=(17, 26, 9.5, 6.5), neck=(26, 23.5, 4.2, 5.5), head=(3, 12), lift=0,
                legs=STAND_LEGS, tail=TAIL,
                wings=[(9, 13), (8.5, 16.5), (9.5, 20)], wroot=(17.5, 22)),
    # mid-jump: front legs tucked, back legs kicked out, tail and mane streaming
    "play": dict(body=(17, 26, 9.5, 6.5), neck=(22.5, 20, 4, 6), head=(1, -1), lift=-4,
                 legs=[((11, 30), (6.5, 34), 3, True), ((22, 29), (26.5, 31), 3, True),
                       ((14, 31), (9.5, 35.5), 3, False), ((25, 30), (29.5, 32.5), 3, False)],
                 tail=[(9, 22), (5.5, 20), (3.5, 16.5), (3, 12.5)],
                 wings=[(7, 7), (5, 11), (6, 15.5)], wroot=(17.5, 22)),
    # bath: standing, shaking off the water
    "wash": dict(body=(17, 26, 9.5, 6.5), neck=(22.5, 20, 4, 6), head=(0, 0), lift=0,
                 legs=STAND_LEGS, tail=TAIL,
                 wings=[(9, 13), (8.5, 16.5), (9.5, 20)], wroot=(17.5, 22)),
}
POSE_MOOD = {"sleep": "sleepy", "eat": "eating", "play": "happy", "wash": "happy"}


def build(p, mood="idle", frame=0, pose="stand"):
    px = {}            # (x,y) -> colour
    kind = {}          # (x,y) -> body part, used for shading

    def put(cells, colour, k):
        for c in cells:
            px[c] = colour
            kind[c] = k

    P = POSES[pose]
    mood = POSE_MOOD.get(pose, mood)
    bob = 1 if frame == 1 else 0           # breathing: body dips one pixel
    lift = P["lift"] + (3 if pose == "play" and frame == 1 else 0)   # jump apex vs landing
    shake = (1 if frame == 1 else -1) if pose == "wash" else 0
    hx, hy = P["head"][0] + shake, P["head"][1] + bob + lift
    by = bob + lift
    if pose == "eat":
        hy += bob                           # chewing nods the head an extra pixel

    def leg(seg, colour, k):
        (ax, ay), (bx, b_y), w, _ = seg
        put(strand([(ax + shake, ay + by), (bx + shake, b_y + by)], w), colour, k)
        put(ellipse(bx + shake, b_y + by + .6, 1.6, 1.3), p["hoof"], "hoof")

    for seg in P["legs"]:
        if seg[3]:
            leg(seg, p["far"], "far")
    for k, col in reversed(list(enumerate(p["mane"]))):
        off = (k - 2.5) * 1.1
        pts = [(x - abs(off) * .3 + shake, y + off + by) for x, y in P["tail"]]
        put(strand(pts, 2.2), col, "mane")
    bx_, by_, brx, bry = P["body"]
    put(ellipse(bx_ + shake, by_ + by, brx, bry), p["body"], "body")
    for seg in P["legs"]:
        if not seg[3]:
            leg(seg, p["body"], "leg")
    nx, ny, nrx, nry = P["neck"]
    put(ellipse(nx + shake, ny + by + (bob if pose == "eat" else 0), nrx, nry), p["body"], "body")
    put(ellipse(26 + hx, 13.5 + hy, 8, 7), p["body"], "body")
    put(ellipse(31.5 + hx, 17 + hy, 4.5, 3.6), p["muzzle"], "muzzle")
    # horn with spiral stripes
    for c in tri((25.8 + hx, 8.6 + hy), (31.2 + hx, 6.8 + hy), (32.4 + hx, 1.2 + hy)):
        x, y = c
        px[c] = p["horn2"] if (x + y) % 3 == 0 else p["horn"]
        kind[c] = "horn"
    # mane over the neck/head, outermost strand first so inner strands sit on top
    for k, col in reversed(list(enumerate(p["mane"]))):
        off = k * 1.05
        flow = -.5 * k if pose == "play" else 0        # mane streams back in the jump
        pts = [(x - off + hx + flow, y - off * .35 + hy) for x, y in MANE]
        put(strand(pts, 2.3), col, "mane")
    # forelock falling over the forehead
    f0, f1 = p.get("forelock", (0, 1))
    put(strand([(25 + hx, 4.5 + hy), (27.5 + hx, 7 + hy), (28 + hx, 9.5 + hy)], 2), p["mane"][f0], "mane")
    put(strand([(24 + hx, 5 + hy), (25.5 + hx, 8 + hy)], 1.6), p["mane"][f1], "mane")
    # ear sits in front of the mane, with a blush-pink inner ear
    put(tri((22.2 + hx, 8 + hy), (23.2 + hx, 2.2 + hy), (26.2 + hx, 6.8 + hy)), p["body"], "ear")
    for x, y in [(23, 5), (23, 6), (24, 6)]:
        px[(x + hx, y + OY + hy)] = p["blush"]
        kind[(x + hx, y + OY + hy)] = "inner"
    # alicorn wings, drawn over the mane so they read on the back
    if p.get("wings"):
        w1, w2 = p["wings"]
        flap = -1 if frame == 1 else 0
        rx_, ry_ = P["wroot"]
        root = (rx_ + shake, ry_ + by)
        for (tx, ty), col in zip(P["wings"], [w2, w1, w1]):
            put(strand([root, (tx + shake, ty + by + flap)], 2.8), col, "wing")

    # rim shading on bottom/right edges of coat
    coat = {"body", "leg", "ear"}
    for (x, y), k in list(kind.items()):
        if k in coat:
            below, right = kind.get((x, y + 1)), kind.get((x + 1, y))
            if below is None or right is None or below in ("far", "hoof"):
                px[(x, y)] = p["shade"]

    face(px, p, mood, 0, hx, hy, frame)
    if p["sparkle"]:
        for (x, y), k in list(kind.items()):
            if k == "mane" and (x * 7 + y * 13) % 41 == 0:
                px[(x, y)] = p["sparkle"]

    # selective outline: every empty pixel touching the sprite
    out = dict(px)
    for (x, y) in px:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if n not in px and 0 <= n[0] < W and 0 <= n[1] < H:
                out[n] = p["outline"]
    return out


def _disc(cx, cy, r):
    for y in range(H):
        for x in range(W):
            if (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 <= r * r:
                yield x, y


def with_props(cells, pose, frame=0, outline="#2A1F4A"):
    """Event props drawn into the sprite grid: food bowl, ball, bath bubbles."""
    prop = {}
    if pose == "eat":
        for y, x0, x1, col in [(36, 30, 39, "#FF8FC4"), (37, 31, 38, "#E0559A"), (38, 32, 37, "#C9407F")]:
            for x in range(x0, x1 + 1):
                prop[(x, y + OY)] = col
        heap = [(32, 35), (33, 35), (34, 35), (35, 35), (36, 35), (37, 35), (33, 34), (34, 34),
                (35, 34), (36, 34)]
        for i, (x, y) in enumerate(heap[: 10 - 3 * frame]):          # the heap shrinks as she eats
            prop[(x, y + OY)] = "#FF5C6E" if i % 3 else "#5EE07A"
    if pose == "play":
        cx, cy = (35.5, 3.5) if frame == 0 else (34.5, 8.5)
        for x, y in _disc(cx, cy, 2.8):
            prop[(x, y)] = "#FFD84D" if (y - cy + 1.2) // 1.4 % 2 == 0 else "#FF5C9C"
        prop[(int(cx) - 1, int(cy) - 1)] = "#FFFFFF"
    if pose == "wash":
        rise = -1 if frame == 1 else 0
        for cx, cy, r in [(5, 15, 2.2), (10, 10, 1.5), (34, 27, 2.2), (29, 35, 1.6),
                          (19, 19, 1.3), (3, 31, 1.8), (37, 33, 1.3), (14, 30, 1.6)]:
            cy += OY + rise
            for x, y in _disc(cx, cy, r):
                edge = (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 > (r - 1) ** 2
                prop[(x, y)] = "#6FD6FF" if edge else "#D9F6FF"
            prop[(int(cx - r / 2), int(cy - r / 2))] = "#FFFFFF"
    out = dict(cells)
    out.update(prop)
    for (x, y) in prop:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if n not in out and 0 <= n[0] < W and 0 <= n[1] < H:
                out[n] = outline
    return out


def face(px, p, mood, bob, hx=0, hy=0, frame=0):
    def dot(x, y, c): px[(x + hx, y + OY + hy)] = c
    if mood in ("idle", "hungry", "sad"):
        for x, y in [(29, 12), (30, 12), (29, 13), (30, 13), (29, 14), (30, 14)]:
            dot(x, y, p["eye"])
        dot(29, 12, "#FFFFFF")                       # catch-light
    elif mood in ("happy", "eating"):                 # ^ ^ closed smiling eye
        for x, y in [(28, 13), (29, 12), (30, 12), (31, 13)]:
            dot(x, y, p["eye"])
    elif mood == "sleepy":                            # closed eye line
        for x in (28, 29, 30, 31):
            dot(x, 13, p["eye"])
    dot(30, 16, p["blush"]); dot(31, 16, p["blush"])
    dot(34, 17, p["shade"])                          # nostril
    if mood == "idle":
        dot(32, 19, p["shade"]); dot(33, 19, p["shade"])
    if mood == "sad":                                 # frown + a tear
        for x, y in [(31, 20), (32, 19), (33, 19), (34, 20)]:
            dot(x, y, p["eye"])
        dot(29, 15, "#9FE7FF"); dot(29, 16, "#9FE7FF")
    if mood == "happy":
        for x, y in [(31, 19), (32, 20), (33, 20), (34, 19)]:
            dot(x, y, p["eye"])
    if mood == "eating":                              # chewing: open, then closed
        if frame == 0:
            for x, y in [(32, 19), (33, 19), (32, 20), (33, 20)]:
                dot(x, y, p["eye"])
        else:
            dot(32, 19, p["shade"]); dot(33, 19, p["shade"])
    if mood == "hungry":                              # small open mouth
        for x, y in [(32, 19), (33, 19), (32, 20), (33, 20)]:
            dot(x, y, p["eye"])


def render(px, scale, bg=None):
    img = Image.new("RGBA", (W, H), bg or (0, 0, 0, 0))
    for (x, y), c in px.items():
        if 0 <= x < W and 0 <= y < H:
            img.putpixel((x, y), ImageColor.getrgb(c) + (255,))
    return img.resize((W * scale, H * scale), Image.NEAREST)


if __name__ == "__main__":
    out = Path(__file__).resolve().parent.parent / "design"
    poses = list(POSES)
    sheet = Image.new("RGBA", (len(poses) * W * 5, 2 * H * 5), "#000000")
    for i, pose in enumerate(poses):
        for f in (0, 1):
            sheet.alpha_composite(render(with_props(build(PALETTES["gece"], frame=f, pose=pose), pose, f), 5),
                                  (i * W * 5, f * H * 5))
    sheet.save(out / "draft_poses.png")
    print("ok", out / "draft_poses.png")
