#!/usr/bin/env python3
"""
Add or update a track in data/tracks.json, including its layout for the
map screen.

The layout comes from OpenStreetMap (every way tagged highway=raceway near
the start/finish), or from a GeoJSON / GPX / KML file you export yourself.

Example (Calabogie):
    python tools/fetch_track_map.py --id calabogie \
        --name "Calabogie Motorsports Park" --location "Calabogie, ON" \
        --country Canada --length 5050 --sf 45.30xxxx,-76.67xxxx

    --sf is the start/finish line. Get it from Google Maps: right-click the
    start/finish line on the satellite view and click the coordinates to copy.

Then copy data/tracks.json to the root of the SD card.

Only uses the Python standard library.
"""

import argparse
import json
import math
import os
import sys
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

OVERPASS_URLS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]
OSM_API_MAP = "https://api.openstreetmap.org/api/0.6/map"
EARTH_R = 6371000.0
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DB = os.path.join(REPO_ROOT, "data", "tracks.json")


# --- geometry -------------------------------------------------------------

def to_local(lat, lon, ref_lat, ref_lon):
    x = math.radians(lon - ref_lon) * EARTH_R * math.cos(math.radians(ref_lat))
    y = math.radians(lat - ref_lat) * EARTH_R
    return x, y


def simplify(points, tol_m, ref):
    """Douglas-Peucker on [lat, lon] points, tolerance in metres."""
    if len(points) < 3:
        return points
    xy = [to_local(p[0], p[1], *ref) for p in points]
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = xy[a]
        bx, by = xy[b]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy)
        worst, worst_i = -1.0, -1
        for i in range(a + 1, b):
            px, py = xy[i]
            d = abs(dx * (ay - py) - dy * (ax - px)) / L if L > 1e-9 else math.hypot(px - ax, py - ay)
            if d > worst:
                worst, worst_i = d, i
        if worst > tol_m:
            keep[worst_i] = True
            stack += [(a, worst_i), (worst_i, b)]
    return [p for p, k in zip(points, keep) if k]


# --- sources --------------------------------------------------------------

