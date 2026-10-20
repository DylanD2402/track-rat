#!/usr/bin/env python3
"""Track Rat overlay videos (6.4): a session's data drawn over your video.

Reads a session's .vbo (every fix) and its .json summary (laps, sectors, and
when a paired DJI camera started recording), and renders an overlay in Track
Rat's style: the track map with the car, speed, lap, lap time and delta to
the best lap, the sectors, a g circle, and RPM, throttle and coolant when the
log has engine data.

Output, by default at 30 fps:
  - a transparent overlay video to drop on top of the footage in an editor:
    ProRes 4444 (.mov, the default) or VP9 with alpha (.webm);
  - or, with --burn video.mp4, the video with the overlay drawn on (.mp4).

Lining it up with the video (pick one; --nudge SECONDS adds a correction):
  --sync camera         the summary's cameraStartUtc: a DJI camera with Track
                        Rat as its GPS remote (6.2). --camera-start N for the
                        Nth time out of the pits (one video each).
  --sync creation       the video's own creation time (an iPhone's
                        com.apple.quicktime.creationdate, else creation_time).
  --sync-frame N        the video frame where the SYNC flash first shows
                        (tap the top bar of Track Rat's summary or debrief while
                        the camera films it). --sync-index K for the Kth flash.
  --start UTC           the video started at this GPS time (2026-10-20T14:03:12.3Z).

  pip install -r tools/requirements-overlay.txt
  python tools/overlay.py SESSION.vbo --burn GX010042.MP4 --sync camera
  python tools/overlay.py SESSION.vbo --video IMG_1234.MOV --sync creation --nudge -0.4 --units mph
  python tools/overlay.py SESSION.vbo --duration 120 -o overlay.webm

ffmpeg comes with imageio-ffmpeg (or use one on the PATH).
"""

import argparse
import bisect
import datetime as dt
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "web", "fonts")
EARTH_R = 6371000.0
KMH_PER_MPH = 1.609344

# Track Rat's colours (include/UiTheme.h)
BG = (0, 0, 0, 150)  # the panels: black, see-through
TEXT = (255, 255, 255, 255)
TEXT2 = (174, 174, 178, 255)
GREEN = (48, 209, 88, 255)
RED = (255, 69, 58, 255)
YELLOW = (255, 214, 10, 255)
PURPLE = (191, 90, 242, 255)
BLUE = (10, 132, 255, 255)
LINE = (255, 255, 255, 110)


# --- the log --------------------------------------------------------------------------

class Session:
    """A .vbo log: every fix as plain numbers, and the laps worked out from it."""

    def __init__(self):
        self.cols = []
        self.t = []      # UTC seconds since 1970, each fix
        self.lat = []
        self.lon = []
        self.kmh = []
        self.lap = []    # lap number (0: the out lap)
        self.glat = []   # + = turning right
        self.glong = []  # + = speeding up
        self.engine = {}  # "RPM", "Throttle", "CoolantTemp", "IntakeTemp": lists, when logged
        self.dist = []   # metres since the lap began
        self.summary = {}
        self.lap_start = {}  # lap -> UTC seconds it began
        self.lap_len = {}    # finished lap -> metres
        self.best_lap = None
        self.ref = None  # the best lap: ([distance], [time into the lap])


def parse_vbo_time(s):
    """hhmmss.sss -> seconds since midnight"""
    return int(s[0:2]) * 3600 + int(s[2:4]) * 60 + float(s[4:])


