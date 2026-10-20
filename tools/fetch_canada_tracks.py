#!/usr/bin/env python3
"""
Add every motorsport track in Canada from OpenStreetMap to data/tracks.json.

    python tools/fetch_canada_tracks.py
    (then copy data/tracks.json to the SD card)

Each track gets its name, location (for automatic detection within 5 km)
and its layout map. OpenStreetMap doesn't record where the start/finish
line is, so the timer asks you to tap the screen as you cross it the first
time you drive each track, and remembers it.

- Tracks you already have (same id, or within 1 km of one) are left alone,
  except that a karting track gets "kind": "kart" if it has no "kind".
- Karting tracks (OSM sport=karting) are marked "kind": "kart": the timer
  uses narrower lines (8 m) and a shorter minimum lap there.
- Dirt tracks, motocross and drag strips are skipped unless you ask for
  them (--include-dirt, --include-drag). Karting tracks are included unless
  --no-karting.
- --from-json FILE reads a saved Overpass result instead of downloading
  (e.g. an export from https://overpass-turbo.eu using the query this
  script prints with --print-query).

Only uses the Python standard library.
"""

import argparse
import json
import math
import os
import re
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fetch_track_map import DEFAULT_DB, OVERPASS_URLS, _post, simplify, to_local  # noqa: E402

QUERY = """[out:json][timeout:300];
area["ISO3166-1"="CA"][admin_level=2]->.ca;
(
  nwr["leisure"="track"]["sport"~"motor|karting"](area.ca);
  nwr["leisure"~"sports_centre|stadium"]["sport"~"motor|karting"](area.ca);
)->.venues;
.venues out center tags;
way["highway"="raceway"](area.ca);
out geom tags;"""

DIRT = {"dirt", "gravel", "ground", "sand", "grass", "earth", "mud", "unpaved", "compacted", "fine_gravel"}
VENUE_RADIUS_M = 2000      # raceway ways within this of a venue belong to it
EXISTING_RADIUS_M = 1000   # skip tracks this close to one already in tracks.json


def dist_m(a, b):
    x, y = to_local(b[0], b[1], a[0], a[1])
    return math.hypot(x, y)


def line_len_m(pts):
    return sum(dist_m(pts[i - 1], pts[i]) for i in range(1, len(pts)))


def slug(name):
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return s[:40] or "track"


def fetch(retries=2):
    data = urllib.parse.urlencode({"data": QUERY}).encode()
    for url in OVERPASS_URLS:
        for attempt in range(retries):
            try:
                print(f"  asking {url} (this can take a minute)...")
                return json.loads(_post(url, data, 360))
            except Exception as e:
                print(f"  {url}: {e}", file=sys.stderr)
                time.sleep(5)
    sys.exit("Could not reach OpenStreetMap (Overpass). Try again later, or use --from-json (see --help).")


def is_dirt(tags):
    return tags.get("surface", "") in DIRT or tags.get("sport", "") in ("motocross", "dirt_bike")


def is_drag(tags):
    return (tags.get("raceway", "") in ("drag_strip", "dragstrip", "drag")
            or "drag" in tags.get("name", "").lower() or "drag" in tags.get("sport", "").lower())


def is_kart(tags):
    return "karting" in tags.get("sport", "")


