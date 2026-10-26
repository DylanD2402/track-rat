#!/usr/bin/env python3
"""
Makes the sample session for the viewer at https://trackrat.ca/analyze/.

A simulated car drives Calabogie: an out-lap, three timed laps (one clearly slower) and an
in-lap, around the map in data/tracks.json. It writes, to site/samples/:

  <YYYYMMDD_HHMMSS>_calabogie.vbo        the log, in the format src/VboFormat.cpp writes
  <YYYYMMDD_HHMMSS>_calabogie.json       the summary, with src/SessionLog.cpp's field names
  <YYYYMMDD_HHMMSS>_calabogie_note.json  a logbook note saying it is simulated
  index.json                             the file names, for the page's "Try a sample session"

The speed comes from the corner curvature (as fast as the grip allows, braking and accelerating
within the car's limits); the log has what the board writes: 20 Hz, the same columns, units and
number formats, the Lap column, g from the GPS speed and heading (as vbo::GForce does), and
engine channels from a simple gear, throttle and temperature model.

Python 3, standard library only. The same output every run (fixed seed):

    python tools/make_sample_session.py
"""

import json
import math
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "site", "samples")

TRACK_ID = "calabogie"
STAMP = "20260926_101530"          # the board's file name: local time (UTC-4 in September)
TZ_MIN = -240
HZ = 20
DS = 2.0                           # metres between points of the path
OUT_LAP_M = 450                    # the out-lap starts from a stop this far before the line
IN_LAP_M = 1000                    # the in-lap ends in the pits this far after it
RADIUS = 15.0                      # start/finish width (m either side), as the board's default
EARTH_R = 6371000.0
DEG = math.pi / 180
G0 = 9.80665

# The car: lateral grip, braking and acceleration (m/s^2), top speed (m/s)
GRIP = 12.6
BRAKE = 11.0
ACCEL = 6.6
VMAX = 61.0                        # 220 km/h
VACC = 80.0                        # the speed where acceleration runs out
# (lap pace, extra time-loss section or None): lap 3 is slower, with a mistake in one corner
PACE = {"out": 0.70, 1: 0.97, 2: 1.0, 3: 0.93, "in": 0.68}


def to_local(lat, lon, ref_lat, ref_lon):
    return ((lon - ref_lon) * DEG * EARTH_R * math.cos(ref_lat * DEG), (lat - ref_lat) * DEG * EARTH_R)


def from_local(x, y, ref_lat, ref_lon):
    return (ref_lat + y / EARTH_R / DEG, ref_lon + x / (EARTH_R * math.cos(ref_lat * DEG)) / DEG)


# --- the track ---------------------------------------------------------------------------

def load_track():
    with open(os.path.join(ROOT, "data", "tracks.json"), encoding="utf-8") as f:
        db = json.load(f)
    t = next(v for v in db["tracks"] if v["id"] == TRACK_ID)
    return t


def resample_loop(pts):
    """Closed polyline [(x, y)] -> points DS metres apart along it, and the loop's length."""
    n = len(pts)
    seg = [math.hypot(pts[(i + 1) % n][0] - pts[i][0], pts[(i + 1) % n][1] - pts[i][1]) for i in range(n)]
    total = sum(seg)
    count = int(round(total / DS))
    step = total / count
    out, i, run = [], 0, 0.0
    for k in range(count):
        target = k * step
        while run + seg[i] < target:
            run += seg[i]
            i += 1
        u = (target - run) / seg[i] if seg[i] else 0
        a, b = pts[i], pts[(i + 1) % n]
        out.append((a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u))
    return out, step


def smooth_loop(vals, sigma_m, step):
    """Gaussian smoothing of a closed series (floats or (x, y) pairs)."""
    n = len(vals)
    half = int(3 * sigma_m / step)
    w = [math.exp(-0.5 * (j * step / sigma_m) ** 2) for j in range(-half, half + 1)]
    tot = sum(w)
    pair = isinstance(vals[0], tuple)
    out = []
    for i in range(n):
        if pair:
            sx = sum(w[j + half] * vals[(i + j) % n][0] for j in range(-half, half + 1))
            sy = sum(w[j + half] * vals[(i + j) % n][1] for j in range(-half, half + 1))
            out.append((sx / tot, sy / tot))
        else:
            out.append(sum(w[j + half] * vals[(i + j) % n] for j in range(-half, half + 1)) / tot)
    return out