def _post(url, data, timeout):
    req = urllib.request.Request(url, data=data, headers={"User-Agent": "track-rat/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def _get(url, timeout):
    req = urllib.request.Request(url, headers={"User-Agent": "track-rat/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def fetch_osm(lat, lon, radius_m):
    """Overpass mirrors first (2 tries each), then the main OpenStreetMap API."""
    query = f'[out:json][timeout:60];way["highway"="raceway"](around:{radius_m},{lat},{lon});out geom;'
    data = urllib.parse.urlencode({"data": query}).encode()
    for url in OVERPASS_URLS:
        for attempt in range(2):
            try:
                lines = parse_overpass(json.loads(_post(url, data, 90)))
                print(f"  got data from {url}")
                return lines
            except Exception as e:
                print(f"  {url}: {e}", file=sys.stderr)
                if attempt == 0:
                    time.sleep(5)

    # Fallback: the main OSM API returns everything in a bounding box as XML.
    dlat = radius_m / 111194.9
    dlon = radius_m / (111194.9 * math.cos(math.radians(lat)))
    bbox = f"{lon - dlon:.6f},{lat - dlat:.6f},{lon + dlon:.6f},{lat + dlat:.6f}"
    url = f"{OSM_API_MAP}?bbox={bbox}"
    for attempt in range(3):
        try:
            print(f"  trying the main OpenStreetMap API...")
            lines = parse_osm_xml(_get(url, 120))
            print(f"  got data from {OSM_API_MAP}")
            return lines
        except Exception as e:
            print(f"  {OSM_API_MAP}: {e}", file=sys.stderr)
            time.sleep(5)
    raise SystemExit("Could not reach OpenStreetMap. Try again later, or use --file (see README).")


def parse_osm_xml(xml_bytes):
    root = ET.fromstring(xml_bytes)
    nodes = {n.get("id"): [float(n.get("lat")), float(n.get("lon"))] for n in root.iter("node")}
    lines = []
    for w in root.iter("way"):
        tags = {t.get("k"): t.get("v") for t in w.iter("tag")}
        if tags.get("highway") != "raceway":
            continue
        pts = [nodes[nd.get("ref")] for nd in w.iter("nd") if nd.get("ref") in nodes]
        if len(pts) >= 2:
            lines.append((pts, tags.get("name", ""), tags.get("service", "")))
    return lines


def parse_overpass(doc):
    lines = []
    for el in doc.get("elements", []):
        if el.get("type") == "way" and el.get("geometry"):
            tags = el.get("tags", {})
            pts = [[g["lat"], g["lon"]] for g in el["geometry"]]
            lines.append((pts, tags.get("name", ""), tags.get("service", "")))
    return lines


def load_file(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in (".geojson", ".json"):
        doc = json.load(open(path, encoding="utf-8"))
        feats = doc["features"] if doc.get("type") == "FeatureCollection" else [doc]
        lines = []
        for f in feats:
            g = f.get("geometry", f)
            coords = g.get("coordinates", [])
            if g.get("type") == "LineString":
                lines.append(coords)
            elif g.get("type") in ("MultiLineString", "Polygon"):
                lines += coords
        return [([[c[1], c[0]] for c in l], "", "") for l in lines]  # GeoJSON is lon,lat
    if ext == ".gpx":
        root = ET.parse(path).getroot()
        lines = []
        for seg in root.iter():
            if seg.tag.endswith("trkseg") or seg.tag.endswith("rte"):
                pts = [[float(p.get("lat")), float(p.get("lon"))] for p in seg
                       if p.tag.endswith("trkpt") or p.tag.endswith("rtept")]
                if pts:
                    lines.append((pts, "", ""))
        return lines
    if ext == ".kml":
        root = ET.parse(path).getroot()
        lines = []
        for el in root.iter():
            if el.tag.endswith("coordinates") and el.text:
                pts = []
                for tok in el.text.split():
                    lon, lat = tok.split(",")[:2]
                    pts.append([float(lat), float(lon)])
                if len(pts) >= 2:
                    lines.append((pts, "", ""))
        return lines
    raise SystemExit(f"Unsupported file type: {ext} (use .geojson, .gpx or .kml)")


# --- preview --------------------------------------------------------------

def ascii_preview(lines, ref, width=60, height=24):
    xy = [[to_local(p[0], p[1], *ref) for p in l] for l in lines]
    allp = [p for l in xy for p in l] + [(0.0, 0.0)]
    minx, maxx = min(p[0] for p in allp), max(p[0] for p in allp)
    miny, maxy = min(p[1] for p in allp), max(p[1] for p in allp)
    sc = min((width - 1) / max(maxx - minx, 1), (height - 1) / max(maxy - miny, 1) * 2)
    grid = [[" "] * width for _ in range(height)]

    def plot(x, y, ch):
        c = int(round((x - minx) * sc))
        r = int(round((maxy - y) * sc / 2))
        if 0 <= r < height and 0 <= c < width:
            grid[r][c] = ch

    for l in xy:
        for (x0, y0), (x1, y1) in zip(l, l[1:]):
            n = max(1, int(math.hypot(x1 - x0, y1 - y0) * sc))
            for i in range(n + 1):
                plot(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, "#")
    plot(0, 0, "S")
    return "\n".join("".join(r).rstrip() for r in grid).strip("\n"), (maxx - minx, maxy - miny)


# --- main -----------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--id", required=True, help="short id, e.g. calabogie")
    ap.add_argument("--name", required=True)
    ap.add_argument("--location", default="")
    ap.add_argument("--country", default="")
    ap.add_argument("--length", type=float, default=0, help="track length in metres")
    ap.add_argument("--sf", required=True, help="start/finish line as LAT,LON")
    ap.add_argument("--gate", type=float, default=15, help="start/finish gate half-width in metres")
    ap.add_argument("--file", help="read the layout from a .geojson/.gpx/.kml file instead of OpenStreetMap")
    ap.add_argument("--radius", type=int, default=2500, help="OpenStreetMap search radius around the S/F, metres")
    ap.add_argument("--no-pit", action="store_true", help="leave out ways tagged as pit lanes")
    ap.add_argument("--tolerance", type=float, default=1.0, help="simplification tolerance, metres")
    ap.add_argument("--db", default=DEFAULT_DB, help="tracks.json to update")
    args = ap.parse_args()

    sf_lat, sf_lon = (float(v) for v in args.sf.split(","))
    ref = (sf_lat, sf_lon)

    if args.file:
        print(f"Reading layout from {args.file}")
        raw = load_file(args.file)
    else:
        print(f"Fetching raceways within {args.radius} m of {sf_lat}, {sf_lon} from OpenStreetMap...")
        raw = fetch_osm(sf_lat, sf_lon, args.radius)

    if args.no_pit:
        raw = [r for r in raw if r[2] != "pit_lane" and "pit" not in r[1].lower()]
    if not raw:
        raise SystemExit("No track layout found. Check --sf, try a larger --radius, or use --file.")

    lines, n_in = [], 0
    for pts, name, _ in raw:
        n_in += len(pts)
        s = simplify(pts, args.tolerance, ref)
        lines.append([[round(p[0], 6), round(p[1], 6)] for p in s])
    n_out = sum(len(l) for l in lines)

    names = sorted({r[1] for r in raw if r[1]})
    print(f"  {len(lines)} line(s){' (' + ', '.join(names) + ')' if names else ''}, "
          f"{n_in} points simplified to {n_out}")

    preview, (w, h) = ascii_preview(lines, ref)
    print(f"\nPreview ({w:.0f} m x {h:.0f} m, S = start/finish):\n{preview}\n")

    nearest = min(math.hypot(*to_local(p[0], p[1], *ref)) for l in lines for p in l)
    if nearest > 30:
        print(f"WARNING: the start/finish is {nearest:.0f} m from the nearest point of the layout. "
              f"Check --sf.\n")

    if os.path.exists(args.db):
        db = json.load(open(args.db, encoding="utf-8"))
    else:
        db = {"tracks": []}
    entry = {
        "id": args.id,
        "name": args.name,
        "location": args.location,
        "country": args.country,
        "length": args.length,
        "startFinish": {"lat": sf_lat, "lon": sf_lon, "radius": args.gate},
        "map": lines,
    }
    tracks = db.setdefault("tracks", [])
    for i, t in enumerate(tracks):
        if t.get("id") == args.id:
            if "splits" in t:
                entry["splits"] = t["splits"]  # keep any splits already set up
            tracks[i] = entry
            print(f"Updated '{args.id}' in {args.db}")
            break
    else:
        tracks.insert(0, entry)
        print(f"Added '{args.id}' to {args.db}")

    with open(args.db, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=2)
        f.write("\n")
    print(f"tracks.json is now {os.path.getsize(args.db) / 1024:.0f} KB. Copy it to the root of the SD card.")


if __name__ == "__main__":
    main()