def read_session(vbo_text, summary=None):
    """A Session from a .vbo's text and its summary (a dict, or None)."""
    s = Session()
    s.summary = summary or {}
    m = re.search(r"File created on (\d\d)/(\d\d)/(\d{4})", vbo_text)
    day0 = dt.datetime(int(m.group(3)), int(m.group(2)), int(m.group(1)), tzinfo=dt.timezone.utc) if m else \
        dt.datetime(2000, 1, 1, tzinfo=dt.timezone.utc)
    base = day0.timestamp()
    section, last_sod, days = "", None, 0
    for raw in vbo_text.splitlines():
        line = raw.strip()
        if line.startswith("["):
            section = line
            continue
        if not line:
            continue
        if section == "[column names]":
            s.cols = line.split()
            for c in ("RPM", "Throttle", "CoolantTemp", "IntakeTemp"):
                if c in s.cols:
                    s.engine[c] = []
        elif section == "[data]" and s.cols:
            v = line.split()
            if len(v) != len(s.cols):
                continue  # cut short by a power cut
            row = dict(zip(s.cols, v))
            lat, lon = float(row["lat"]) / 60.0, -float(row["long"]) / 60.0
            if lat == 0 and lon == 0:
                continue
            sod = parse_vbo_time(row["time"])
            if last_sod is not None and sod < last_sod - 3600:
                days += 1  # past midnight
            last_sod = sod
            s.t.append(base + days * 86400 + sod)
            s.lat.append(lat)
            s.lon.append(lon)
            s.kmh.append(float(row["velocity"]))
            s.lap.append(int(float(row.get("Lap", 0))))
            s.glat.append(float(row.get("LateralG", 0)))
            s.glong.append(float(row.get("LongitudinalG", 0)))
            for c in s.engine:
                s.engine[c].append(float(row[c]))
    if s.engine and all(max(v, default=0) <= 0 for v in s.engine.values()):
        s.engine = {}  # the columns are there but the engine never answered
    _laps(s)
    return s


def local_xy(lat, lon, lat0, lon0):
    return ((lon - lon0) * math.radians(1) * EARTH_R * math.cos(math.radians(lat0)), (lat - lat0) * math.radians(1) * EARTH_R)


def _laps(s):
    """Distance into each lap, when each lap began, and the best lap as the delta's reference."""
    if not s.t:
        return
    lat0, lon0 = s.lat[0], s.lon[0]
    prev = None
    d = 0.0
    for i in range(len(s.t)):
        if i == 0 or s.lap[i] != s.lap[i - 1]:
            d = 0.0
            prev = None
            s.lap_start.setdefault(s.lap[i], s.t[i])
        x, y = local_xy(s.lat[i], s.lon[i], lat0, lon0)
        if prev:
            d += math.hypot(x - prev[0], y - prev[1])
        prev = (x, y)
        s.dist.append(d)
    # Complete laps: a later lap began. The best: the summary's (clean laps only), else the fastest.
    done = {}
    laps = sorted(s.lap_start)
    for a, b in zip(laps, laps[1:]):
        if a > 0:
            done[a] = s.lap_start[b] - s.lap_start[a]
    for i in range(len(s.t)):
        if s.lap[i] in done:
            s.lap_len[s.lap[i]] = s.dist[i]  # (the last fix of the lap wins)
    best = s.summary.get("bestLap")
    if best not in done:
        best = min(done, key=done.get) if done else None
    s.best_lap = best
    if best is not None:
        idx = [i for i in range(len(s.t)) if s.lap[i] == best]
        s.ref = ([s.dist[i] for i in idx], [s.t[i] - s.lap_start[best] for i in idx], done[best])


def _at(xs, ys, x):
    """ys at x, between the nearest two (xs ascending)"""
    if not xs:
        return None
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    i = bisect.bisect_right(xs, x)
    a, b = xs[i - 1], xs[i]
    u = (x - a) / (b - a) if b > a else 0
    return ys[i - 1] + (ys[i] - ys[i - 1]) * u