class Path:
    """The closed racing line: points, heading and curvature at each, spaced `step` metres."""

    def __init__(self, track):
        sf = track["startFinish"]
        self.ref = (sf["lat"], sf["lon"])
        line = track["map"][0]                     # the circuit; the other lines are pit lane and links
        pts = [to_local(p[0], p[1], *self.ref) for p in line]
        if math.hypot(pts[0][0] - pts[-1][0], pts[0][1] - pts[-1][1]) < 1:
            pts.pop()                              # drawn closed: the last point is the first again
        pts, self.step = resample_loop(pts)
        self.pts = smooth_loop(pts, 14.0, self.step)
        self.n = len(self.pts)
        self.length = self.n * self.step
        n = self.n
        self.heading = []                          # radians, 0 = north, clockwise
        for i in range(n):
            a, b = self.pts[(i - 1) % n], self.pts[(i + 1) % n]
            self.heading.append(math.atan2(b[0] - a[0], b[1] - a[1]))
        k = []                                     # + = turning right
        for i in range(n):
            dh = self.heading[(i + 1) % n] - self.heading[(i - 1) % n]
            dh = (dh + math.pi) % (2 * math.pi) - math.pi
            k.append(dh / (2 * self.step))
        self.curv = smooth_loop(k, 24.0, self.step)

    def at(self, s):
        """Position (x, y) and heading (radians) at distance s (m) along the loop."""
        f = (s / self.step) % self.n
        i = int(f)
        u = f - i
        a, b = self.pts[i], self.pts[(i + 1) % self.n]
        dh = (self.heading[(i + 1) % self.n] - self.heading[i] + math.pi) % (2 * math.pi) - math.pi
        return (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u), self.heading[i] + dh * u

    def nearest(self, x, y):
        return min(range(self.n), key=lambda i: (self.pts[i][0] - x) ** 2 + (self.pts[i][1] - y) ** 2)


# --- the drive ---------------------------------------------------------------------------

class Plan(list):
    """The run as [(from_m, to_m, pace)] segments; origin: where it starts, in metres along the loop."""
    origin = 0.0


def speed_profile(path, plan, rng):
    """plan: the unrolled run as segments [(from_m, to_m, pace)], along the loop from `origin`.
    Returns v (m/s) at every DS along it, from standstill to standstill."""
    total = plan[-1][1]
    n = int(total / DS) + 1
    # Slow, smooth wander in each lap's pace, so no two laps are alike
    wander = smooth_loop([rng.uniform(-1, 1) for _ in range(n)], 60.0, DS)
    scale = 0.02 / max(abs(w) for w in wander)
    lim, pace_at = [], []
    for j in range(n):
        d = j * DS
        pace = plan[-1][2]
        for k, (a, b, p) in enumerate(plan):
            if a <= d < b:
                nxt = plan[k + 1][2] if k + 1 < len(plan) else p
                prv = plan[k - 1][2] if k > 0 else p
                blend = 120.0                      # eased over the line, not a step
                pace = p
                if d - a < blend:
                    pace = prv + (p - prv) * (0.5 + 0.5 * (d - a) / blend)
                elif b - d < blend:
                    pace = p + (nxt - p) * (0.5 - 0.5 * (b - d) / blend)
                break
        pace *= 1 + wander[j] * scale
        pace_at.append(pace)
        k = abs(path.curv[int(round((plan.origin + d) / path.step)) % path.n])
        grip = GRIP * pace
        lim.append(min(VMAX * (0.5 + 0.5 * pace), math.sqrt(grip / max(k, 1e-4))))
    return lim, pace_at


