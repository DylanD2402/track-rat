#!/usr/bin/env python3
"""Tests for tools/overlay.py (6.4), no pytest needed: a synthetic session (a 100 m
circle, an out lap, a lap at 72 km/h, a slower one at 60 km/h, with engine data)
is read back, timed, synced each way, and a few frames are rendered and checked
for what they show.

    pip install -r tools/requirements-overlay.txt
    python test/test_overlay.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import overlay as ov  # noqa: E402

failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg)


R = 100.0  # m
LAT0, LON0 = 45.0, -76.0
CIRC = 2 * math.pi * R
T0 = 14 * 3600 + 3 * 60  # 14:03:00 UTC


def vbo(engine=True):
    """The log Track Rat would write: 20 fixes a second round the circle."""
    lines = ["File created on 20/10/2026 @ 14:03:00", "", "[header]", "satellites", "time", "", "[column names]",
             "sats time lat long velocity heading height HDOP Lap LateralG LongitudinalG LapTag" +
             (" RPM Throttle CoolantTemp IntakeTemp" if engine else ""), "", "[data]"]
    # out lap: a quarter of the circle at 72 km/h; lap 1 at 72 km/h; lap 2 at 60; half of lap 3 at 72
    legs = [(0, CIRC / 4, 20.0), (1, CIRC, 20.0), (2, CIRC, 50 / 3), (3, CIRC / 2, 20.0)]
    t, d = 0.0, 0.0
    for lap, length, v in legs:
        end = d + length
        while d < end - 1e-9:
            a = d / R
            lat = LAT0 + (R * math.sin(a)) / ov.EARTH_R / math.radians(1)
            lon = LON0 + (R * (1 - math.cos(a))) / (ov.EARTH_R * math.cos(math.radians(LAT0))) / math.radians(1)
            sod = T0 + t
            hh, mm, ss = int(sod // 3600), int(sod // 60 % 60), sod % 60
            row = (f"012 {hh:02d}{mm:02d}{ss:06.3f} {lat * 60:012.5f} {-lon * 60:012.5f} {v * 3.6:07.3f} 090.00 "
                   f"+00100.00 00.80 {lap:03d} {v * v / R / 9.81:+06.3f} +0.000 000")
            if engine:
                row += f" {3000 + 1000 * lap:05.0f} 075.0 +092.0 +025.0"
            lines.append(row)
            t += 0.05
            d += v * 0.05
    return "\r\n".join(lines) + "\r\n"


def utc(sod):
    return ov.parse_utc("2026-10-20T00:00:00Z") + sod


SUMMARY = {
    "bestLap": 1,
    "laps": [{"lap": 1, "timeMs": 31416, "sectorsMs": [10000, 20000, 31416]},
             {"lap": 2, "timeMs": 37699, "sectorsMs": [12000, 24000, 37699]}],
    "cameraStartUtc": "2026-10-20T14:02:55.500Z",
    "cameraStartsUtc": ["2026-10-20T14:02:55.500Z", "2026-10-20T14:20:00.000Z"],
    "syncFlashesUtc": ["2026-10-20T14:03:20.000Z"],
}

# --- reading the log -------------------------------------------------------------------------
s = ov.read_session(vbo(), SUMMARY)
check(len(s.t) > 1800 and s.engine and set(s.engine) == {"RPM", "Throttle", "CoolantTemp", "IntakeTemp"}, "read the log with its engine columns")
check(abs(s.t[0] - utc(T0)) < 1e-6, "UTC from the file's date and the fix's time")
check(abs(s.lat[1] - LAT0) < 1e-5 and abs(s.lon[0] - LON0) < 1e-6, "minutes back to degrees, west negative")
check(sorted(s.lap_start) == [0, 1, 2, 3] and s.best_lap == 1, "laps and the best one")
lap1 = s.lap_start[2] - s.lap_start[1]
check(abs(lap1 - CIRC / 20) < 0.06, f"lap 1 took {lap1:.2f} s")
check(abs(s.lap_len[1] - CIRC) < 1, f"lap 1 is {s.lap_len[1]:.1f} m")

# --- what's shown at a moment ------------------------------------------------------------------
half2 = s.lap_start[2] + (CIRC / 2) / (50 / 3)  # half way round the slow lap
st = ov.state_at(s, half2)
check(st["lap"] == 2 and abs(st["kmh"] - 60) < 0.1, f"lap 2 at 60 km/h ({st['kmh']:.1f})")
check(abs(st["lap_time"] - CIRC / 2 / (50 / 3)) < 0.06, f"lap time {st['lap_time']:.2f}")
want = CIRC / 2 / (50 / 3) - CIRC / 2 / 20  # 3.14 s behind at half way
check(abs(st["delta"] - want) < 0.1, f"delta {st['delta']:.2f}, want {want:.2f}")
check(st["engine"]["RPM"] == 5000 and st["engine"]["CoolantTemp"] == 92, "engine values")
check(st["sector"] == 1 and st["sectors"] == [ov.YELLOW], "lap 2 in T2: T1 slower than lap 1's")
st1 = ov.state_at(s, s.lap_start[1] + 25)
check(st1["sectors"] == [ov.PURPLE, ov.PURPLE] and st1["sector"] == 2, "lap 1 set the best sectors")
out = ov.state_at(s, s.t[0] + 2)
check(out["lap"] == 0 and out["lap_time"] is None and out["delta"] is None and out["sector"] is None, "the out lap: no times")
check(ov.state_at(s, s.t[0] - 1) is None and ov.state_at(s, s.t[-1] + 1) is None, "nothing outside the log")
check(abs(ov.state_at(s, s.lap_start[1] + 5)["delta"]) < 0.06, "the best lap against itself: no delta")

# --- syncing ------------------------------------------------------------------------------------
class A:
    start = None
    sync_frame = None
    sync_index = 0
    sync = None
    camera_start = 0
    nudge = 0.0

a = A()
a.sync = "camera"
check(abs(ov.video_start(a, s, None, 30) - utc(T0 - 4.5)) < 1e-6, "camera: cameraStartUtc")
a.camera_start, a.nudge = 1, 0.25
check(abs(ov.video_start(a, s, None, 30) - (utc(14 * 3600 + 20 * 60) + 0.25)) < 1e-6, "the second time out, nudged")
a = A()
a.sync_frame = 90
check(abs(ov.video_start(a, s, None, 30) - utc(T0 + 17)) < 1e-6, "SYNC flash at frame 90 of 30 fps: the video began 3 s before it")
a = A()
a.sync = "creation"
stderr = """Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'IMG_1234.MOV':
  Metadata:
    creation_time   : 2026-10-20T14:03:06.000000Z
    com.apple.quicktime.creationdate: 2026-10-20T10:03:05-0400
  Duration: 00:01:02.50, start: 0.000000, bitrate: 10000 kb/s
    Stream #0:0(und): Video: hevc (Main) (hvc1 / 0x31637668), yuv420p(tv), 1920x1080, 9000 kb/s, 29.97 fps, 29.97 tbr
