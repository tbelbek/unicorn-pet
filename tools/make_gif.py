"""Turn test/out/rec/*.ppm (from `make -C test record`) into design/gameplay.gif.

Run:  uv run --with pillow python tools/make_gif.py
Drops consecutive identical frames (the screen only changes when the core redraws) and keeps
their time, so the GIF plays at the same pace as the device.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
frames, durations = [], []
for p in sorted((ROOT / "test/out/rec").glob("*.ppm")):
    img = Image.open(p).convert("RGB")
    if frames and img.tobytes() == frames[-1].tobytes():
        durations[-1] += 100
        continue
    frames.append(img)
    durations.append(100)
small = [f.resize((f.width * 3 // 4, f.height * 3 // 4), Image.NEAREST).quantize(64) for f in frames]
out = ROOT / "design/gameplay.gif"
small[0].save(out, save_all=True, append_images=small[1:], duration=durations, loop=0, optimize=True)
print(f"{out} {len(small)} frames, {out.stat().st_size // 1024} KB")