def state_at(s, utc):
    """Everything the overlay shows at UTC seconds `utc`, or None outside the log."""
    if not s.t or utc < s.t[0] or utc > s.t[-1]:
        return None
    i = max(1, bisect.bisect_right(s.t, utc))
    i = min(i, len(s.t) - 1)
    a, b = i - 1, i
    u = (utc - s.t[a]) / (s.t[b] - s.t[a]) if s.t[b] > s.t[a] else 0
    mix = lambda arr: arr[a] + (arr[b] - arr[a]) * u
    lap = s.lap[a]
    st = {
        "utc": utc, "lat": mix(s.lat), "lon": mix(s.lon), "kmh": mix(s.kmh), "lap": lap,
        "glat": mix(s.glat), "glong": mix(s.glong), "lap_time": None, "delta": None,
        "engine": {c: mix(v) for c, v in s.engine.items()}, "sector": None, "sectors": [],
    }
    if lap > 0 and lap in s.lap_start:
        st["lap_time"] = utc - s.lap_start[lap]
        if s.ref:
            d = s.dist[a] + (s.dist[b] - s.dist[a]) * u if s.lap[b] == lap else s.dist[a]
            ref_d, ref_t, _ = s.ref
            # A finished lap is stretched to the best lap's length, so the same point on track lines up
            lap_len = s.lap_len.get(lap)
            if lap_len and ref_d[-1] > 0:
                d = d * ref_d[-1] / lap_len
            st["delta"] = st["lap_time"] - _at(ref_d, ref_t, d)
        st["sector"], st["sectors"] = _sectors(s, lap, st["lap_time"])
    return st


def _sectors(s, lap, lap_time):
    """The sector being driven (0-based) and the finished ones' colours: purple = the
    session's best of that sector, green = better than the lap before, yellow = slower."""
    laps = {l.get("lap"): l for l in s.summary.get("laps", [])}
    this = laps.get(lap, {}).get("sectorsMs")
    if not this:
        return None, []
    def split(l):  # elapsed -> each sector's own time
        e = l.get("sectorsMs") or []
        return [e[k] - (e[k - 1] if k else 0) for k in range(len(e))]
    own = split(laps[lap])
    best = {}
    for l in laps.values():
        for k, v in enumerate(split(l)):
            best[k] = min(best.get(k, v), v)
    before = split(laps.get(lap - 1, {}))
    colours, cur = [], len(this)
    for k, end in enumerate(this):
        if lap_time * 1000 < end:
            cur = k
            break
        if own[k] <= best.get(k, own[k]):
            colours.append(PURPLE)
        elif k < len(before) and own[k] < before[k]:
            colours.append(GREEN)
        else:
            colours.append(YELLOW)
    return min(cur, len(this) - 1), colours


# --- syncing ------------------------------------------------------------------------------

def parse_utc(text):
    """ISO 8601 ("2026-10-20T14:03:12.350Z", "2026-10-20T10:03:12-0400") -> UTC seconds"""
    t = text.strip().replace("Z", "+00:00")
    t = re.sub(r"([+-]\d\d)(\d\d)$", r"\1:\2", t)  # -0400 -> -04:00
    d = dt.datetime.fromisoformat(t)
    if d.tzinfo is None:
        d = d.replace(tzinfo=dt.timezone.utc)
    return d.timestamp()


def probe(ffmpeg_stderr):
    """What `ffmpeg -i video` says about it: duration (s), fps, size, and its creation time
    (UTC seconds: an iPhone's com.apple.quicktime.creationdate first, else creation_time)."""
    out = {"duration": None, "fps": None, "size": None, "created": None}
    m = re.search(r"Duration: (\d+):(\d\d):(\d\d(?:\.\d+)?)", ffmpeg_stderr)
    if m:
        out["duration"] = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))
    m = re.search(r"Video: .*?(\d{2,5})x(\d{2,5})", ffmpeg_stderr)
    if m:
        out["size"] = (int(m.group(1)), int(m.group(2)))
    m = re.search(r"([\d.]+) fps", ffmpeg_stderr)
    if m:
        out["fps"] = float(m.group(1))
    m = re.search(r"com\.apple\.quicktime\.creationdate\s*:\s*(\S+)", ffmpeg_stderr) or \
        re.search(r"creation_time\s*:\s*(\S+)", ffmpeg_stderr)
    if m:
        out["created"] = parse_utc(m.group(1))
    return out