"""
info = ov.probe(stderr)
check(info["duration"] == 62.5 and info["size"] == (1920, 1080) and abs(info["fps"] - 29.97) < 1e-6, f"probe {info}")
check(abs(ov.video_start(a, s, info, info["fps"]) - utc(T0 + 5)) < 1e-6, "creation: the iPhone's own date, with its time zone")
info2 = ov.probe(stderr.replace("    com.apple.quicktime.creationdate: 2026-10-20T10:03:05-0400\n", ""))
check(abs(info2["created"] - utc(T0 + 6)) < 1e-6, "else creation_time")
a = A()
a.start = "2026-10-20T14:03:01.5Z"
check(abs(ov.video_start(a, s, None, 30) - utc(T0 + 1.5)) < 1e-6, "--start")

# --- frames -------------------------------------------------------------------------------------
o = ov.Overlay(s, (960, 540))
img, drawn = o.frame(st)
check(img.size == (960, 540) and img.mode == "RGBA", "an RGBA frame the size asked for")
check(drawn["speed"] == "60" and drawn["unit"] == "km/h" and drawn["lap"] == "LAP 2", f"speed and lap: {drawn}")
check(drawn["lap_time"] == ov.fmt_lap(st["lap_time"]) and drawn["delta"] == ov.fmt_delta(st["delta"]) and drawn["delta"].startswith("+3.1") and drawn["delta_colour"] == ov.RED,
      f"lap time and delta {drawn.get('lap_time')} {drawn.get('delta')}")
check(drawn["sectors"] == [ov.YELLOW], "the sector strip")
check(drawn["rpm"] == "5000" and drawn["throttle"] == "75%" and drawn["coolant"] == "92°C", "engine")
px = img.getpixel(drawn["car"])
check(px[:3] == ov.BLUE[:3], f"the car's dot on the map: {px}")
x0, y0, x1, y1 = o.map_box
check(x0 <= drawn["car"][0] <= x1 and y0 <= drawn["car"][1] <= y1, "inside the map")
corner = img.getpixel((480, 270))
check(corner[3] == 0, "see-through where nothing is drawn")
# half way round from the start: the dot is on the far side of the circle from where it began
start_dot = o.frame(ov.state_at(s, s.t[0]))[1]["car"]
far = o.frame(ov.state_at(s, s.lap_start[1] + CIRC / 4 / 20))[1]["car"]  # half a circle from the start
check(math.dist(start_dot, far) > (y1 - y0) * 0.6, f"the dot moves round the map {start_dot} {far}")

mph = ov.Overlay(s, (960, 540), "mph").frame(st)[1]
check(mph["speed"] == "37" and mph["unit"] == "mph", "mph")
plain = ov.read_session(vbo(engine=False), SUMMARY)
check("rpm" not in ov.Overlay(plain, (960, 540)).frame(ov.state_at(plain, half2))[1], "no engine data: no engine bars")

# the same as the last frame: not drawn again
b1, new1 = o.frame_bytes(st)
b2, new2 = o.frame_bytes(dict(st))
check(new1 and not new2 and b1 == b2 and len(b1) == 960 * 540 * 4, "a repeated frame is reused")
b3, new3 = o.frame_bytes(ov.state_at(s, half2 + 1))
check(new3 and b3 != b1, "and a new one drawn")

# ffmpeg's arguments
check("prores_ks" in ov.encoder_args("x.mov", (960, 540), 30, None), "ProRes 4444 for .mov")
check("yuva420p" in ov.encoder_args("x.webm", (960, 540), 30, None), "VP9 with alpha for .webm")
burn = ov.encoder_args("x.mp4", (960, 540), 29.97, "in.mp4")
check(burn[:2] == ["-i", "in.mp4"] and any("overlay" in a for a in burn), "burn: the video, then the overlay on top")

if failures:
    print(f"{failures} FAILED")
    sys.exit(1)
print("Overlay checks OK")
