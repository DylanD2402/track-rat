"""
Shared code of tools/fetch_canada_tracks.py and tools/fetch_world_tracks.py:
which OpenStreetMap things are tracks, how raceway ways are grouped into venues,
the Overpass download (mirrors, retries, backoff, a cache) and the start/finish
estimate. Not run on its own.

Only uses the Python standard library.
"""

import json
import math
import os
import re
import sys
import time
import urllib.error
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fetch_track_map import OVERPASS_URLS, _post, simplify, to_local  # noqa: E402,F401

DIRT = {"dirt", "gravel", "ground", "sand", "grass", "earth", "mud", "unpaved", "compacted", "fine_gravel"}
VENUE_RADIUS_M = 2000      # raceway ways within this of a venue belong to it
EXISTING_RADIUS_M = 1000   # skip tracks this close to one already in tracks.json


# --- small helpers ------------------------------------------------------------

def dist_m(a, b):
    x, y = to_local(b[0], b[1], a[0], a[1])
    return math.hypot(x, y)


def line_len_m(pts):
    return sum(dist_m(pts[i - 1], pts[i]) for i in range(1, len(pts)))


def slug(name):
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return s[:40] or "track"


def is_dirt(tags):
    return tags.get("surface", "") in DIRT or tags.get("sport", "") in ("motocross", "dirt_bike")


def is_drag(tags):
    return (tags.get("raceway", "") in ("drag_strip", "dragstrip", "drag")
            or "drag" in tags.get("name", "").lower() or "drag" in tags.get("sport", "").lower())


def is_kart(tags):
    return "karting" in tags.get("sport", "")


_PIT_NAME = re.compile(r"\bpits?\b|pit[ _-]?lane|pitlane|boxengasse|corsia box|\bboxen\b", re.I)


def is_pit_tags(tags):
    """A pit lane: service or raceway says pit, or "pit"/"pit lane"/"Boxengasse" is in the name
    (as a word: "Pitt Race" isn't one)."""
    return ("pit" in tags.get("service", "").lower() or "pit" in tags.get("raceway", "").lower()
            or bool(_PIT_NAME.search(tags.get("name", ""))))


def _is_minor_pit(tags):
    n = tags.get("name", "").lower()
    return any(w in n for w in ("support", "paddock", "service", "kart")) or is_kart(tags)


def country_query(cc, timeout=300, places=False):
    """The Overpass query for a country's venues and raceway ways (ISO 3166-1 alpha-2 code).
    With places, the country's cities and towns come too (to name where a track is)."""
    town = '\nnode["place"~"^(city|town)$"]["name"](area.ca);\nout center tags qt;' if places else ""
    return f"""[out:json][timeout:{timeout}];
area["ISO3166-1"="{cc}"][admin_level=2]->.ca;
(
  nwr["leisure"="track"]["sport"~"motor|karting"](area.ca);
  nwr["leisure"~"sports_centre|stadium"]["sport"~"motor|karting"](area.ca);
)->.venues;
.venues out center tags;
way["highway"="raceway"](area.ca);
out geom tags;{town}"""


# --- Overpass ---------------------------------------------------------------------

class OverpassError(Exception):
    pass


def fetch_overpass(query, mirrors=None, timeout=900, rounds=3, post=_post, sleep=time.sleep, log=print):
    """Ask the Overpass mirrors until one answers. A 429 or 504 (or 502/503), a timed-out
    answer or a network error moves on to the next mirror; after each full round of mirrors
    it waits longer (30 s, 60 s, ... up to 5 minutes, or the server's Retry-After)."""
    mirrors = mirrors or OVERPASS_URLS
    data = urllib.parse.urlencode({"data": query}).encode()
    wait = 30
    for rnd in range(rounds):
        retry_after = 0
        for url in mirrors:
            try:
                log(f"  asking {url} ...")
                doc = json.loads(post(url, data, timeout))
                remark = str(doc.get("remark", ""))
                if "runtime error" in remark or "out of memory" in remark.lower():
                    raise OverpassError(remark)  # the server gave up part way: the answer is partial
                return doc
            except urllib.error.HTTPError as e:
                log(f"  {url}: HTTP {e.code}")
                if e.code in (429, 502, 503, 504):
                    try:
                        retry_after = max(retry_after, int(e.headers.get("Retry-After", 0)))
                    except Exception:
                        pass
                sleep(5)
            except Exception as e:
                log(f"  {url}: {e}")
                sleep(5)
        if rnd + 1 < rounds:
            pause = min(300, max(wait, retry_after))
            log(f"  every mirror failed; waiting {pause} s")
            sleep(pause)
            wait *= 2
    raise OverpassError("no Overpass mirror answered")


