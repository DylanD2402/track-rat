"""
PlatformIO pre-build script (extra_scripts in platformio.ini).

- Gzips the Wi-Fi page: web/index.html -> data/web/index.html.gz, which
  `pio run -e es3c28p -t uploadfs` copies into the board's flash. Runs on
  every pio command, so the copy in data/ is never out of date.
- Copies the page's fonts (web/fonts/*.woff2, Inter, already compressed)
  to data/web/fonts/. The phone has no internet on Track Rat's network, so
  the page can't load fonts from anywhere else.
- Builds WifiPortal.cpp with TRACKRAT_VERSION set from `git describe`, shown
  on the page's Device tab. Only that one file is rebuilt when it changes.

Can also be run on its own: python tools/build_web.py
"""

import gzip
import os
import shutil
import subprocess

try:
    Import("env")  # noqa: F821 (provided by PlatformIO)
except NameError:
    env = None

ROOT = os.getcwd() if env is None else env.subst("$PROJECT_DIR")
SOURCES = ["index.html"]


def gzip_web():
    src_dir = os.path.join(ROOT, "web")
    out_dir = os.path.join(ROOT, "data", "web")
    os.makedirs(out_dir, exist_ok=True)
    for name in SOURCES:
        with open(os.path.join(src_dir, name), "rb") as f:
            raw = f.read()
        packed = gzip.compress(raw, compresslevel=9, mtime=0)  # same input, same bytes
        out = os.path.join(out_dir, name + ".gz")
        old = open(out, "rb").read() if os.path.exists(out) else None
        if old != packed:
            with open(out, "wb") as f:
                f.write(packed)
            print(f"build_web: {name} {len(raw)} -> {len(packed)} bytes gzipped")


def copy_fonts():
    src_dir = os.path.join(ROOT, "web", "fonts")
    out_dir = os.path.join(ROOT, "data", "web", "fonts")
    if not os.path.isdir(src_dir):
        return
    os.makedirs(out_dir, exist_ok=True)
    for name in sorted(os.listdir(src_dir)):
        if not name.endswith(".woff2"):
            continue
        src, out = os.path.join(src_dir, name), os.path.join(out_dir, name)
        if not os.path.exists(out) or open(src, "rb").read() != open(out, "rb").read():
            shutil.copyfile(src, out)
            print(f"build_web: fonts/{name} copied")


def git_version():
    try:
        out = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=ROOT, capture_output=True,
                             text=True, timeout=10)
        return out.stdout.strip() or "dev"
    except Exception:
        return "dev"


gzip_web()
copy_fonts()

if env is not None:
    version = git_version()

    def add_version(env, node):
        return env.Object(node, CPPDEFINES=list(env["CPPDEFINES"]) + [("TRACKRAT_VERSION", env.StringifyMacro(version))])

    env.AddBuildMiddleware(add_version, "*WifiPortal.cpp")
