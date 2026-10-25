"""
Renders every screen to PNG on your PC, so screen changes can be checked
without flashing the board.

Builds the real display code (src/DisplayManager.cpp, DisplayPerf.cpp, DisplaySettings.cpp,
DisplayPower.cpp) against a stand-in for TFT_eSPI (tools/screens/mock), drives
a simulated car round a track from data/tracks.json through the real
LapTimer, and saves each screen. Text is drawn the way TFT_eSPI draws it, with
the same fonts; shapes are close but not pixel-identical.

    pip install pillow
    python tools/render_screens.py              # -> build/screens/*.png and contact_sheet.png
    python tools/render_screens.py --track calabogie --out somewhere

Needs g++ (or set CXX, e.g. CXX="zig c++ -D_USE_MATH_DEFINES" on Windows).
"""

import argparse
import json
import os
import shlex
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCREENS = os.path.join(ROOT, "tools", "screens")
SOURCES = [
    os.path.join(SCREENS, "screens.cpp"),
    os.path.join(SCREENS, "mock", "TFT_eSPI.cpp"),
    os.path.join(ROOT, "src", "DisplayManager.cpp"),
    os.path.join(ROOT, "src", "DisplayPerf.cpp"),
    os.path.join(ROOT, "src", "DisplayDebrief.cpp"),
    os.path.join(ROOT, "src", "DisplayAutocross.cpp"),
    os.path.join(ROOT, "src", "DisplayPower.cpp"),
    os.path.join(ROOT, "src", "DisplaySettings.cpp"),
    os.path.join(ROOT, "src", "Version.cpp"),
    os.path.join(ROOT, "src", "LapTimer.cpp"),
    os.path.join(ROOT, "src", "GpsSource.cpp"),
    os.path.join(ROOT, "src", "UbxParser.cpp"),
]


def write_track(track_id, out):
    with open(os.path.join(ROOT, "data", "tracks.json"), encoding="utf-8") as f:
        tracks = json.load(f)["tracks"]
    t = next((t for t in tracks if t.get("id") == track_id), None)
    if not t or "map" not in t or "startFinish" not in t:
        sys.exit(f"render_screens: track '{track_id}' needs a map and a startFinish in data/tracks.json")
    line = max(t["map"], key=len)  # the circuit; the shorter lines are pit lanes
    pts = ",".join(f"{lat},{lon}" for lat, lon in line)
    with open(os.path.join(out, "track_data.h"), "w") as f:
        f.write("// Made by tools/render_screens.py\n#pragma once\n")
        f.write(f'#define TRACK_NAME "{t["name"]}"\n#define TRACK_LOCATION "{t.get("location", "")}"\n')
        f.write(f'#define TRACK_LENGTH_M {float(t.get("length", 0))}\n')
        f.write(f'#define SF_LAT {t["startFinish"]["lat"]}\n#define SF_LON {t["startFinish"]["lon"]}\n')
        f.write(f"static const double TRACK_PTS[] = {{{pts}}};\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--track", default="calabogie", help="id of a track in data/tracks.json with a map")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "screens"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    write_track(args.track, args.out)

    exe = os.path.join(args.out, "screens.exe" if os.name == "nt" else "screens")
    cmd = [*shlex.split(os.environ.get("CXX", "g++")), "-std=c++17", "-O1", "-DDEBUG_MODE=0", "-DBLE_ENABLED=1",
           '-DTRACKRAT_VERSION="b65364c"', '-DTRACKRAT_BUILT="Oct 24 2026 17:03:12"',
           "-I" + os.path.join(SCREENS, "mock"), "-I" + os.path.join(ROOT, "include"), "-I" + args.out,
           *SOURCES, "-o", exe]
    subprocess.run(cmd, check=True)
    subprocess.run([exe, args.out], check=True)

    names = sorted(n for n in os.listdir(args.out) if n.endswith(".ppm"))
    for n in names:
        src = os.path.join(args.out, n)
        Image.open(src).save(src[:-4] + ".png")
        os.remove(src)

    # Everything on one sheet, at 2x, four across
    cols, scale, pad = 4, 2, 16
    w, h = 320 * scale, 240 * scale
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad) + pad), (40, 40, 44))
    for i, n in enumerate(names):
        img = Image.open(os.path.join(args.out, n[:-4] + ".png")).resize((w, h), Image.NEAREST)
        sheet.paste(img, (pad + (i % cols) * (w + pad), pad + (i // cols) * (h + pad)))
    sheet.save(os.path.join(args.out, "contact_sheet.png"))
    print(f"render_screens: {len(names)} screens in {os.path.relpath(args.out, ROOT)}")


if __name__ == "__main__":
    main()
