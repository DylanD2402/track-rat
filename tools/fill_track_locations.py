#!/usr/bin/env python3
"""
Fill in "Town, Province" for every Canadian track in tracks.json.

    python tools/fill_track_locations.py            # tracks with no province yet
    python tools/fill_track_locations.py --all      # redo every Canadian track
    python tools/fill_track_locations.py --dry-run  # show what would change

Where each track is comes from its start/finish (or "center"). Two OpenStreetMap
services name the place, with no keys or extra packages:

- Nominatim (reverse geocoding) gives the province, and the city when the track
  is in one. It's asked once a second, as its usage policy requires.
- Photon (Komoot's OpenStreetMap geocoder) lists the cities, towns, villages
  and hamlets near each track. If Photon can't be reached, Overpass is asked
  instead.

The town, in order of preference:
1. the town already in tracks.json, when it came from the track's own
   OpenStreetMap address and isn't normalised yet ("Toronto, Ontario")
2. a nearby place the track is named after (Shannonville Motorsports Park)
3. the city, town or village Nominatim says the track is in
4. a place right next to the track, within 3 km (Calabogie), skipping
   numbered and two-name reserve names ("Okanagan 1")
5. the nearest town or city, then the nearest village

Nominatim alone names only the township or rural municipality ("Greater
Madawaska" rather than Calabogie). Locations are written like "Calabogie, ON".
Tracks outside Canada are left alone.
"""

import argparse
import json
import math
import os
import re
import sys
import time
import unicodedata
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fetch_track_map import DEFAULT_DB, OVERPASS_URLS, _post  # noqa: E402

USER_AGENT = "track-rat/1.0 (github.com/DylanD2402/track-rat)"
NOMINATIM = "https://nominatim.openstreetmap.org/reverse"
PHOTON = "https://photon.komoot.io/reverse"
SEARCH_M = 25000      # look for named places this far from a track
AT_PLACE_M = 3000     # this close to a village or hamlet, the track is "at" it
PROVINCES = {"AB", "BC", "MB", "NB", "NL", "NS", "NT", "NU", "ON", "PE", "QC", "SK", "YT"}
PROVINCE_NAMES = {"alberta", "british columbia", "manitoba", "new brunswick", "newfoundland and labrador",
                  "newfoundland", "nova scotia", "northwest territories", "nunavut", "ontario",
                  "prince edward island", "quebec", "québec", "saskatchewan", "yukon"}
NOT_A_TOWN = re.compile(r"\b(county|district|municipality|regional|no\.|area [a-z]\b)", re.I)


def plain(s):
    """Lower case, no accents, "St-" spelt out, punctuation as spaces: for comparing names."""
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().lower()
    s = re.sub(r"\bste?[-. ]", lambda m: "sainte " if m.group(0).startswith("ste") else "saint ", s)
    return " ".join(re.sub(r"[^a-z0-9]+", " ", s).split())


def first_name(name):
    """ "Grand-Sault / Grand Falls" -> "Grand-Sault"; "Brockville (city)" -> "Brockville"."""
    name = re.sub(r"\s*\([^)]*\)?", "", name)  # brackets first: "Area E (Beaverdell/West Boundary)"
    return name.split("/")[0].strip()


# First words too common to say a track is named after a place: "St-Élie" isn't "Saint-Denis".
COMMON_FIRST_WORDS = {"saint", "sainte", "mount", "north", "south", "upper", "lower", "little", "grand",
                      "notre", "pointe", "riviere", "petit", "petite", "great", "glen", "port"}


def existing_town(location):
    """The town in an un-normalised location from the track's own address ("Toronto, Ontario",
    "Baldonnel"), or "" if there's none or it's already "Town, XX" or isn't a town ("Leduc County")."""
    loc = (location or "").strip()
    if not loc or loc.lower() == "your location":
        return ""
    parts = [p.strip() for p in loc.split(",")]
    if len(parts) > 1 and parts[-1] in PROVINCES:
        return ""  # already done: --all recomputes it
    town = parts[0] if (len(parts) == 1 or parts[-1].lower() in PROVINCE_NAMES) else ""
    if town.lower() in PROVINCE_NAMES or NOT_A_TOWN.search(town):
        return ""
    return town


