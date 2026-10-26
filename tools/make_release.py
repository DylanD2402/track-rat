#!/usr/bin/env python3
"""
Builds a Track Rat release into dist/ (run from the repo root, on a PC or in CI).

    python tools/make_release.py                 build firmware + file system, then package
    python tools/make_release.py --no-build      package what is already in .pio/build/es3c28p
    python tools/make_release.py --notes 0.1.0-beta.1
                                                 print that version's CHANGELOG.md section
                                                 (nothing, exit 0, if there is none)

dist/ gets:
  trackrat-<version>-full.bin      everything in one image, for esptool at offset 0x0
  trackrat.bin                     the app alone (a copy of firmware.bin), for the SD card
                                   and the Wi-Fi page
  trackrat-<version>-littlefs.bin  the file system alone
  bootloader.bin partitions.bin boot_app0.bin firmware.bin littlefs.bin
                                   the separate parts, for the web installer
  manifest.json                    the web installer's (ESP Web Tools) list of parts
  SHA256SUMS                       checksums of every .bin

Only the Python standard library is used.
"""

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ENV = "es3c28p"
ROOT = os.getcwd()
BUILD = os.path.join(ROOT, ".pio", "build", ENV)
DIST = os.path.join(ROOT, "dist")

# Arduino's default_16MB.csv (platformio.ini: board_build.partitions)
OFFSET_BOOTLOADER = 0x0
OFFSET_PARTITIONS = 0x8000
OFFSET_BOOT_APP0 = 0xE000   # the otadata image: makes app0 boot, even if an OTA update had switched to app1
OFFSET_APP = 0x10000
OFFSET_LITTLEFS = 0xC90000
LITTLEFS_SIZE = 0x360000

# Flash settings. These must be what `pio run -t upload` uses, because the header of
# the bootloader image carries them and the ROM reads it to set the flash up.
# PlatformIO's espressif32 builder (platforms/espressif32/builder/main.py) passes
#   --flash_mode ${__get_board_flash_mode}  --flash_freq ${__get_board_f_image}
#   --flash_size upload.flash_size
# and __get_board_flash_mode returns "dout" only for the opi_opi / opi_qspi memory types,
# otherwise it turns qio and qout into "dio". This board is memory_type qio_opi with
# flash_mode qio, so the upload writes the header as DIO. The frequency is the board's
# f_flash, 80 MHz (esp32-s3-devkitc-1). platformio.ini says upload.flash_size = 16MB.
# (The firmware still runs its flash in QIO once it starts; the header is only for the ROM.)
FLASH_MODE = "dio"
FLASH_FREQ = "80m"
FLASH_SIZE = "16MB"


def die(msg):
    print(f"make_release: {msg}", file=sys.stderr)
    sys.exit(1)


def git_version():
    # Same string the firmware shows (tools/build_web.py), without a leading "v"
    out = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=ROOT,
                         capture_output=True, text=True, timeout=30)
    v = out.stdout.strip()
    if out.returncode != 0 or not v:
        die("git describe failed")
    return v[1:] if v.startswith("v") else v


def changelog_notes(version):
    path = os.path.join(ROOT, "CHANGELOG.md")
    if not os.path.exists(path):
        return ""
    version = version[1:] if version.startswith("v") else version
    lines, found = [], False
    head = re.compile(r"^##\s+\[?v?" + re.escape(version) + r"\]?(\s|$)")
    with open(path, encoding="utf-8") as f:
        for line in f:
            if found:
                if line.startswith("## "):
                    break
                lines.append(line)
            elif head.match(line):
                found = True
    return "".join(lines).strip() + "\n" if found and "".join(lines).strip() else ""


def core_dir():
    return os.environ.get("PLATFORMIO_CORE_DIR") or os.path.join(os.path.expanduser("~"), ".platformio")


def find_boot_app0():
    pats = os.path.join(core_dir(), "packages", "framework-arduinoespressif32*", "tools", "partitions", "boot_app0.bin")
    hits = sorted(glob.glob(pats))
    if not hits:
        die(f"boot_app0.bin not found ({pats})")
    return hits[0]


def esptool_cmd():
    """(command list, merge subcommand spelling): PlatformIO's own esptool first."""
    script = os.path.join(core_dir(), "packages", "tool-esptoolpy", "esptool.py")
    candidates = []
    if os.path.exists(script):
        candidates.append([sys.executable, script])
    candidates.append([sys.executable, "-m", "esptool"])
    for cmd in candidates:
        try:
            r = subprocess.run(cmd + ["--help"], capture_output=True, text=True, timeout=60)
        except Exception:
            continue
        text = r.stdout + r.stderr
        if "merge-bin" in text:
            return cmd, "merge-bin"   # esptool v5
        if "merge_bin" in text:
            return cmd, "merge_bin"   # esptool v4
    die("esptool not found (PlatformIO's tool-esptoolpy, or pip install esptool)")


