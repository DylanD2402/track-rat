"""
Builds the README pictures from the PC-rendered screens.

    python tools/readme_images.py            # reuse build/screens
    python tools/readme_images.py --render   # run tools/render_screens.py first

Writes docs/images/screens/*.png (2x), docs/images/gallery.png and
docs/images/social-preview.png (1280x640).
"""

import argparse
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "build", "screens")
OUT = os.path.join(ROOT, "docs", "images")

# (render file, readable name)
CHOSEN = [
    ("04_menu", "menu"),
    ("06_track_select", "track-select"),
    ("07_ready", "ready"),
    ("09_running_ahead", "timing-ahead"),
    ("11_running_behind_split", "timing-behind"),
    ("10_map", "map"),
    ("12b_lap_flash_pb", "lap-flash-pb"),
    ("17c_debrief", "pit-debrief"),
    ("17d_debrief_laps_sectors", "pit-debrief-laps"),
    ("22_perf_card", "performance-card"),
    ("18_free_roam", "free-roam"),
    ("25_wifi", "wifi"),
    ("30_settings_list", "settings"),
    ("36b_engine_data_reading", "engine-data"),
    ("37b_camera_recording", "camera"),
    ("46_overheat_coolant", "overheat-warning"),
]
HERO = ["timing-ahead", "map", "pit-debrief"]
BG = (17, 17, 17)

BOLD = ["C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "DejaVuSans-Bold.ttf"]
REG = ["C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
       "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVuSans.ttf"]


def font(paths, size):
    for p in paths:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            pass
    return ImageFont.load_default()


def rounded(img, radius):
    img = img.convert("RGBA")
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, img.width - 1, img.height - 1), radius, fill=255)
    img.putalpha(mask)
    return img


def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.convert("RGB").save(path, optimize=True)
    print("wrote %s  %dx%d  %.1f KB" % (os.path.relpath(path, ROOT), img.width, img.height,
                                       os.path.getsize(path) / 1024))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", action="store_true", help="run render_screens.py first")
    args = ap.parse_args()
    if args.render:
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "render_screens.py")])

    shots = {}
    for src, name in CHOSEN:
        p = os.path.join(SRC, src + ".png")
        if not os.path.exists(p):
            sys.exit("missing %s (run with --render)" % p)
        shots[name] = Image.open(p).convert("RGB")

    for name, im in shots.items():
        save(im.resize((im.width * 2, im.height * 2), Image.NEAREST),
             os.path.join(OUT, "screens", name + ".png"))

    # gallery: 4 across
    cols, gap = 4, 24
    w, h = 320, 240
    rows = (len(CHOSEN) + cols - 1) // cols
    g = Image.new("RGB", (gap + cols * (w + gap), gap + rows * (h + gap)), BG)
    for i, (_, name) in enumerate(CHOSEN):
        x = gap + (i % cols) * (w + gap)
        y = gap + (i // cols) * (h + gap)
        tile = rounded(shots[name], 10)
        g.paste(tile, (x, y), tile)
    save(g, os.path.join(OUT, "gallery.png"))

    # social preview
    W, H = 1280, 640
    s = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(s)
    title, sub = "Track Rat", "Open-source GPS lap timer \u00b7 offline \u00b7 no app \u00b7 no subscription"
    ft, fs = font(BOLD, 76), font(REG, 30)
    for text, f, y, col in ((title, ft, 40, (255, 255, 255)), (sub, fs, 140, (170, 170, 170))):
        tw = d.textlength(text, font=f)
        d.text(((W - tw) / 2, y), text, font=f, fill=col)
    sw, sh, sgap = 360, 270, 40
    total = 3 * sw + 2 * sgap
    x0, y0 = (W - total) // 2, 270
    for i, name in enumerate(HERO):
        im = shots[name].resize((sw, sh), Image.NEAREST)
        tile = rounded(im, 14)
        s.paste(tile, (x0 + i * (sw + sgap), y0), tile)
    save(s, os.path.join(OUT, "social-preview.png"))


if __name__ == "__main__":
    main()