def track_point(t):
    p = t.get("startFinish") or t.get("center")
    if not p and t.get("layouts"):
        p = t["layouts"][0].get("startFinish") or t["layouts"][0].get("center")
    if not p or (abs(p["lat"]) < 1e-6 and abs(p["lon"]) < 1e-6):
        return None
    return p["lat"], p["lon"]


def dist_m(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 6371000 * 2 * math.asin(math.sqrt(h))


def nominatim(lat, lon):
    q = urllib.parse.urlencode({"format": "jsonv2", "lat": lat, "lon": lon, "zoom": 14, "addressdetails": 1})
    # No language preference: towns keep their own names (Montréal, Trois-Rivières), as Photon gives them.
    req = urllib.request.Request(f"{NOMINATIM}?{q}", headers={"User-Agent": USER_AGENT})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return json.load(r).get("address", {})
        except Exception as e:
            print(f"  Nominatim: {e} (retrying)", file=sys.stderr)
            time.sleep(5)
    return {}


def photon_places(lat, lon):
    """Cities, towns, villages and hamlets within SEARCH_M of a point, from Photon. None if unreachable."""
    q = [("lat", lat), ("lon", lon), ("limit", 50), ("radius", SEARCH_M // 1000)]  # enough to reach past the hamlets
    q += [("osm_tag", "place:" + k) for k in ("city", "town", "village", "hamlet")]
    req = urllib.request.Request(f"{PHOTON}?{urllib.parse.urlencode(q)}", headers={"User-Agent": USER_AGENT})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                doc = json.load(r)
            out = []
            for f in doc.get("features", []):
                p, (flon, flat) = f.get("properties", {}), f["geometry"]["coordinates"]
                if p.get("name") and p.get("osm_value") in ("city", "town", "village", "hamlet"):
                    out.append((p["name"], p["osm_value"], (flat, flon)))
            return out
        except Exception as e:
            print(f"  Photon: {e} (retrying)", file=sys.stderr)
            time.sleep(5)
    return None


def places_near(points, batch=5):
    """Every city, town, village and hamlet within SEARCH_M of any of the points.
    Asked in small batches: one query for every track at once times out."""
    found = {}
    for start in range(0, len(points), batch):
        chunk = points[start:start + batch]
        parts = "".join(f'node(around:{SEARCH_M},{la:.5f},{lo:.5f})[place~"^(city|town|village|hamlet)$"][name];'
                        for la, lo in chunk)
        data = urllib.parse.urlencode({"data": f"[out:json][timeout:120];({parts});out;"}).encode()
        for attempt in range(8):  # the public servers are often busy: keep trying
            url = OVERPASS_URLS[attempt % len(OVERPASS_URLS)]
            try:
                print(f"  towns near tracks {start + 1}-{start + len(chunk)} of {len(points)} ({url})...")
                for e in json.loads(_post(url, data, 180))["elements"]:
                    tags = e["tags"]
                    found[e["id"]] = (tags.get("name:en", tags["name"]), tags["place"], (e["lat"], e["lon"]))
                break
            except Exception as e:
                print(f"  {url}: {e}", file=sys.stderr)
                time.sleep(15 + 10 * attempt)
        else:
            sys.exit("Could not reach OpenStreetMap (Overpass). Try again later.")
        time.sleep(2)  # be gentle with the public servers
    return list(found.values())


def named_after(track_name, near):
    """A nearby place the track is named after ("Shannonville Motorsports Park", "Sauble
    Speedway" for Sauble Beach), or "". Every name of a two-name place counts."""
    track = f" {plain(track_name)} "
    for d, name, kind in near:
        for part in re.sub(r"\s*\([^)]*\)?", "", name).split("/"):
            p = plain(part)
            first = p.split()[0] if p else ""
            if p and (f" {p} " in track or
                      (len(first) >= 5 and first not in COMMON_FIRST_WORDS and f" {first} " in track)):
                return part.strip()
    return ""


def reserve_like(name):
    """Numbered or two-name places ("Okanagan 1", "Qweqwe'ópelhp / Kwawkwawapilt"): usually a
    reserve beside the track, not the name people use for where it is."""
    return "/" in name or any(c.isdigit() for c in name)


def choose_town(point, address, places, track_name=""):
    """The name people use for where a track is (see the order at the top of this file).
    A track 6 km from a village and 13 km from a town is known by the town: CTMP is
    "Bowmanville", not "Tyrone"."""
    near = sorted(((dist_m(point, p), name, kind) for name, kind, p in places), key=lambda x: x[0])
    near = [x for x in near if x[0] <= SEARCH_M]
    named = named_after(track_name, near)
    if named:
        return named
    # In a city, town or village: Nominatim's name, when it's a real place and not a township.
    for key in ("city", "town", "village"):
        name = address.get(key)
        if name and not reserve_like(name) and any(n == name for _, n, _ in near):
            return first_name(name)
    for d, name, kind in near:  # right next to a place, whatever its size (Calabogie)
        if d <= AT_PLACE_M and not reserve_like(name):
            return first_name(name)
    for wanted in (("city", "town"), ("village",)):
        for d, name, kind in near:
            if kind in wanted and not reserve_like(name):
                return first_name(name)
        for d, name, kind in near:
            if kind in wanted:
                return first_name(name)
    # Nothing named nearby: the township or county is better than nothing.
    fallback = address.get("city") or address.get("municipality") or address.get("county") or ""
    return first_name(fallback)


def province_of(address):
    code = address.get("ISO3166-2-lvl4", "")
    code = code.split("-")[-1] if code.startswith("CA-") else ""
    return code if code in PROVINCES else ""


def fill_locations(tracks, redo_all=False, quiet=False):
    """Set "Town, XX" on Canadian tracks (those missing a province, or all). Returns how many changed."""
    todo = []
    for t in tracks:
        if t.get("country") != "Canada":
            continue
        loc = t.get("location", "")
        has_prov = loc.rsplit(", ", 1)[-1] in PROVINCES if ", " in loc else False
        if (redo_all or not has_prov) and track_point(t):
            todo.append(t)
    if not todo:
        return 0
    changed = 0
    for i, t in enumerate(todo):
        point = track_point(t)
        address = nominatim(*point)
        places = photon_places(*point)
        if places is None:  # Photon is down: Overpass instead
            places = places_near([point])
        time.sleep(1.1)  # Nominatim's usage policy: at most one request a second
        town = existing_town(t.get("location")) or choose_town(point, address, places, t.get("name", ""))
        prov = province_of(address)
        new = ", ".join(x for x in (town, prov) if x)
        if not quiet:
            print(f"  {i + 1:>3}/{len(todo)}  {t['name'][:42]:<42} {t.get('location') or '-':<28} -> {new}")
        if new and new != t.get("location"):
            t["location"] = new
            changed += 1
    return changed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default=DEFAULT_DB, help=f"tracks.json to update (default {DEFAULT_DB})")
    ap.add_argument("--all", action="store_true", help="redo every Canadian track, not just those with no province")
    ap.add_argument("--dry-run", action="store_true", help="show the changes without saving")
    args = ap.parse_args()

    with open(args.db, encoding="utf-8") as f:
        db = json.load(f)
    n = fill_locations(db["tracks"], redo_all=args.all)
    if args.dry_run:
        print(f"{n} location(s) would change (dry run, nothing saved)")
        return
    with open(args.db, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=1)
        f.write("\n")
    print(f"{n} location(s) updated in {args.db}")


if __name__ == "__main__":
    main()