def run(cmd, **kw):
    print("+ " + " ".join(cmd))
    r = subprocess.run(cmd, cwd=ROOT, **kw)
    if r.returncode != 0:
        die(f"command failed ({r.returncode})")


def build():
    pio = shutil.which("pio") or shutil.which("platformio")
    if not pio:
        die("pio not found (pip install platformio)")
    run([pio, "run", "-e", ENV])
    run([pio, "run", "-e", ENV, "-t", "buildfs"])


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description="Build Track Rat release files into dist/")
    ap.add_argument("--no-build", action="store_true", help="use what is already in .pio/build/" + ENV)
    ap.add_argument("--notes", metavar="VERSION", help="print that version's CHANGELOG.md section and exit")
    args = ap.parse_args()

    if args.notes:
        sys.stdout.reconfigure(encoding="utf-8")  # the notes have ◀ and ·, which a Windows console can't encode
        sys.stdout.write(changelog_notes(args.notes))
        return

    version = git_version()
    if not args.no_build:
        build()

    parts = {  # name in dist/ -> (source, offset)
        "bootloader.bin": (os.path.join(BUILD, "bootloader.bin"), OFFSET_BOOTLOADER),
        "partitions.bin": (os.path.join(BUILD, "partitions.bin"), OFFSET_PARTITIONS),
        "boot_app0.bin": (find_boot_app0(), OFFSET_BOOT_APP0),
        "firmware.bin": (os.path.join(BUILD, "firmware.bin"), OFFSET_APP),
        "littlefs.bin": (os.path.join(BUILD, "littlefs.bin"), OFFSET_LITTLEFS),
    }
    for name, (src, _) in parts.items():
        if not os.path.exists(src):
            die(f"missing {src}" + ("" if name == "boot_app0.bin" else " (build first, or drop --no-build)"))
    if os.path.getsize(parts["littlefs.bin"][0]) > LITTLEFS_SIZE:
        die("littlefs.bin is bigger than the LittleFS partition")

    if os.path.isdir(DIST):
        shutil.rmtree(DIST)
    os.makedirs(DIST)
    for name, (src, _) in parts.items():
        shutil.copyfile(src, os.path.join(DIST, name))

    # One merged image for offset 0x0; gaps are 0xFF, which is erased flash
    full = f"trackrat-{version}-full.bin"
    tool, sub = esptool_cmd()
    cmd = tool + ["--chip", "esp32s3", sub, "-o", os.path.join(DIST, full)]
    if sub == "merge-bin":
        cmd += ["--flash-mode", FLASH_MODE, "--flash-freq", FLASH_FREQ, "--flash-size", FLASH_SIZE]
    else:
        cmd += ["--flash_mode", FLASH_MODE, "--flash_freq", FLASH_FREQ, "--flash_size", FLASH_SIZE]
    for name, (_, off) in parts.items():
        cmd += [hex(off), os.path.join(DIST, name)]
    run(cmd)

    # The merge rewrites the bootloader's header (flash mode/frequency/size), as an upload
    # does. The web installer writes bootloader.bin as it is, so give it the same bytes.
    blen = os.path.getsize(parts["bootloader.bin"][0])
    with open(os.path.join(DIST, full), "rb") as f:
        patched = f.read(blen)
    with open(os.path.join(DIST, "bootloader.bin"), "wb") as f:
        f.write(patched)

    shutil.copyfile(os.path.join(DIST, "firmware.bin"), os.path.join(DIST, "trackrat.bin"))
    shutil.copyfile(os.path.join(DIST, "littlefs.bin"), os.path.join(DIST, f"trackrat-{version}-littlefs.bin"))

    manifest = {
        "name": "Track Rat",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,  # the firmware has no Improv
        "builds": [{
            "chipFamily": "ESP32-S3",
            "parts": [{"path": name, "offset": off} for name, (_, off) in parts.items()],
        }],
    }
    with open(os.path.join(DIST, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    bins = sorted(n for n in os.listdir(DIST) if n.endswith(".bin"))
    with open(os.path.join(DIST, "SHA256SUMS"), "w", encoding="utf-8", newline="\n") as f:
        for n in bins:
            f.write(f"{sha256(os.path.join(DIST, n))}  {n}\n")

    print(f"\nTrack Rat {version} -> dist/")
    for n in sorted(os.listdir(DIST)):
        print(f"  {os.path.getsize(os.path.join(DIST, n)):>10,}  {n}")


if __name__ == "__main__":
    main()