def video_start(args, session, info, video_fps):
    """The GPS time (UTC seconds) of the video's first frame."""
    if args.start:
        start = parse_utc(args.start)
    elif args.sync_frame is not None:
        flashes = session.summary.get("syncFlashesUtc") or []
        if not flashes:
            sys.exit("overlay: --sync-frame needs a SYNC flash in the summary (tap the summary's top bar while filming)")
        if not video_fps:
            sys.exit("overlay: --sync-frame needs the video's frame rate (give --video, or --video-fps)")
        start = parse_utc(flashes[args.sync_index]) - args.sync_frame / video_fps
    elif args.sync == "camera":
        starts = session.summary.get("cameraStartsUtc") or ([session.summary["cameraStartUtc"]] if "cameraStartUtc" in session.summary else [])
        if not starts:
            sys.exit("overlay: the summary has no cameraStartUtc (a DJI camera with Track Rat as its GPS remote)")
        start = parse_utc(starts[args.camera_start])
    elif args.sync == "creation":
        if not info or info.get("created") is None:
            sys.exit("overlay: no creation time in the video")
        start = info["created"]
    else:
        start = session.t[0]  # no video: from the start of the log
    return start + args.nudge


# --- drawing --------------------------------------------------------------------------------

def load_font(size, weight="semibold"):
    from PIL import ImageFont
    for name in (f"inter-{weight}.woff2", "inter-semibold.woff2"):
        p = os.path.join(FONTS, name)
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except OSError:
                pass
    return ImageFont.load_default(size)


def fmt_lap(t):
    if t is None:
        return "--:--.-"
    m, sec = divmod(max(0.0, t), 60)
    return f"{int(m)}:{sec:04.1f}"


def fmt_delta(d):
    return "" if d is None else f"{'+' if d >= 0 else '−'}{abs(d):.2f}"


