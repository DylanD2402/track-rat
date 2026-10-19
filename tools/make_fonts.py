"""
Makes the screen's fonts: include/fonts/*.h, anti-aliased "smooth fonts" for
TFT_eSPI (its .vlw format, stored as C arrays in flash).

The typeface is Inter (https://rsms.me/inter/, SIL Open Font License 1.1, see
include/fonts/OFL.txt): a clean grotesque in the style of Apple's San
Francisco. Small sizes use Inter, the big digits use Inter Display (its
tighter cut for large text). Digits are tabular, so a running clock doesn't
shift sideways as it counts.

    pip install pillow fonttools     (Pillow with libraqm, for the tabular digits)
    python tools/make_fonts.py --fonts DIR

DIR holds the Inter .otf or .ttf files (Inter-Medium, Inter-SemiBold,
InterDisplay-SemiBold, ...), e.g. from the release zip at
https://github.com/rsms/inter/releases or /usr/share/fonts/opentype/inter
on Debian/Ubuntu (package fonts-inter).

To change a size or weight, edit FONTS below, run this again and rebuild.
"""

import argparse
import os
import struct
import sys

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont, features

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "include", "fonts")

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
EXTRA = "±°·–—…‹›−×¼½⅛"  # skipped if the typeface lacks one
DIGITS = "0123456789:.+-−% "
CAPS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .-"

# name, file, pixel size, characters. Sizes are chosen to match the line
# heights of the TFT_eSPI fonts they replaced (2: 16 px, 4: 26 px, 6/7: 48 px, 8: 75 px).
# The order is the order of DisplayManager::Font.
FONTS = [
    ("InterCaption", "Inter-Medium", 12, ASCII + EXTRA),
    ("InterBody", "Inter-Medium", 15, ASCII + EXTRA),
    ("InterButton", "Inter-SemiBold", 16, ASCII + EXTRA),
    ("InterTitle", "Inter-SemiBold", 22, ASCII + EXTRA),
    ("InterHero", "InterDisplay-Bold", 40, CAPS),
    ("InterNumL", "InterDisplay-SemiBold", 50, DIGITS),
    ("InterNumXL", "InterDisplay-SemiBold", 76, DIGITS),
]


def find_font(folder, stem):
    for ext in (".otf", ".ttf"):
        path = os.path.join(folder, stem + ext)
        if os.path.exists(path):
            return path
    sys.exit(f"make_fonts: {stem}.otf / .ttf not found in {folder}")


def has_glyph(font, ch):
    # A missing character comes back as the .notdef box, the same as U+FFFF
    if ch == " ":
        return True
    try:
        return font.getmask(ch).getbbox() is not None and font.getmask(ch).tobytes() != font.getmask("￿").tobytes()
    except Exception:
        return False


def glyph(font, ch, feats):
    """Bitmap and metrics of one character: (w, h, advance, top, left, alpha bytes)."""
    advance = round(font.getlength(ch, features=feats))
    if ch == " ":
        return 0, 0, advance, 0, 0, b""
    # Render on a canvas big enough for any bearing, baseline at y = 2 * size
    size = font.size
    img = Image.new("L", (size * 4, size * 4), 0)
    ImageDraw.Draw(img).text((size, 2 * size), ch, font=font, fill=255, anchor="ls", features=feats)
    box = img.getbbox()
    if not box:
        return 0, 0, advance, 0, 0, b""
    x0, y0, x1, y1 = box
    crop = img.crop(box)
    return x1 - x0, y1 - y0, advance, 2 * size - y0, x0 - size, crop.tobytes()


def make_vlw(path, size, chars, feats):
    font = ImageFont.truetype(path, size)
    cmap = TTFont(path).getBestCmap()
    glyphs = []
    for ch in sorted(set(chars), key=ord):
        if ord(ch) not in cmap:
            print(f"  skipping U+{ord(ch):04X} (not in {os.path.basename(path)})")
            continue
        glyphs.append((ord(ch),) + glyph(font, ch, feats))
    # Ascent and descent from the glyphs actually included, so a digits-only
    # font is only as tall as its digits.
    ascent = max(g[4] for g in glyphs)
    descent = max(max(g[2] - g[4] for g in glyphs), 0)

    out = bytearray(struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent))
    for code, w, h, adv, top, left, _ in glyphs:
        out += struct.pack(">7i", code, h, w, adv, top, left, 0)
    for g in glyphs:
        out += g[6]
    return bytes(out), len(glyphs), ascent, descent


def write_header(name, data, source, size, count, ascent, descent):
    lines = [
        f"// {name}: {source} at {size} px, {count} glyphs, ascent {ascent}, descent {descent}.",
        "// Made by tools/make_fonts.py from Inter (SIL Open Font License 1.1, see OFL.txt). Don't edit.",
        "#pragma once",
        "#include <pgmspace.h>",
        f"const uint8_t {name}[] PROGMEM = {{",
    ]
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    with open(os.path.join(OUT_DIR, name + ".h"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fonts", default="/usr/share/fonts/opentype/inter", help="folder with the Inter font files")
    args = ap.parse_args()
    if not features.check("raqm"):
        sys.exit("make_fonts: Pillow was built without libraqm, so tabular digits can't be selected")

    os.makedirs(OUT_DIR, exist_ok=True)
    total = 0
    for name, stem, size, chars in FONTS:
        data, count, ascent, descent = make_vlw(find_font(args.fonts, stem), size, chars, ["tnum"])
        write_header(name, data, stem, size, count, ascent, descent)
        total += len(data)
        print(f"make_fonts: {name} ({stem} {size} px): {count} glyphs, {len(data) // 1024} KB")
    print(f"make_fonts: {total // 1024} KB of fonts in {os.path.relpath(OUT_DIR, ROOT)}")


if __name__ == "__main__":
    main()
