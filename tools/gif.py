"""Assemble the PNG frames tools/shoot.ps1 recorded into an animated GIF.

    python tools/gif.py <frames_dir> <out.gif> [--fps 12] [--width 800]

Frames are read in name order, scaled to `width`, quantised to an adaptive
128-colour palette (text stays crisp, files stay small) and written with the
frame duration derived from `fps`. Needs Pillow.
"""
import argparse, glob, os, sys
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("frames_dir")
ap.add_argument("out")
ap.add_argument("--fps", type=float, default=12.0)
ap.add_argument("--width", type=int, default=800)
a = ap.parse_args()

files = sorted(glob.glob(os.path.join(a.frames_dir, "*.png")))
if len(files) < 2:
    sys.exit(f"gif.py: need at least 2 frames in {a.frames_dir}, found {len(files)}")

frames = []
for f in files:
    im = Image.open(f).convert("RGB")
    if im.width > a.width:
        im = im.resize((a.width, round(im.height * a.width / im.width)), Image.LANCZOS)
    frames.append(im.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG))

frames[0].save(a.out, save_all=True, append_images=frames[1:],
               duration=round(1000.0 / a.fps), loop=0, optimize=True, disposal=1)
print(f"{a.out}: {len(frames)} frames @ {a.fps:g} fps, {os.path.getsize(a.out) / 1048576:.2f} MB")
