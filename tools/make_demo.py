"""
Makes the demo video on the website from the real screen code.

Builds tools/screens/demo.cpp, which plays a scripted session at Calabogie through the
real LapTimer and draws every frame with the real display code (the same stand-ins
as render_screens.py), then encodes the frames with ffmpeg:

    site/assets/demo.mp4          the video, 960x720, 20 frames a second
    site/assets/demo-poster.png   the first frame of the lap, for the video's poster
    site/assets/demo.json         the frame rate, length and captions (cues)

    python tools/make_demo.py                   # the site's files
    python tools/make_demo.py --stills 10       # also every 10th frame, for checking
    python tools/make_demo.py --no-video        # the frames only (CI); site/ is not touched

build/demo/sheet.png shows one frame a second, for checking the screens.
Needs g++ (or set CXX, e.g. CXX="zig c++ -D_USE_MATH_DEFINES" on Windows), Pillow,
and ffmpeg on PATH (or FFMPEG=path to it), unless --no-video.
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys

from PIL import Image

import render_screens as rs

ROOT = rs.ROOT
SCREENS = rs.SCREENS
W, H = 320, 240
FRAME_BYTES = W * H * 3
SHEET_COLS, SHEET_GAP = 5, 8


def read_frames(path, n):
    with open(path, "rb") as f:
        for i in range(n):
            f.seek(i * FRAME_BYTES)
            yield i, f.read(FRAME_BYTES)


def contact_sheet(frames_path, n, fps, out):
    picks = list(range(0, n, fps))  # one a second
    rows = math.ceil(len(picks) / SHEET_COLS)
    w = SHEET_COLS * W + (SHEET_COLS - 1) * SHEET_GAP
    h = rows * H + (rows - 1) * SHEET_GAP
    sheet = Image.new("RGB", (w, h), (40, 40, 44))
    want = set(picks)
    for i, raw in read_frames(frames_path, n):
        if i not in want:
            continue
        k = picks.index(i)
        img = Image.frombytes("RGB", (W, H), raw)
        sheet.paste(img, ((k % SHEET_COLS) * (W + SHEET_GAP), (k // SHEET_COLS) * (H + SHEET_GAP)))
    sheet.save(os.path.join(out, "sheet.png"))


def write_stills(frames_path, n, every, out):
    d = os.path.join(out, "stills")
    os.makedirs(d, exist_ok=True)
    for i, raw in read_frames(frames_path, n):
        if i % every == 0:
            Image.frombytes("RGB", (W, H), raw).save(os.path.join(d, f"f{i:05d}.png"))


def encode(out, scale, crf, frames_path, meta):
    ff = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if not ff:
        sys.exit("make_demo: ffmpeg not found: put it on PATH or set FFMPEG to its path (or use --no-video)")
    assets = os.path.join(ROOT, "site", "assets")
    os.makedirs(assets, exist_ok=True)
    mp4 = os.path.join(assets, "demo.mp4")
    cmd = [ff, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", "20",
           "-i", frames_path,
           "-vf", f"scale={W * scale}:{H * scale}:flags=neighbor:out_color_matrix=bt709:out_range=tv,format=yuv420p",
           "-c:v", "libx264", "-preset", "veryslow", "-crf", str(crf), "-tune", "animation", "-g", "100",
           "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv",
           "-movflags", "+faststart", "-an", mp4]
    subprocess.run(cmd, check=True)

    Image.open(os.path.join(out, "poster.ppm")).resize((W * scale, H * scale), Image.NEAREST).save(
        os.path.join(assets, "demo-poster.png"), optimize=True)
    site_meta = dict(meta, width=W * scale, height=H * scale)
    with open(os.path.join(assets, "demo.json"), "w", encoding="utf-8") as f:
        json.dump(site_meta, f, indent=1)
        f.write("\n")
    print(f"make_demo: {os.path.relpath(mp4, ROOT)} {os.path.getsize(mp4) / 1e6:.2f} MB")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--track", default="calabogie", help="id of a track in data/tracks.json with a map")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "demo"))
    ap.add_argument("--stills", type=int, default=0, metavar="N", help="also write every Nth frame as a PNG")
    ap.add_argument("--no-video", action="store_true", help="build and check the frames; do not encode the video")
    ap.add_argument("--scale", type=int, default=3, help="the video is the screen at this many times its size")
    ap.add_argument("--crf", type=int, default=20, help="x264 quality (lower is bigger)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    rs.write_track(args.track, args.out)

    exe = rs.build(os.path.join(SCREENS, "demo.cpp"), args.out, "demo")
    subprocess.run([exe, args.out], check=True)

    frames_path = os.path.join(args.out, "frames.rgb")
    try:
        with open(os.path.join(args.out, "demo.json"), encoding="utf-8") as f:
            meta = json.load(f)
        n = meta["frames"]
        if os.path.getsize(frames_path) != n * FRAME_BYTES:
            sys.exit("make_demo: frames.rgb does not match demo.json")
        contact_sheet(frames_path, n, meta["fps"], args.out)
        if args.stills:
            write_stills(frames_path, n, args.stills, args.out)
        if not args.no_video:
            encode(args.out, args.scale, args.crf, frames_path, meta)
        print(f"make_demo: {n} frames, sheet in {os.path.relpath(args.out, ROOT)}")
    finally:
        if os.path.exists(frames_path):
            os.remove(frames_path)


if __name__ == "__main__":
    main()
