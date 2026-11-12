"""
Builds the firmware as a WebAssembly module, so the website can run the device in a browser (site/try).

It compiles the firmware's own code unchanged: src/main.cpp (setup() and loop()), every screen, the lap timer,
the saved settings, the track list, the session files. What a browser does not have is stood in for by
tools/sim: the GPS (fixes come from JavaScript), the battery, Wi-Fi, Bluetooth, the touch controller and BOOT
button, a clock that JavaScript sets, and two in-memory file systems (tools/sim/hal). The firmware's code is
never edited for this. The module is written to site/try/trackrat.wasm; site/try/host.js runs it.

    python tools/build_sim.py                  # -> site/try/trackrat.wasm
    python tools/build_sim.py --out somewhere  # -> somewhere/trackrat.wasm

Needs zig 0.16 to compile (the ZIG environment variable, else `python -m ziglang`, else zig on the PATH:
pip install ziglang==0.16.0) and the ArduinoJson library, which `pio run` downloads into .pio/libdeps
(or set ARDUINOJSON_DIR to its src folder). After changing the firmware, run this again and commit the module.
"""

import argparse
import glob
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, "tools", "sim")

# Firmware files that talk to hardware: tools/sim has a stand-in with the same header for each
STOOD_IN = {
    "CameraBle.cpp", "FirmwareUpdate.cpp", "GPSHandler.cpp", "LogWriter.cpp", "ObdBle.cpp", "Power.cpp",
    "RaceBoxBle.cpp", "RaceChronoBle.cpp", "Splash.cpp", "WifiPortal.cpp",
}


def find_zig():
    z = os.environ.get("ZIG")
    if z:
        return [z] if os.path.exists(z) else shlex.split(z)
    try:
        r = subprocess.run([sys.executable, "-m", "ziglang", "version"], capture_output=True, text=True)
        if r.returncode == 0:
            return [sys.executable, "-m", "ziglang"]
    except OSError:
        pass
    z = shutil.which("zig")
    if z:
        return [z]
    sys.exit("build_sim: zig not found. pip install ziglang==0.16.0 (or set ZIG to zig's path)")


def find_arduinojson():
    d = os.environ.get("ARDUINOJSON_DIR") or os.path.join(ROOT, ".pio", "libdeps", "es3c28p", "ArduinoJson", "src")
    if not os.path.isfile(os.path.join(d, "ArduinoJson.h")):
        sys.exit(f"build_sim: ArduinoJson not found at {d}. Run `pio run` once to download it, "
                 "or set ARDUINOJSON_DIR to the library's src folder")
    return d


def git(*args):
    try:
        r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, timeout=10)
        return r.stdout.strip()
    except Exception:
        return ""


def sources():
    firmware = [os.path.join(ROOT, "src", "main.cpp")]
    for pattern in ("*.cpp", os.path.join("app", "*.cpp")):
        for f in sorted(glob.glob(os.path.join(ROOT, "src", pattern))):
            if os.path.basename(f) in STOOD_IN or os.path.basename(f) == "main.cpp":
                continue
            firmware.append(f)
    return firmware, sorted(glob.glob(os.path.join(SIM, "*.cpp"))) + [os.path.join(ROOT, "tools", "screens", "mock", "TFT_eSPI.cpp")]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, "site", "try"), help="folder for trackrat.wasm")
    args = ap.parse_args()

    zig = find_zig()
    json_dir = find_arduinojson()
    version = (git("describe", "--always", "--dirty") or "dev") + "-sim"
    built = git("log", "-1", "--format=%cd", "--date=format:%b %e %Y %H:%M:%S") or "unknown"  # (the same module every build)

    flags = [
        "-target", "wasm32-wasi", "-std=c++17", "-Os", "-w", "-fno-exceptions", "-fno-rtti",
        "-ffunction-sections", "-fdata-sections", "-D_USE_MATH_DEFINES",
        "-DTRACKRAT_SIM=1", "-DARDUINO=10812", "-DBOARD_HAS_PSRAM",
        f'-DTRACKRAT_VERSION="{version}"', f'-DTRACKRAT_BUILT="{built}"',
        "-I" + os.path.join(SIM, "hal"), "-I" + json_dir, "-I" + os.path.join(ROOT, "tools", "screens", "mock"),
        "-I" + os.path.join(ROOT, "include"), "-I" + os.path.join(ROOT, "src"), "-I" + SIM,
    ]

    firmware, stand_ins = sources()
    os.makedirs(args.out, exist_ok=True)
    out = os.path.join(args.out, "trackrat.wasm")
    failed = []
    with tempfile.TemporaryDirectory() as tmp:
        jobs = []
        for i, src in enumerate(firmware + stand_ins):
            jobs.append((src, os.path.join(tmp, f"{i:02d}_{os.path.splitext(os.path.basename(src))[0]}.o")))

        def compile_one(job):
            src, obj = job
            r = subprocess.run([*zig, "c++", "-c", *flags, src, "-o", obj], capture_output=True, text=True)
            return job, r

        print(f"build_sim: compiling {len(jobs)} files ({len(firmware)} of the firmware's, {len(stand_ins)} of tools/sim)...")
        with ThreadPoolExecutor(max_workers=max(2, os.cpu_count() or 2)) as pool:
            for (src, _), r in pool.map(compile_one, jobs):
                if r.returncode != 0:
                    failed.append(src)
                    print(f"\nbuild_sim: {os.path.relpath(src, ROOT)} failed:\n" + "\n".join((r.stderr or r.stdout).splitlines()[:40]))
        if failed:
            sys.exit(f"build_sim: {len(failed)} file(s) did not compile")

        link = [*zig, "c++", "-target", "wasm32-wasi", "-Os", "-s", "-mexec-model=reactor", "-Wl,--no-entry", "-Wl,--gc-sections",
                "-Wl,-z,stack-size=1048576", "-Wl,--initial-memory=33554432", "-Wl,--max-memory=1073741824",
                *[o for _, o in jobs], "-o", out]
        r = subprocess.run(link, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("build_sim: linking failed:\n" + "\n".join((r.stderr or r.stdout).splitlines()[:60]))

    size = os.path.getsize(out)
    print(f"build_sim: {os.path.relpath(out, ROOT)}  {size / 1024:.0f} KB  (version {version})")


if __name__ == "__main__":
    main()
