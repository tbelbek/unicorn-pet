"""Turn test/out/tour/*.ppm (from `make -C test tour`) into design/tour.gif: every frame with
its caption in a strip below the screen. Identical consecutive frames are merged.

Run:  uv run --with pillow python tools/make_tour.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
src = ROOT / "test/out/tour"
times = [int(t) for t in (src / "times.txt").read_text().split()]
caps = (src / "captions.txt").read_text(encoding="utf-8").split("\n")
FONTS = ["/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
font = next((ImageFont.truetype(f, 17) for f in FONTS if Path(f).exists()), ImageFont.load_default())
BAR = 48

def framed(img, text):
    out = Image.new("RGB", (img.width, img.height + BAR), (24, 18, 44))
    out.paste(img, (0, 0))
    d = ImageDraw.Draw(out)
    words, lines = text.split(), [""]
    for w in words:                        # wrap to two lines at most
        trial = (lines[-1] + " " + w).strip()
        if d.textlength(trial, font=font) > img.width - 16 and lines[-1]:
            lines.append(w)
        else:
            lines[-1] = trial
    y = img.height + (BAR - 20 * len(lines)) // 2
    for line in lines[:2]:
        d.text(((img.width - d.textlength(line, font=font)) // 2, y), line, font=font, fill=(255, 224, 102))
        y += 20
    return out

files = sorted(src.glob("*.ppm"))
caps = caps[:len(times)]                   # the file ends with a newline
if not (len(files) == len(times) == len(caps)) or [p.stem for p in files] != [f"{i:04d}" for i in range(len(files))]:
    raise SystemExit(f"{len(files)} frames, {len(times)} times, {len(caps)} captions: re-run make -C test tour")
frames, durations, last = [], [], None
for p, ms, cap in zip(files, times, caps):
    img = framed(Image.open(p).convert("RGB"), cap)
    key = img.tobytes()
    if key == last:
        durations[-1] += ms
        continue
    last = key
    frames.append(img)
    durations.append(ms)
pal = [f.quantize(128) for f in frames]
out = ROOT / "design/tour.gif"
pal[0].save(out, save_all=True, append_images=pal[1:], duration=durations, loop=0, optimize=True)
print(f"{out} {len(pal)} frames, {sum(durations) // 1000} s, {out.stat().st_size // 1024} KB")