def solve(path, plan, rng):
    lim, pace_at = speed_profile(path, plan, rng)
    n = len(lim)
    # Pit lane speed limit at the start (60 km/h for 120 m) and a stop at both ends
    for j in range(n):
        if j * DS < 120:
            lim[j] = min(lim[j], 60 / 3.6)
    lim[0] = 0.0
    lim[-1] = 0.0
    v = lim[:]
    brake = lambda j: BRAKE * pace_at[j]
    for j in range(n - 2, -1, -1):                 # braking
        v[j] = min(v[j], math.sqrt(v[j + 1] ** 2 + 2 * brake(j) * DS))
    for j in range(n - 1):                         # acceleration, less where the tyres are busy cornering
        j2 = (int(round((plan.origin + j * DS) / path.step))) % path.n
        k = abs(path.curv[j2])
        busy = min(1.0, (v[j] ** 2 * k) / (GRIP * pace_at[j]))
        a = ACCEL * pace_at[j] * max(0.0, 1 - v[j] / VACC) * math.sqrt(max(0.0, 1 - busy ** 2)) + 0.35
        v[j + 1] = min(v[j + 1], math.sqrt(v[j] ** 2 + 2 * a * DS))
    # one more braking pass: acceleration limits can leave a corner entry too fast
    for j in range(n - 2, -1, -1):
        v[j] = min(v[j], math.sqrt(v[j + 1] ** 2 + 2 * brake(j) * DS))
    return v


def timeline(v):
    """Time (s) at each point of the profile."""
    t = [0.0]
    for j in range(len(v) - 1):
        vm = max((v[j] + v[j + 1]) / 2, 0.4)
        t.append(t[-1] + DS / vm)
    return t


# --- the log -----------------------------------------------------------------------------

CHANNELS = ["satellites", "time", "latitude", "longitude", "velocity kmh", "heading", "height", "HDOP", "Lap",
            "Lateral G", "Longitudinal G", "Lap tag", "RPM", "Throttle position", "Coolant temperature",
            "Intake temperature", "Oil temperature"]
UNITS = ["-", "-", "g", "g", "-"]                   # the non-standard channels, then the engine's
COLUMNS = "sats time lat long velocity heading height HDOP Lap LateralG LongitudinalG LapTag RPM Throttle CoolantTemp IntakeTemp OilTemp"


def vbo_time(ms):
    ms %= 86400000
    return "%02d%02d%02d.%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def point_text(lat, lon, radius, heading):
    return "%.7f %.7f %.1f %.1f map" % (lat, lon, radius, heading)


def gate_text(kind, lat, lon, heading_deg, radius, name):
    """A timing line across the track at (lat, lon), as VboFormat's appendGate writes it."""
    h = heading_deg * DEG
    ax, ay = math.cos(h) * radius, -math.sin(h) * radius            # across the direction of travel
    x1, y1 = lat, lon
    la1 = lat + ay / EARTH_R / DEG
    lo1 = lon + ax / (EARTH_R * math.cos(lat * DEG)) / DEG
    la2 = lat - ay / EARTH_R / DEG
    lo2 = lon - ax / (EARTH_R * math.cos(lat * DEG)) / DEG
    return "%s %+.6f %+.6f %+.6f %+.6f ¬ %s" % (kind, -lo1 * 60, la1 * 60, -lo2 * 60, la2 * 60, name)


