"""Turn test/out/rec/*.ppm (from `make -C test record`) into design/gameplay.gif.

Run:  uv run --with pillow python tools/make_gif.py
Each frame's on-screen time comes from times.txt (record.cpp). Identical consecutive frames are
merged and keep their time, so the GIF plays at the same pace as the device.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
rec = ROOT / "test/out/rec"
times = [int(t) for t in (rec / "times.txt").read_text().split()]
frames, durations = [], []
for p, ms in zip(sorted(rec.glob("*.ppm")), times):
    img = Image.open(p).convert("RGB")
    if frames and img.tobytes() == frames[-1].tobytes():
        durations[-1] += ms
        continue
    frames.append(img)
    durations.append(ms)
small = [f.resize((f.width * 3 // 4, f.height * 3 // 4), Image.NEAREST).quantize(64) for f in frames]
out = ROOT / "design/gameplay.gif"
small[0].save(out, save_all=True, append_images=small[1:], duration=durations, loop=0, optimize=True)
print(f"{out} {len(small)} frames, {out.stat().st_size // 1024} KB")