def cached_fetch(cache_path, query, refresh=False, offline=False, **kw):
    """The Overpass answer for query, from cache_path when it's there (unless refresh), else
    downloaded and saved. Returns (doc, from_cache), or (None, False) when offline and uncached."""
    if cache_path and os.path.exists(cache_path) and not refresh:
        with open(cache_path, encoding="utf-8") as f:
            return json.load(f), True
    if offline:
        return None, False
    doc = fetch_overpass(query, **kw)
    if cache_path:
        os.makedirs(os.path.dirname(cache_path) or ".", exist_ok=True)
        tmp = cache_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(doc, f, separators=(",", ":"))
        os.replace(tmp, cache_path)
    return doc, False


# --- grouping ---------------------------------------------------------------------------

def group_venues(doc, include_dirt=False, include_drag=False, karting=True,
                 name_of=lambda tags: tags.get("name"), loose_unnamed=None):
    """Venues (leisure=track and friends) with the raceway ways that belong to them.
    Returns [{"name", "tags", "center": [lat, lon], "ways": [{"tags", "pts", "id"}], "refs": [(type, id)]}].
    A raceway way with no venue near it and a name becomes a venue of its own (with the other
    ways of that name nearby); with no name it goes into loose_unnamed, if a list is given."""
    venues, ways = [], []
    for el in doc.get("elements", []):
        tags = el.get("tags", {})
        if el.get("type") == "way" and tags.get("highway") == "raceway" and el.get("geometry"):
            ways.append({"tags": tags, "pts": [[g["lat"], g["lon"]] for g in el["geometry"]], "id": el.get("id")})
            continue
        c = el.get("center") or ({"lat": el["lat"], "lon": el["lon"]} if "lat" in el else None)
        if c and name_of(tags):
            venues.append({"name": name_of(tags), "tags": tags, "center": [c["lat"], c["lon"]], "ways": [],
                           "refs": [(el.get("type"), el.get("id"))]})

    def keep(tags):
        if not include_dirt and is_dirt(tags):
            return False
        if not include_drag and is_drag(tags):
            return False
        if not karting and "karting" in tags.get("sport", ""):
            return False
        return True

    venues = [v for v in venues if keep(v["tags"])]
    # Merge duplicate venues (same name nearby, e.g. a node and an area)
    merged = []
    for v in venues:
        dup = next((m for m in merged if m["name"] == v["name"] and dist_m(m["center"], v["center"]) < VENUE_RADIUS_M), None)
        if not dup:
            merged.append(v)
        else:
            dup["refs"] += v["refs"]
    venues = merged

    # Attach each raceway way to the nearest venue
    loose = []
    for w in ways:
        if not keep(w["tags"]):
            continue
        mid = w["pts"][len(w["pts"]) // 2]
        best = min(venues, key=lambda v: dist_m(v["center"], mid), default=None)
        if best and dist_m(best["center"], mid) < VENUE_RADIUS_M:
            best["ways"].append(w)
        else:
            loose.append(w)

    # Named raceways with no venue around them become tracks of their own
    for w in loose:
        name = name_of(w["tags"])
        if not name:
            if loose_unnamed is not None:
                loose_unnamed.append(w)
            continue
        mid = w["pts"][len(w["pts"]) // 2]
        v = next((v for v in venues if v["name"] == name and dist_m(v["center"], mid) < VENUE_RADIUS_M), None)
        if v:
            v["ways"].append(w)
        else:
            venues.append({"name": name, "tags": w["tags"], "center": mid, "ways": [w], "refs": []})
    return venues


# --- start/finish estimate -------------------------------------------------------------------

def bearing_deg(dx, dy):
    return math.degrees(math.atan2(dx, dy)) % 360


def _cum(xy):
    out = [0.0]
    for i in range(1, len(xy)):
        out.append(out[-1] + math.hypot(xy[i][0] - xy[i - 1][0], xy[i][1] - xy[i - 1][1]))
    return out


def _point_at(xy, cum, s):
    s = max(0.0, min(cum[-1], s))
    for i in range(1, len(xy)):
        if cum[i] >= s:
            seg = cum[i] - cum[i - 1]
            u = (s - cum[i - 1]) / seg if seg > 1e-9 else 0.0
            return (xy[i - 1][0] + (xy[i][0] - xy[i - 1][0]) * u, xy[i - 1][1] + (xy[i][1] - xy[i - 1][1]) * u)
    return xy[-1]


def estimate_start_finish(circuit, pits, span=15.0, align_with_pit=True):
    """Where the start/finish line probably is, from the circuit's ways (lists of [lat, lon]) and
    its pit lane ways: the point on the circuit closest to the middle of the (longest) pit lane,
    with the heading of the circuit there. OpenStreetMap draws a way in its direction of travel
    when it's one-way, but the circuit's direction isn't recorded: the way's own direction is
    used, unless the pit lane (drawn entry to exit) points the other way, then the opposite.
    Returns {"lat", "lon", "heading"} or None (no pit lane or no circuit)."""
    pits = [p for p in pits if len(p) >= 2]
    circuit = [c for c in circuit if len(c) >= 2]
    if not pits or not circuit:
        return None
    pit = max(pits, key=line_len_m)
    ref = pit[0]
    pxy = [to_local(p[0], p[1], *ref) for p in pit]
    pc = _cum(pxy)
    mid = _point_at(pxy, pc, pc[-1] / 2)
    best = None
    for wi, way in enumerate(circuit):
        xy = [to_local(p[0], p[1], *ref) for p in way]
        for i in range(1, len(xy)):
            ax, ay = xy[i - 1]
            dx, dy = xy[i][0] - ax, xy[i][1] - ay
            L2 = dx * dx + dy * dy
            u = max(0.0, min(1.0, ((mid[0] - ax) * dx + (mid[1] - ay) * dy) / L2)) if L2 > 1e-9 else 0.0
            qx, qy = ax + dx * u, ay + dy * u
            d = math.hypot(qx - mid[0], qy - mid[1])
            if best is None or d < best[0]:
                best = (d, wi, i, u, qx, qy)
    d, wi, i, u, qx, qy = best
    xy = [to_local(p[0], p[1], *ref) for p in circuit[wi]]
    cum = _cum(xy)
    s = cum[i - 1] + (cum[i] - cum[i - 1]) * u
    a = _point_at(xy, cum, s - span)
    b = _point_at(xy, cum, s + span)
    if math.hypot(b[0] - a[0], b[1] - a[1]) < 1e-6:
        return None
    heading = bearing_deg(b[0] - a[0], b[1] - a[1])
    if align_with_pit and pc[-1] > 1:
        ph = bearing_deg(pxy[-1][0] - pxy[0][0], pxy[-1][1] - pxy[0][1])
        if abs((heading - ph + 180) % 360 - 180) > 90:
            heading = (heading + 180) % 360
    lat = ref[0] + math.degrees(qy / 6371000.0)
    lon = ref[1] + math.degrees(qx / (6371000.0 * math.cos(math.radians(ref[0]))))
    return {"lat": round(lat, 6), "lon": round(lon, 6), "heading": round(heading, 1)}


def estimate_from_ways(circuit_ways, pit_ways, **kw):
    """estimate_start_finish for way dicts ({"tags", "pts"}): support, paddock and kart pit lanes
    only count when the venue has no proper pit lane, and kart raceways aren't the circuit when
    there are others."""
    pits = [w for w in pit_ways if not _is_minor_pit(w["tags"])] or pit_ways
    circuit = [w for w in circuit_ways if not is_kart(w["tags"])] or circuit_ways
    return estimate_start_finish([w["pts"] for w in circuit], [w["pts"] for w in pits], **kw)