class Overlay:
    """Draws frames. The parts that never change (the map's track, the panels, the
    g circle's rings) are drawn once; each frame copies them and adds the rest."""

    def __init__(self, session, size, units="kmh"):
        from PIL import Image, ImageDraw
        self.s, self.w, self.h, self.units = session, size[0], size[1], units
        self.laps = {l.get("lap"): l for l in session.summary.get("laps", [])}
        k = self.h / 1080.0
        self.k = k
        self.f_big = load_font(int(130 * k))
        self.f_mid = load_font(int(64 * k))
        self.f_small = load_font(int(34 * k), "medium")
        pad = int(40 * k)
        self.pad = pad
        # the map: top right
        self.map_box = (self.w - pad - int(420 * k), pad, self.w - pad, pad + int(420 * k))
        self.g_box = (self.w - pad - int(260 * k), self.h - pad - int(260 * k), self.w - pad, self.h - pad)
        self.base = Image.new("RGBA", (self.w, self.h), (0, 0, 0, 0))
        dr = ImageDraw.Draw(self.base)
        self._map_points = []
        if session.t:
            lat0, lon0 = session.lat[0], session.lon[0]
            idx = [i for i in range(len(session.t)) if session.lap[i] == session.best_lap] if session.best_lap is not None else []
            if not idx:
                idx = list(range(len(session.t)))
            xy = [local_xy(session.lat[i], session.lon[i], lat0, lon0) for i in idx]
            xs, ys = [p[0] for p in xy], [p[1] for p in xy]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            bx0, by0, bx1, by1 = self.map_box
            inner = int(24 * k)
            scale = min((bx1 - bx0 - 2 * inner) / max(x1 - x0, 1), (by1 - by0 - 2 * inner) / max(y1 - y0, 1))
            cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
            mx, my = (x0 + x1) / 2, (y0 + y1) / 2
            self.to_px = lambda x, y: (cx + (x - mx) * scale, cy - (y - my) * scale)
            self.lat0, self.lon0 = lat0, lon0
            dr.rounded_rectangle(self.map_box, radius=int(24 * k), fill=BG)
            self._map_points = [self.to_px(x, y) for x, y in xy]
            dr.line(self._map_points, fill=TEXT2, width=max(2, int(6 * k)), joint="curve")
        # top left: lap and times; bottom left: speed
        dr.rounded_rectangle((pad, pad, pad + int(560 * k), pad + int(250 * k)), radius=int(24 * k), fill=BG)
        dr.rounded_rectangle((pad, self.h - pad - int(220 * k), pad + int(420 * k), self.h - pad), radius=int(24 * k), fill=BG)
        # the g circle
        dr.rounded_rectangle(self.g_box, radius=int(24 * k), fill=BG)
        gx, gy, gr = self._g_geom()
        for frac in (0.5, 1.0):
            dr.ellipse((gx - gr * frac, gy - gr * frac, gx + gr * frac, gy + gr * frac), outline=LINE, width=max(1, int(3 * k)))
        dr.line((gx - gr, gy, gx + gr, gy), fill=LINE, width=1)
        dr.line((gx, gy - gr, gx, gy + gr), fill=LINE, width=1)
        if session.engine:
            ex0 = pad + int(460 * k)
            dr.rounded_rectangle((ex0, self.h - pad - int(220 * k), ex0 + int(560 * k), self.h - pad), radius=int(24 * k), fill=BG)
        self.last_key, self.last_frame = None, None

    def _g_geom(self):
        x0, y0, x1, y1 = self.g_box
        return (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2 - int(30 * self.k)

    def key(self, st):
        """What a frame shows, rounded as drawn: the same key, the same frame (no redraw)."""
        if st is None:
            return None
        e = tuple(round(v) for v in st["engine"].values())
        return (round(st["kmh"]), st["lap"], fmt_lap(st["lap_time"]), fmt_delta(st["delta"]), round(st["lat"], 6),
                round(st["lon"], 6), round(st["glat"], 2), round(st["glong"], 2), e, st["sector"], tuple(st["sectors"]))

    def frame(self, st):
        """The overlay image for a state (from state_at), and what it wrote, for checking."""
        from PIL import ImageDraw
        img = self.base.copy()
        drawn = {}
        if st is None:
            return img, drawn
        dr = ImageDraw.Draw(img)
        k, pad = self.k, self.pad
        # speed
        v = st["kmh"] / KMH_PER_MPH if self.units == "mph" else st["kmh"]
        drawn["speed"] = f"{round(v)}"
        drawn["unit"] = "mph" if self.units == "mph" else "km/h"
        sy = self.h - pad - int(220 * k)
        dr.text((pad + int(30 * k), sy + int(20 * k)), drawn["speed"], font=self.f_big, fill=TEXT)
        dr.text((pad + int(30 * k), self.h - pad - int(56 * k)), drawn["unit"], font=self.f_small, fill=TEXT2)
        # lap, lap time, delta
        drawn["lap"] = f"LAP {st['lap']}" if st["lap"] > 0 else "OUT LAP"
        drawn["lap_time"] = fmt_lap(st["lap_time"])
        drawn["delta"] = fmt_delta(st["delta"])
        dr.text((pad + int(30 * k), pad + int(20 * k)), drawn["lap"], font=self.f_small, fill=TEXT2)
        dr.text((pad + int(30 * k), pad + int(60 * k)), drawn["lap_time"], font=self.f_mid, fill=TEXT)
        if drawn["delta"]:
            colour = GREEN if st["delta"] < 0 else RED
            dr.text((pad + int(300 * k), pad + int(60 * k)), drawn["delta"], font=self.f_mid, fill=colour)
            drawn["delta_colour"] = colour
        # sectors: a box each, coloured once done, outlined while driven
        if st["sector"] is not None:
            n = len(self.laps[st["lap"]]["sectorsMs"])
            bw = (int(500 * k) - (n - 1) * int(8 * k)) / max(n, 1)
            by = pad + int(180 * k)
            for i in range(n):
                x = pad + int(30 * k) + i * (bw + int(8 * k))
                box = (x, by, x + bw, by + int(40 * k))
                if i < len(st["sectors"]):
                    dr.rounded_rectangle(box, radius=int(8 * k), fill=st["sectors"][i])
                else:
                    dr.rounded_rectangle(box, radius=int(8 * k), outline=TEXT if i == st["sector"] else LINE, width=max(1, int(3 * k)))
            drawn["sectors"] = list(st["sectors"])
        # the car on the map
        if self._map_points:
            x, y = local_xy(st["lat"], st["lon"], self.lat0, self.lon0)
            px, py = self.to_px(x, y)
            r = int(14 * k)
            dr.ellipse((px - r, py - r, px + r, py + r), fill=BLUE, outline=TEXT, width=max(1, int(3 * k)))
            drawn["car"] = (round(px), round(py))
        # g: right = turning right, up = braking (the body is thrown forward)
        gx, gy, gr = self._g_geom()
        cap = 1.5
        px = gx + max(-cap, min(cap, st["glat"])) / cap * gr
        py = gy + max(-cap, min(cap, st["glong"])) / cap * gr
        r = int(12 * k)
        dr.ellipse((px - r, py - r, px + r, py + r), fill=YELLOW)
        drawn["g"] = f"{math.hypot(st['glat'], st['glong']):.1f} g"
        dr.text((self.g_box[0] + int(16 * k), self.g_box[1] + int(10 * k)), drawn["g"], font=self.f_small, fill=TEXT)
        # engine
        if st["engine"]:
            ex0 = pad + int(490 * k)
            y = self.h - pad - int(200 * k)
            rpm = st["engine"].get("RPM")
            thr = st["engine"].get("Throttle")
            bar_w = int(500 * k)
            if rpm is not None:
                drawn["rpm"] = f"{round(rpm)}"
                dr.text((ex0, y), f"{drawn['rpm']} rpm", font=self.f_small, fill=TEXT)
                frac = max(0.0, min(1.0, rpm / 8000.0))
                dr.rounded_rectangle((ex0, y + int(45 * k), ex0 + bar_w, y + int(75 * k)), radius=int(6 * k), outline=LINE)
                dr.rounded_rectangle((ex0, y + int(45 * k), ex0 + bar_w * frac, y + int(75 * k)), radius=int(6 * k),
                                     fill=RED if frac > 0.85 else GREEN)
            if thr is not None:
                drawn["throttle"] = f"{round(thr)}%"
                dr.text((ex0, y + int(90 * k)), f"Throttle {drawn['throttle']}", font=self.f_small, fill=TEXT)
                dr.rounded_rectangle((ex0, y + int(135 * k), ex0 + bar_w, y + int(160 * k)), radius=int(6 * k), outline=LINE)
                dr.rounded_rectangle((ex0, y + int(135 * k), ex0 + bar_w * max(0.0, min(1.0, thr / 100.0)), y + int(160 * k)),
                                     radius=int(6 * k), fill=GREEN)
            cool = st["engine"].get("CoolantTemp")
            if cool is not None and cool > 0:
                drawn["coolant"] = f"{round(cool)}°C"
                dr.text((ex0 + int(330 * k), y), drawn["coolant"], font=self.f_small, fill=RED if cool >= 110 else TEXT2)
        return img, drawn

    def frame_bytes(self, st):
        """RGBA bytes for ffmpeg; a frame showing the same as the last isn't drawn again."""
        key = self.key(st)
        if key is not None and key == self.last_key:
            return self.last_frame, False
        img, _ = self.frame(st)
        self.last_key, self.last_frame = key, img.tobytes()
        return self.last_frame, True


# --- ffmpeg -------------------------------------------------------------------------------------

def ffmpeg_exe():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        exe = shutil.which("ffmpeg")
        if not exe:
            sys.exit("overlay: needs ffmpeg: pip install -r tools/requirements-overlay.txt, or ffmpeg on the PATH")
        return exe


def probe_video(path):
    r = subprocess.run([ffmpeg_exe(), "-hide_banner", "-i", path], capture_output=True, text=True, errors="replace")
    return probe(r.stderr)


def encoder_args(out, size, fps, burn):
    w, h = size
    raw = ["-f", "rawvideo", "-pix_fmt", "rgba", "-s", f"{w}x{h}", "-r", f"{fps:g}", "-i", "-"]
    if burn:
        return ["-i", burn, *raw, "-filter_complex", "[0:v][1:v]overlay=shortest=1:format=auto[v]", "-map", "[v]",
                "-map", "0:a?", "-c:a", "copy", "-c:v", "libx264", "-crf", "18", "-preset", "medium", "-pix_fmt", "yuv420p", out]
    if out.lower().endswith(".webm"):
        return [*raw, "-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-b:v", "0", "-crf", "30", "-row-mt", "1", out]
    return [*raw, "-c:v", "prores_ks", "-profile:v", "4444", "-pix_fmt", "yuva444p10le", "-vendor", "apl0", out]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("vbo", help="the session's .vbo (its .json next to it is read too)")
    ap.add_argument("-o", "--out", help="output file (default: next to the .vbo: _overlay.mov, or _overlay.mp4 with --burn)")
    ap.add_argument("--burn", metavar="VIDEO", help="draw the overlay onto this video")
    ap.add_argument("--video", help="the video it's for (its length, frame rate, size and creation time), without burning")
    ap.add_argument("--sync", choices=["camera", "creation", "log"], default=None)
    ap.add_argument("--camera-start", type=int, default=0, help="with --sync camera: which time out of the pits (0 = first)")
    ap.add_argument("--sync-frame", type=int, help="the video frame where the SYNC flash first shows")
    ap.add_argument("--sync-index", type=int, default=0, help="with --sync-frame: which SYNC flash in the summary")
    ap.add_argument("--start", help="the GPS time of the video's first frame (ISO 8601)")
    ap.add_argument("--nudge", type=float, default=0.0, help="seconds to add to the start (+ = the data later)")
    ap.add_argument("--fps", type=float, default=30.0, help="overlay frame rate (default 30; with --burn, the video's)")
    ap.add_argument("--video-fps", type=float, help="the video's frame rate, if it can't be read")
    ap.add_argument("--size", default="1920x1080", help="overlay size without a video (default 1920x1080)")
    ap.add_argument("--duration", type=float, help="seconds to render (default: the video's, or the whole log)")
    ap.add_argument("--units", choices=["kmh", "mph"], default="kmh")
    args = ap.parse_args(argv)

    with open(args.vbo, encoding="utf-8", errors="replace") as f:
        text = f.read()
    summary = {}
    js = os.path.splitext(args.vbo)[0] + ".json"
    if os.path.exists(js):
        with open(js, encoding="utf-8") as f:
            summary = json.load(f)
    session = read_session(text, summary)
    if not session.t:
        sys.exit("overlay: no fixes in that log")

    video = args.burn or args.video
    info = probe_video(video) if video else None
    size = info["size"] if info and info["size"] else tuple(int(v) for v in args.size.split("x"))
    video_fps = args.video_fps or (info and info["fps"])
    fps = video_fps if args.burn and video_fps else args.fps
    if args.sync is None and args.sync_frame is None and not args.start:
        args.sync = "camera" if ("cameraStartUtc" in summary and video) else "creation" if video else "log"
    start = video_start(args, session, info, video_fps)
    duration = args.duration or (info and info["duration"]) or (session.t[-1] - start)
    out = args.out or os.path.splitext(args.vbo)[0] + ("_overlay.mp4" if args.burn else "_overlay.mov")

    overlay = Overlay(session, size, args.units)
    frames = int(duration * fps)
    print(f"overlay: {frames} frames ({duration:.1f} s at {fps:g} fps, {size[0]}x{size[1]}), video starts at "
          f"{dt.datetime.fromtimestamp(start, dt.timezone.utc).isoformat()} -> {out}")
    proc = subprocess.Popen([ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y", *encoder_args(out, size, fps, args.burn)],
                            stdin=subprocess.PIPE)
    t0, drawn = time.time(), 0
    try:
        for n in range(frames):
            data, new = overlay.frame_bytes(state_at(session, start + n / fps))
            drawn += new
            proc.stdin.write(data)
            if n and n % int(fps * 10) == 0:
                rate = n / (time.time() - t0)
                print(f"  {n}/{frames} frames, {rate:.1f} a second, about {(frames - n) / rate:.0f} s to go "
                      f"({drawn} drawn, the rest repeats)", flush=True)
    finally:
        proc.stdin.close()
        proc.wait()
    if proc.returncode:
        sys.exit(f"overlay: ffmpeg failed ({proc.returncode})")
    print(f"overlay: done in {time.time() - t0:.0f} s -> {out}")


if __name__ == "__main__":
    main()