def lap_time_text(ms):
    return "%d:%06.3f" % (ms // 60000, (ms % 60000) / 1000)


def run():
    rng = random.Random(20260926)
    track = load_track()
    path = Path(track)
    sf_ll = track["startFinish"]
    sx, sy = to_local(sf_ll["lat"], sf_ll["lon"], *path.ref)
    i_line = path.nearest(sx, sy)          # tracks.json's line is ~200 m off this map: the sample's line is on the map
    line_s = i_line * path.step
    line_pos, line_heading = path.at(line_s)
    line_lat, line_lon = from_local(line_pos[0], line_pos[1], *path.ref)
    line_deg = (line_heading / DEG) % 360

    # The run, in metres from where it starts (OUT_LAP_M before the line)
    L = path.length
    plan = Plan()
    plan.origin = line_s - OUT_LAP_M
    plan.append((0, OUT_LAP_M, PACE["out"]))
    for n in (1, 2, 3):
        plan.append((OUT_LAP_M + (n - 1) * L, OUT_LAP_M + n * L, PACE[n]))
    plan.append((OUT_LAP_M + 3 * L, OUT_LAP_M + 3 * L + IN_LAP_M, PACE["in"]))
    # Lap 3's mistake: at the sharpest corner between 30% and 45% of the way round, the driver
    # runs wide and slow
    lo, hi = int(0.30 * path.n), int(0.45 * path.n)
    corner = max(range(lo, hi), key=lambda i: abs(path.curv[(i + int(line_s / path.step)) % path.n]))
    mistake = OUT_LAP_M + 2 * L + corner * path.step

    v = solve(path, plan, rng)
    j0 = int(mistake / DS)
    for j in range(max(j0 - 40, 0), min(j0 + 70, len(v))):
        ease = 1 - abs(j - j0) / 55.0 if abs(j - j0) < 55 else 0.0
        v[j] *= 1 - 0.2 * max(ease, 0.0)
    # (the dip is applied after the passes: re-run them so the speeds stay possible)
    for j in range(len(v) - 2, -1, -1):
        v[j] = min(v[j], math.sqrt(v[j + 1] ** 2 + 2 * BRAKE * 0.955 * DS))
    for j in range(len(v) - 1):
        v[j + 1] = min(v[j + 1], math.sqrt(v[j] ** 2 + 2 * (ACCEL * 0.955 * max(0.0, 1 - v[j] / VACC) + 0.35) * DS))
    t_at = timeline(v)
    total_m = len(v) * DS - DS
    dist_at = lambda j: j * DS

    # When the car crosses the line: the board times it between fixes
    cross_m = [OUT_LAP_M + k * L for k in range(0, 4)]    # start of lap 1, 2, 3 and the end of lap 3
    def time_at_m(m):
        f = m / DS
        j = min(int(f), len(v) - 2)
        u = f - j
        return t_at[j] + (t_at[j + 1] - t_at[j]) * u
    cross_t = [time_at_m(m) for m in cross_m]
    lap_ms = [int(round((cross_t[k + 1] - cross_t[k]) * 1000)) for k in range(3)]

    # Fixes at 20 Hz, from a stop in the pits (10 s of standing before it pulls out)
    lead_s = 3.0
    t_end = t_at[-1] + 3.0
    nfix = int((lead_s + t_end - 3.0) * HZ) + 1
    # started: the local time in STAMP, as UTC, plus the stop at the start
    day, clock = STAMP.split("_")
    y, mo, d = int(day[:4]), int(day[4:6]), int(day[6:])
    start_ms_local = (int(clock[:2]) * 3600 + int(clock[2:4]) * 60 + int(clock[4:])) * 1000
    start_ms = start_ms_local - TZ_MIN * 60000          # UTC ms since midnight (past midnight not reached)
    # Correlated GPS position noise
    ex = ey = 0.0

    def s_of_time(t):
        """Distance run (m) at time t (s) since the run's start; standing still before and after."""
        if t <= 0:
            return 0.0
        if t >= t_at[-1]:
            return total_m
        lo, hi = 0, len(t_at) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if t_at[mid] <= t:
                lo = mid
            else:
                hi = mid
        u = (t - t_at[lo]) / (t_at[hi] - t_at[lo])
        return (lo + u) * DS

    def speed_at(m):
        f = m / DS
        j = min(int(f), len(v) - 2)
        return v[j] + (v[j + 1] - v[j]) * (f - j)

    # Gears: speed (km/h) at the redline
    gear_top = [62, 98, 138, 178, 218, 262]
    gear = 1
    rpm = 900.0
    rpm_f = 900.0
    throttle_f = 0.0
    coolant = 79.0
    intake = 27.0
    rows = []
    laps_fix = {}                    # lap -> [(speed, coolant, fix index)]
    prev = None
    glat = glong = 0.0
    prev_speed = prev_head = None
    prev_head_valid = False
    last_head = line_deg
    for k in range(nfix):
        t = k / HZ
        m = s_of_time(t)
        sp = speed_at(m) if 0 < m < total_m else 0.0
        (x, y_), hd = path.at(plan.origin + m)
        ex = 0.985 * ex + rng.gauss(0, 0.05)
        ey = 0.985 * ey + rng.gauss(0, 0.05)
        lat, lon = from_local(x + ex, y_ + ey, *path.ref)
        kmh = max(0.0, sp * 3.6 + rng.gauss(0, 0.04)) if sp > 0.3 else rng.uniform(0, 0.25)
        if kmh > 6:
            last_head = (hd / DEG + rng.gauss(0, 0.03)) % 360
        head = last_head
        height = 262.0 + 9.0 * math.sin(2 * math.pi * ((plan.origin + m) % L) / L * 2) + 3.0 * math.sin((plan.origin + m) / 90.0)
        # The lap: 0 until the first crossing, then the lap in progress
        if m >= cross_m[3]:
            lap = 4
        elif m >= cross_m[2]:
            lap = 3
        elif m >= cross_m[1]:
            lap = 2
        elif m >= cross_m[0]:
            lap = 1
        else:
            lap = 0
        # g from the speed and heading, as vbo::GForce does (0.2 s smoothing)
        ms_utc = start_ms + int(round(k * 1000 / HZ))
        if prev_speed is not None:
            dts = 1.0 / HZ
            v0, v1 = prev_speed / 3.6, kmh / 3.6
            raw_long = max(-5, min(5, (v1 - v0) / dts / G0))
            raw_lat = 0.0
            if kmh > 6 and prev_head_valid:
                dh = head - prev_head
                while dh > 180:
                    dh -= 360
                while dh < -180:
                    dh += 360
                raw_lat = max(-5, min(5, 0.5 * (v0 + v1) * (dh * DEG) / dts / G0))
            a = dts / (0.2 + dts)
            glong += a * (raw_long - glong)
            glat += a * (raw_lat - glat)
        prev_speed, prev_head, prev_head_valid = kmh, head, kmh > 6
        # Engine: gear, rpm, throttle, temperatures
        acc = (speed_at(min(m + 1.0, total_m)) ** 2 - speed_at(max(m - 1.0, 0)) ** 2) / 4.0 if 0 < m < total_m else 0.0  # m/s^2
        drag = 0.0012 * sp * sp / 3.0
        full = max(ACCEL * (1 - sp / VACC), 0.5)
        thr = max(0.0, min(100.0, (acc + drag + 0.25) / full * 100)) if acc > -drag - 0.4 else 0.0
        if sp < 0.5:
            thr = 0.0
        throttle_f += 0.35 * (thr - throttle_f)
        while gear < 6 and kmh / gear_top[gear - 1] * 7000 > 6600:
            gear += 1
        while gear > 1 and kmh / gear_top[gear - 2] * 7000 < 6000 and kmh / gear_top[gear - 1] * 7000 < 3600:
            gear -= 1
        target = max(900.0, kmh / gear_top[gear - 1] * 7000) if kmh > 8 else 900.0 + throttle_f * 18
        rpm_f += min(0.45, 1.0) * (target - rpm_f)
        rpm = max(850.0, rpm_f + rng.gauss(0, 12))
        target_c = 80.0 + 22.0 * (0.6 * throttle_f / 100 + 0.4 * min(kmh / 200, 1))
        coolant += (target_c - coolant) / 150.0 / HZ
        intake += 0.002 * ((24.0 + 15.0 * min(1.0, 0.4 * throttle_f / 100 + (1 - min(kmh, 160) / 160) * 0.7)) - intake)
        row = (ms_utc, lat, lon, kmh, head, height, lap, glat, glong, kmh < 5.0, rpm, throttle_f, round(coolant), round(intake))
        rows.append(row)
        laps_fix.setdefault(lap, []).append((kmh, round(coolant)))

    # --- the file ---
    stamp_name = STAMP + "_" + TRACK_ID
    first_ms = rows[0][0]
    fm = vbo_time(first_ms)
    out = []
    out.append("File created on %02d/%02d/%04d @ %02d:%02d:%02d\r\n\r\n" % (d, mo, y, first_ms // 3600000, first_ms // 60000 % 60, first_ms // 1000 % 60))
    out.append("[header]\r\n" + "".join(c + "\r\n" for c in CHANNELS))
    out.append("\r\n[channel units]\r\n" + "".join(u + "\r\n" for u in UNITS) + "".join(u + "\r\n" for u in ["rpm", "%", "C", "C", "C"]))
    comments = [
        "Track Rat GPS lap timer",
        "Started: " + STAMP,
        "SIMULATED: this is not a real drive. A simulated car at %s, made by tools/make_sample_session.py" % track["name"],
        "Type: laps",
        "Track: " + TRACK_ID,
        "Track name: " + track["name"],
        "Start/finish: " + point_text(line_lat, line_lon, RADIUS, line_deg),
        "Min lap ms: 20000",
    ]
    out.append("\r\n[comments]\r\n" + "".join(c + "\r\n" for c in comments))
    out.append("\r\n[laptiming]\r\n" + gate_text("Start  ", line_lat, line_lon, line_deg, RADIUS, "StartFinish") + "\r\n")
    out.append("\r\n[column names]\r\n" + COLUMNS + "\r\n\r\n[data]\r\n")
    for (ms_utc, lat, lon, kmh, head, height, lap, glat_, glong_, stopped, rpm_, thr_, cool_, int_) in rows:
        hd = head % 360
        if hd >= 359.995:
            hd = 0
        sats = 19 + int(rng.random() * 3)
        hdop = 0.72 + 0.12 * rng.random()
        line = "%03u %s %012.5f %012.5f %07.3f %06.2f %+09.2f %05.2f %03u %+06.3f %+06.3f %03u" % (
            sats, vbo_time(ms_utc), lat * 60, -lon * 60, min(kmh, 999.999), hd, height, hdop, lap,
            max(-5, min(5, glat_)), max(-5, min(5, glong_)), 1 if stopped else 0)
        line += " %05.0f %05.1f %+06.1f %+06.1f %+06.1f" % (min(rpm_, 99999), min(thr_, 100), cool_, int_, 0.0)
        out.append(line + "\r\n")
    vbo_text = "".join(out)

    # --- the summary ---
    laps = []
    for n in (1, 2, 3):
        fixes = laps_fix[n]
        speeds = [f[0] for f in fixes]
        laps.append({"lap": n, "timeMs": lap_ms[n - 1], "time": lap_time_text(lap_ms[n - 1]),
                     "maxKmh": round(max(speeds), 1), "avgKmh": round(L / (lap_ms[n - 1] / 1000) * 3.6, 1),
                     "distanceM": int(round(L)), "coolantMaxC": max(f[1] for f in fixes)})
    sectors = track.get("sectors") or []
    if sectors:   # corner sectors: elapsed at the end of T1, T2... (the last ends at the line)
        for n, lap_entry in enumerate(laps):
            ends = []
            for sc in sectors:
                px, py = to_local(sc["lat"], sc["lon"], *path.ref)
                s_gate = path.nearest(px, py) * path.step
                rel = (s_gate - line_s) % L
                ends.append(int(round((time_at_m(cross_m[n] + rel) - cross_t[n]) * 1000)))
            ends[-1] = lap_ms[n]
            lap_entry["sectorsMs"] = ends
    best = min(laps, key=lambda l: l["timeMs"])
    summary = {
        "track": TRACK_ID, "trackName": track["name"], "started": STAMP, "log": stamp_name + ".vbo", "finished": True,
        "lapCount": 3, "cleanLaps": 3, "averageMs": int(round(sum(lap_ms) / 3)),
        "bestLap": best["lap"], "bestTimeMs": best["timeMs"], "bestTime": best["time"],
        "topSpeedKmh": round(max(r[3] for r in rows), 1),
        "coolantMaxC": max(r[12] for r in rows),
        "laps": laps,
    }
    note = {"note": "A simulated session: not a real drive. A simulated car at Calabogie, made by tools/make_sample_session.py "
                    "so you can try the analysis. Lap 1 is a warm-up, lap 2 the best, and in lap 3 the driver ran wide at one corner."}

    os.makedirs(OUT_DIR, exist_ok=True)
    names = [stamp_name + ".vbo", stamp_name + ".json", stamp_name + "_note.json"]
    with open(os.path.join(OUT_DIR, names[0]), "w", encoding="utf-8", newline="") as f:
        f.write(vbo_text)
    for name, doc in ((names[1], summary), (names[2], note)):
        with open(os.path.join(OUT_DIR, name), "w", encoding="utf-8", newline="\n") as f:
            json.dump(doc, f, indent=2, ensure_ascii=False)
            f.write("\n")
    with open(os.path.join(OUT_DIR, "index.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"files": names}, f, indent=2)
        f.write("\n")
    print("%s: %d bytes, %d fixes" % (names[0], len(vbo_text.encode("utf-8")), len(rows)))
    for l in laps:
        print("  lap %d  %s  max %.1f km/h" % (l["lap"], l["time"], l["maxKmh"]))
    print("  best lap %d, top speed %.1f km/h, coolant max %d C, lap length %.0f m" %
          (summary["bestLap"], summary["topSpeedKmh"], summary["coolantMaxC"], L))


if __name__ == "__main__":
    run()
