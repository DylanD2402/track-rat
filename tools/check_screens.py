"""
Fails when the README pictures no longer match the screen code.

Renders every screen fresh (tools/render_screens.py), rebuilds the README
pictures from it (tools/readme_images.py), and compares each with the one in
docs/images by decoded pixels. Renders from other compilers or Pillow versions
can differ by a few anti-aliased pixels, so a picture only counts as stale
when more than 0.5% of its pixels differ by more than 32 in any channel.

    python tools/check_screens.py

The social preview's title text uses whatever font the machine has, so only
its screenshots (below the title) are compared.
"""

import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs", "images")
PIXEL_DELTA = 32      # per-channel difference that counts as changed
MAX_CHANGED = 0.005   # fraction of pixels allowed to change
# Only this part of a picture is compared: (left, top, right, bottom)
REGION = {"social-preview.png": (0, 250, 1280, 640)}


def changed_fraction(a, b, box):
    if a.size != b.size:
        return 1.0
    a, b = a.convert("RGB"), b.convert("RGB")
    if box:
        a, b = a.crop(box), b.crop(box)
    diff = ImageChops.difference(a, b).point(lambda v: 255 if v > PIXEL_DELTA else 0)
    r, g, bl = diff.split()
    mask = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    return mask.histogram()[255] / (a.width * a.height)


def main():
    tmp = tempfile.mkdtemp(prefix="trackrat-screens-")
    renders, fresh = os.path.join(tmp, "render"), os.path.join(tmp, "images")
    py = sys.executable
    subprocess.check_call([py, os.path.join(ROOT, "tools", "render_screens.py"), "--out", renders])
    subprocess.check_call([py, os.path.join(ROOT, "tools", "readme_images.py"), "--src", renders, "--out", fresh])

    stale, checked = [], 0
    for dirpath, _, files in os.walk(fresh):
        for f in sorted(files):
            new = os.path.join(dirpath, f)
            rel = os.path.relpath(new, fresh)
            old = os.path.join(DOCS, rel)
            checked += 1
            if not os.path.exists(old):
                stale.append((rel, "missing"))
                continue
            frac = changed_fraction(Image.open(old), Image.open(new), REGION.get(f))
            if frac > MAX_CHANGED:
                stale.append((rel, "%.1f%% of pixels differ" % (frac * 100)))
    # Pictures in docs/images/screens that readme_images.py no longer makes
    for f in sorted(os.listdir(os.path.join(DOCS, "screens"))):
        if not os.path.exists(os.path.join(fresh, "screens", f)):
            stale.append((os.path.join("screens", f), "no longer produced"))

    if stale:
        print("check_screens: %d of %d README pictures are stale:" % (len(stale), checked))
        for rel, why in stale:
            print("  docs/images/%s  (%s)" % (rel.replace(os.sep, "/"), why))
        print("\nThe screens changed. Regenerate and commit the pictures:")
        print("  python tools/render_screens.py")
        print("  python tools/readme_images.py")
        print("  git add docs/images")
        sys.exit(1)
    print("check_screens: %d README pictures match the screen code" % checked)


if __name__ == "__main__":
    main()