def build_tracks(doc, include_dirt=False, include_drag=False, karting=True, tolerance=1.5):
    venues, ways = [], []
    for el in doc.get("elements", []):
        tags = el.get("tags", {})
        if el.get("type") == "way" and tags.get("highway") == "raceway" and el.get("geometry"):
            ways.append({"tags": tags, "pts": [[g["lat"], g["lon"]] for g in el["geometry"]]})
            continue
        c = el.get("center") or ({"lat": el["lat"], "lon": el["lon"]} if "lat" in el else None)
        if c and tags.get("name"):
            venues.append({"name": tags["name"], "tags": tags, "center": [c["lat"], c["lon"]], "ways": []})

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
        name = w["tags"].get("name")
        if not name:
            continue
        mid = w["pts"][len(w["pts"]) // 2]
        v = next((v for v in venues if v["name"] == name and dist_m(v["center"], mid) < VENUE_RADIUS_M), None)
        if v:
            v["ways"].append(w)
        else:
            venues.append({"name": name, "tags": w["tags"], "center": mid, "ways": [w]})

    tracks = []
    for v in venues:
        t = v["tags"]
        lines = [simplify(w["pts"], tolerance, v["center"]) for w in v["ways"]]
        lines = [[[round(p[0], 6), round(p[1], 6)] for p in l] for l in lines if len(l) >= 2]
        racing = [w for w in v["ways"] if "pit" not in (w["tags"].get("service", "") + w["tags"].get("name", "")).lower()]
        length = round(sum(line_len_m(w["pts"]) for w in racing))
        city = t.get("addr:city") or t.get("is_in:city") or ""
        prov = t.get("addr:province") or t.get("addr:state") or ""
        entry = {
            "id": slug(v["name"]),
            "name": v["name"],
            "location": ", ".join(x for x in (city, prov) if x),
            "country": "Canada",
            "length": length,
            "center": {"lat": round(v["center"][0], 7), "lon": round(v["center"][1], 7)},
        }
        if lines:
            entry["map"] = lines
        # A karting venue, or one whose every raceway is a kart track (not a circuit with a kart track beside it)
        if is_kart(t) or (v["ways"] and all(is_kart(w["tags"]) for w in v["ways"]) and "motor" not in t.get("sport", "")):
            entry["kind"] = "kart"  # narrower lines and a shorter minimum lap on the timer
        tracks.append(entry)
    return tracks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default=DEFAULT_DB, help="tracks.json to update")
    ap.add_argument("--from-json", help="use a saved Overpass JSON result instead of downloading")
    ap.add_argument("--include-dirt", action="store_true")
    ap.add_argument("--include-drag", action="store_true")
    ap.add_argument("--no-karting", action="store_true")
    ap.add_argument("--print-query", action="store_true", help="print the Overpass query and exit")
    ap.add_argument("--no-locations", action="store_true",
                    help="don't look up town and province for new tracks (see fill_track_locations.py)")
    args = ap.parse_args()

    if args.print_query:
        print(QUERY)
        return

    if args.from_json:
        doc = json.load(open(args.from_json, encoding="utf-8"))
    else:
        print("Fetching Canadian motorsport tracks from OpenStreetMap...")
        doc = fetch()

    found = build_tracks(doc, args.include_dirt, args.include_drag, not args.no_karting)
    db = json.load(open(args.db, encoding="utf-8")) if os.path.exists(args.db) else {"tracks": []}
    existing = db.setdefault("tracks", [])

    def ref(t):
        sf = t.get("startFinish") or {}
        c = t.get("center") or {}
        lat, lon = sf.get("lat") or c.get("lat"), sf.get("lon") or c.get("lon")
        return [lat, lon] if lat and lon else None

    ids = {t.get("id") for t in existing}
    refs = [(t.get("name", ""), ref(t), t) for t in existing if ref(t)]
    added, skipped, karts = [], [], []
    for t in sorted(found, key=lambda t: t["name"]):
        near = next((e for e in refs if dist_m(e[1], [t["center"]["lat"], t["center"]["lon"]]) < EXISTING_RADIUS_M), None)
        if near:
            skipped.append(f"{t['name']} (you already have '{near[0]}')")
            if t.get("kind") == "kart" and near[2] is not None and "kind" not in near[2]:
                near[2]["kind"] = "kart"
                karts.append(near[0])
            continue
        base, n = t["id"], 2
        while t["id"] in ids:
            t["id"] = f"{base}-{n}"
            n += 1
        ids.add(t["id"])
        refs.append((t["name"], [t["center"]["lat"], t["center"]["lon"]], None))
        existing.append(t)
        added.append(t)

    if added and not args.no_locations:
        print(f"Looking up the town and province of {len(added)} new track(s)...")
        from fill_track_locations import fill_locations
        fill_locations(added, quiet=True)

    with open(args.db, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=1)
        f.write("\n")

    print(f"\nAdded {len(added)} track(s):")
    for t in added:
        pts = sum(len(l) for l in t.get("map", []))
        km = f"{t['length'] / 1000:.1f} km" if t["length"] else "length unknown"
        print(f"  {t['name']:<45} {t['location'] or '-':<25} {km:<16} {'map ' + str(pts) + ' pts' if pts else 'no map'}")
    if skipped:
        print(f"\nSkipped {len(skipped)} already in tracks.json:")
        for s in skipped:
            print(f"  {s}")
    if karts:
        print(f"\nMarked {len(karts)} you already had as kart tracks: {', '.join(karts)}")
    print(f"\n{args.db} is now {os.path.getsize(args.db) / 1024:.0f} KB. Copy it to the root of the SD card.")
    print("None of these have a start/finish line yet: the first time you drive one, the timer asks you")
    print("to tap the screen as you cross it.")


if __name__ == "__main__":
    main()
