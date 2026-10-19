"""
PlatformIO pre-build script (extra_scripts in platformio.ini).

- Gzips the Wi-Fi page: web/index.html -> data/web/index.html.gz, which
  `pio run -e es3c28p -t uploadfs` copies into the board's flash. Runs on
  every pio command, so the copy in data/ is never out of date.
- Builds WifiPortal.cpp with TRACKRAT_VERSION set from `git describe`, shown
  on the page's Device tab. Only that one file is rebuilt when it changes.

Can also be run on its own: python tools/build_web.py
"""

import gzip
import os
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


def git_version():
    try:
        out = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=ROOT, capture_output=True,
                             text=True, timeout=10)
        return out.stdout.strip() or "dev"
    except Exception:
        return "dev"


gzip_web()

if env is not None:
    version = git_version()

    def add_version(env, node):
        return env.Object(node, CPPDEFINES=list(env["CPPDEFINES"]) + [("TRACKRAT_VERSION", env.StringifyMacro(version))])

    env.AddBuildMiddleware(add_version, "*WifiPortal.cpp")
