#!/usr/bin/env python3
"""
Build the world track set from OpenStreetMap: every paved circuit and kart
track, written as 2-degree tiles for the SD card (Phase 10 of ROADMAP.md).

    python tools/fetch_world_tracks.py --countries CA,GB,JP
    python tools/fetch_world_tracks.py                 (every country; hours)
    python tools/fetch_world_tracks.py --from-cache    (no downloads at all)

Output (--out, default build/world):
    <out>/<lat0>/<lon0>.json   {"tracks": [...]}, a track in the tile of its
                               centre: lat0 = floor(lat/2)*2, lon0 = floor(lon/2)*2,
                               e.g. 44/-78.json, -34/150.json
    <out>/index.json           {"generated", "source", "tiles": {"44/-78": n}, "tracks": n}

Tracks use the schema of data/tracks.json (id, name, location, country, center,
length, map, pit, kind, layouts), plus:
- "localName": OpenStreetMap's own name, when the name we chose (name:en, then
  int_name, then name; the first with Latin letters) differs from it.
- "startFinish": {"lat", "lon", "radius", "heading", "estimated": true}, only
  for a circuit with a mapped pit lane: the point of the circuit closest to
  the middle of the pit lane, heading along the way's direction (see
  osm_tracks.estimate_start_finish). OpenStreetMap doesn't record the line.
- ids are "osm-w<way id>", "osm-r<relation id>" or "osm-n<node id>" of the
  venue (or, with no venue, of the longest raceway way), stable between runs.

A venue with several separate circuits (not joined end to end) gets "layouts".
Dirt, motocross and drag strips are skipped unless asked for; karting tracks
are included unless --no-karting. One country per Overpass request, with a
pause between requests; each raw answer is cached in --cache (default
build/osm-cache/<CC>.json), so a rerun doesn't download again (--refresh to).

--validate-estimates compares the start/finish estimate with the real lines in
data/tracks.json (it downloads the circuits around them, cached too).

Only uses the Python standard library.
"""

import argparse
import datetime
import json
import math
import os
import shutil
import sys
import time
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fetch_track_map import DEFAULT_DB, REPO_ROOT, simplify, to_local  # noqa: E402
from osm_tracks import (OverpassError, cached_fetch, country_query, dist_m, estimate_from_ways,  # noqa: E402
                        group_venues, is_kart, is_pit_tags, line_len_m)

TOLERANCE_M = 1.5
TILE_DEG = 2
MIN_LAYOUT_M = 150          # a separate piece of raceway shorter than this is a connector, not a layout
JOIN_M = 3                  # ways whose ends are this close are one circuit
LOOSE_MIN_M = 300           # an unnamed raceway with no venue must be this long to count
GATE_M, KART_GATE_M = 15, 8
SOURCE = "OpenStreetMap contributors, ODbL"

COUNTRIES = {
    "AD": "Andorra", "AE": "United Arab Emirates", "AF": "Afghanistan", "AG": "Antigua and Barbuda", "AI": "Anguilla",
    "AL": "Albania", "AM": "Armenia", "AO": "Angola", "AQ": "Antarctica", "AR": "Argentina", "AS": "American Samoa",
    "AT": "Austria", "AU": "Australia", "AW": "Aruba", "AX": "Aland Islands", "AZ": "Azerbaijan",
    "BA": "Bosnia and Herzegovina", "BB": "Barbados", "BD": "Bangladesh", "BE": "Belgium", "BF": "Burkina Faso",
    "BG": "Bulgaria", "BH": "Bahrain", "BI": "Burundi", "BJ": "Benin", "BL": "Saint Barthelemy", "BM": "Bermuda",
    "BN": "Brunei", "BO": "Bolivia", "BQ": "Caribbean Netherlands", "BR": "Brazil", "BS": "Bahamas", "BT": "Bhutan",
    "BV": "Bouvet Island", "BW": "Botswana", "BY": "Belarus", "BZ": "Belize", "CA": "Canada", "CC": "Cocos Islands",
    "CD": "DR Congo", "CF": "Central African Republic", "CG": "Congo", "CH": "Switzerland", "CI": "Cote d'Ivoire",
    "CK": "Cook Islands", "CL": "Chile", "CM": "Cameroon", "CN": "China", "CO": "Colombia", "CR": "Costa Rica",
    "CU": "Cuba", "CV": "Cape Verde", "CW": "Curacao", "CX": "Christmas Island", "CY": "Cyprus", "CZ": "Czechia",
    "DE": "Germany", "DJ": "Djibouti", "DK": "Denmark", "DM": "Dominica", "DO": "Dominican Republic", "DZ": "Algeria",
    "EC": "Ecuador", "EE": "Estonia", "EG": "Egypt", "EH": "Western Sahara", "ER": "Eritrea", "ES": "Spain",
    "ET": "Ethiopia", "FI": "Finland", "FJ": "Fiji", "FK": "Falkland Islands", "FM": "Micronesia", "FO": "Faroe Islands",
    "FR": "France", "GA": "Gabon", "GB": "United Kingdom", "GD": "Grenada", "GE": "Georgia", "GF": "French Guiana",
    "GG": "Guernsey", "GH": "Ghana", "GI": "Gibraltar", "GL": "Greenland", "GM": "Gambia", "GN": "Guinea",
    "GP": "Guadeloupe", "GQ": "Equatorial Guinea", "GR": "Greece", "GS": "South Georgia", "GT": "Guatemala",
    "GU": "Guam", "GW": "Guinea-Bissau", "GY": "Guyana", "HK": "Hong Kong", "HM": "Heard Island", "HN": "Honduras",
    "HR": "Croatia", "HT": "Haiti", "HU": "Hungary", "ID": "Indonesia", "IE": "Ireland", "IL": "Israel",
    "IM": "Isle of Man", "IN": "India", "IO": "British Indian Ocean Territory", "IQ": "Iraq", "IR": "Iran", "IS": "Iceland",
    "IT": "Italy", "JE": "Jersey", "JM": "Jamaica", "JO": "Jordan", "JP": "Japan", "KE": "Kenya", "KG": "Kyrgyzstan",
    "KH": "Cambodia", "KI": "Kiribati", "KM": "Comoros", "KN": "Saint Kitts and Nevis", "KP": "North Korea",
    "KR": "South Korea", "KW": "Kuwait", "KY": "Cayman Islands", "KZ": "Kazakhstan", "LA": "Laos", "LB": "Lebanon",
    "LC": "Saint Lucia", "LI": "Liechtenstein", "LK": "Sri Lanka", "LR": "Liberia", "LS": "Lesotho", "LT": "Lithuania",
    "LU": "Luxembourg", "LV": "Latvia", "LY": "Libya", "MA": "Morocco", "MC": "Monaco", "MD": "Moldova",
    "ME": "Montenegro", "MF": "Saint Martin", "MG": "Madagascar", "MH": "Marshall Islands", "MK": "North Macedonia",
    "ML": "Mali", "MM": "Myanmar", "MN": "Mongolia", "MO": "Macao", "MP": "Northern Mariana Islands", "MQ": "Martinique",
    "MR": "Mauritania", "MS": "Montserrat", "MT": "Malta", "MU": "Mauritius", "MV": "Maldives", "MW": "Malawi",
    "MX": "Mexico", "MY": "Malaysia", "MZ": "Mozambique", "NA": "Namibia", "NC": "New Caledonia", "NE": "Niger",
    "NF": "Norfolk Island", "NG": "Nigeria", "NI": "Nicaragua", "NL": "Netherlands", "NO": "Norway", "NP": "Nepal",
    "NR": "Nauru", "NU": "Niue", "NZ": "New Zealand", "OM": "Oman", "PA": "Panama", "PE": "Peru",
    "PF": "French Polynesia", "PG": "Papua New Guinea", "PH": "Philippines", "PK": "Pakistan", "PL": "Poland",
    "PM": "Saint Pierre and Miquelon", "PN": "Pitcairn Islands", "PR": "Puerto Rico", "PS": "Palestine", "PT": "Portugal",
    "PW": "Palau", "PY": "Paraguay", "QA": "Qatar", "RE": "Reunion", "RO": "Romania", "RS": "Serbia", "RU": "Russia",
    "RW": "Rwanda", "SA": "Saudi Arabia", "SB": "Solomon Islands", "SC": "Seychelles", "SD": "Sudan", "SE": "Sweden",
    "SG": "Singapore", "SH": "Saint Helena", "SI": "Slovenia", "SJ": "Svalbard", "SK": "Slovakia", "SL": "Sierra Leone",
    "SM": "San Marino", "SN": "Senegal", "SO": "Somalia", "SR": "Suriname", "SS": "South Sudan",
    "ST": "Sao Tome and Principe", "SV": "El Salvador", "SX": "Sint Maarten", "SY": "Syria", "SZ": "Eswatini",
    "TC": "Turks and Caicos Islands", "TD": "Chad", "TF": "French Southern Territories", "TG": "Togo", "TH": "Thailand",
    "TJ": "Tajikistan", "TK": "Tokelau", "TL": "Timor-Leste", "TM": "Turkmenistan", "TN": "Tunisia", "TO": "Tonga",
    "TR": "Turkey", "TT": "Trinidad and Tobago", "TV": "Tuvalu", "TW": "Taiwan", "TZ": "Tanzania", "UA": "Ukraine",
    "UG": "Uganda", "UM": "US Minor Outlying Islands", "US": "United States", "UY": "Uruguay", "UZ": "Uzbekistan",
    "VA": "Vatican City", "VC": "Saint Vincent and the Grenadines", "VE": "Venezuela", "VG": "British Virgin Islands",
    "VI": "US Virgin Islands", "VN": "Vietnam", "VU": "Vanuatu", "WF": "Wallis and Futuna", "WS": "Samoa", "XK": "Kosovo",
    "YE": "Yemen", "YT": "Mayotte", "ZA": "South Africa", "ZM": "Zambia", "ZW": "Zimbabwe",
}


# --- names ------------------------------------------------------------------------

def has_latin(s):
    return any(c.isalpha() and unicodedata.name(c, "").startswith("LATIN") for c in s or "")


def latin_tag(tags, *keys):
    for k in keys:
        v = (tags.get(k) or "").strip()
        if v and has_latin(v):
            return v
    return ""


def venue_name_key(tags):
    return tags.get("name") or tags.get("name:en") or tags.get("int_name")


def pick_name(tags, kart, location, country):
    """(name, localName or None). name:en, then int_name, then name: the first with Latin
    letters. With none, "<Kind> track, <location>" from English tags. Never empty."""
    name = latin_tag(tags, "name:en", "int_name", "name")
    if not name:
        name = f"{'Kart' if kart else 'Race'} track, {location or country}"
    local = (tags.get("name") or "").strip()
    return name, (local if local and local != name else None)


def location_of(tags, country):
    """Town and region from the venue's own tags (no geocoder: it'd be 10,000 requests), else the country."""
    city = latin_tag(tags, "addr:city:en", "addr:city", "is_in:city", "addr:town", "addr:village")
    region = latin_tag(tags, "addr:state:en", "addr:state", "addr:province", "is_in:state", "is_in:province")
    if not city and not region:
        isin = [p.strip() for p in (tags.get("is_in") or "").split(",") if p.strip()]
        if isin and has_latin(isin[0]):
            city = isin[0]
    parts = [p for p in (city, region) if p]
    return ", ".join(parts) if parts else country


# --- building tracks ---------------------------------------------------------------------

def _components(ways):
    """Group ways whose end points meet (within JOIN_M of a vertex of another way): one circuit each."""
    n = len(ways)
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(n):
        ends = [ways[i]["pts"][0], ways[i]["pts"][-1]]
        for j in range(n):
            if i == j or find(i) == find(j):
                continue
            if any(dist_m(e, p) <= JOIN_M for e in ends for p in ways[j]["pts"]):
                parent[find(i)] = find(j)
    groups = {}
    for i in range(n):
        groups.setdefault(find(i), []).append(ways[i])
    return list(groups.values())


def _way_ref(w):
    return w.get("id") or 0


def _compact(pts, center):
    s = simplify(pts, TOLERANCE_M, center)
    return [[round(p[0], 6), round(p[1], 6)] for p in s]


def _layout_of(ways, pit_ways, center, gate):
    """map/pit/length/startFinish for one circuit: its racing ways and the pit lane ways beside it."""
    lines, pit_idx = [], []
    for w in ways + pit_ways:
        l = _compact(w["pts"], center)
        if len(l) >= 2:
            if w in pit_ways:
                pit_idx.append(len(lines))
            lines.append(l)
    out = {"length": round(sum(line_len_m(w["pts"]) for w in ways)), "map": lines}
    if pit_idx:
        out["pit"] = pit_idx
    est = estimate_from_ways(ways, pit_ways)
    if est:
        out["startFinish"] = {"lat": est["lat"], "lon": est["lon"], "radius": gate, "heading": est["heading"],
                              "estimated": True}
    return out


def _track_id(venue):
    refs = [r for r in venue.get("refs", []) if r[0] and r[1]]
    if refs:
        # prefer an area (relation, then way) over a node: it's the more stable object
        order = {"relation": 0, "way": 1, "node": 2}
        t, i = min(refs, key=lambda r: (order.get(r[0], 3), r[1]))
        return f"osm-{t[0]}{i}"
    main = max(venue["ways"], key=lambda w: (line_len_m(w["pts"]), _way_ref(w)))
    return f"osm-w{_way_ref(main)}"


def make_track(venue, country, country_code=""):
    t = venue["tags"]
    center = venue["center"]
    racing = [w for w in venue["ways"] if not is_pit_tags(w["tags"])]
    pits = [w for w in venue["ways"] if is_pit_tags(w["tags"])]
    kart = is_kart(t) or (bool(venue["ways"]) and all(is_kart(w["tags"]) for w in venue["ways"])
                          and "motor" not in t.get("sport", ""))
    gate = KART_GATE_M if kart else GATE_M
    location = location_of(t, country)
    name, local = pick_name(t, kart, location, country)
    entry = {
        "id": _track_id(venue),
        "name": name,
        "location": location,
        "country": country,
        "length": 0,
        "center": {"lat": round(center[0], 7), "lon": round(center[1], 7)},
    }
    if local:
        entry["localName"] = local
    if kart:
        entry["kind"] = "kart"

    comps = _components(racing) if racing else []
    comps.sort(key=lambda c: -sum(line_len_m(w["pts"]) for w in c))
    if len(comps) > 1:
        comps = [c for c in comps if sum(line_len_m(w["pts"]) for w in c) >= MIN_LAYOUT_M] or comps[:1]
    if len(comps) > 1:
        layouts, used = [], set()
        for n, comp in enumerate(comps, 1):
            near = [p for p in pits if min(dist_m(p["pts"][len(p["pts"]) // 2], q) for w in comp for q in w["pts"]) < 300]
            lo = _layout_of(comp, near, center, gate)
            if all(is_kart(w["tags"]) for w in comp):
                label = "Kart track"
            else:
                longest = max(comp, key=lambda w: line_len_m(w["pts"]))
                label = latin_tag(longest["tags"], "name:en", "int_name", "name") or f"Layout {n}"
                if label == name:
                    label = f"Layout {n}"
            base, k = label, 2
            while label in used:
                label, k = f"{base} {k}", k + 1
            used.add(label)
            layouts.append({"name": label, **lo})
        entry["layouts"] = layouts
        entry["length"] = layouts[0]["length"]
    else:
        lo = _layout_of(comps[0] if comps else [], pits if comps else [], center, gate) if comps else \
            {"length": 0, "map": [_compact(w["pts"], center) for w in pits if len(w["pts"]) >= 2]}
        entry["length"] = lo.pop("length")
        if lo.get("map"):
            entry.update(lo)
    return entry


def build_world_tracks(doc, cc, include_dirt=False, include_drag=False, karting=True):
    """All the tracks of one country's Overpass answer, as tile-ready entries."""
    country = COUNTRIES.get(cc, cc)
    loose = []
    venues = group_venues(doc, include_dirt, include_drag, karting, name_of=venue_name_key, loose_unnamed=loose)
    # Unnamed raceways with no venue: a circuit each, if long enough
    for comp in _components(loose):
        if sum(line_len_m(w["pts"]) for w in comp if not is_pit_tags(w["tags"])) < LOOSE_MIN_M:
            continue
        main = max(comp, key=lambda w: line_len_m(w["pts"]))
        venues.append({"name": "", "tags": main["tags"], "center": main["pts"][len(main["pts"]) // 2],
                       "ways": comp, "refs": []})
    tracks = {}
    for v in venues:
        if not any(not is_pit_tags(w["tags"]) for w in v["ways"]) and not v["refs"]:
            continue
        t = make_track(v, country, cc)
        if t["id"] not in tracks:
            tracks[t["id"]] = t
    return list(tracks.values())


# --- tiles -----------------------------------------------------------------------------------

def tile_of(lat, lon):
    return math.floor(lat / TILE_DEG) * TILE_DEG, math.floor(lon / TILE_DEG) * TILE_DEG


def write_tiles(tracks, out, generated=None):
    """Write the tiles and index.json (replacing the old ones in out); returns the index."""
    tiles = {}
    for t in sorted({t["id"]: t for t in tracks}.values(), key=lambda t: t["id"]):
        tiles.setdefault(tile_of(t["center"]["lat"], t["center"]["lon"]), []).append(t)
    if os.path.isdir(out):  # a previous run's tiles (only ours: integer folders of integer .json files)
        for d in os.listdir(out):
            p = os.path.join(out, d)
            if os.path.isdir(p) and d.lstrip("-").isdigit():
                shutil.rmtree(p)
    index = {"generated": generated or datetime.date.today().isoformat(), "source": SOURCE, "tiles": {}, "tracks": 0}
    for (lat0, lon0), items in sorted(tiles.items()):
        d = os.path.join(out, str(lat0))
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, f"{lon0}.json"), "w", encoding="utf-8") as f:
            json.dump({"tracks": items}, f, separators=(",", ":"), ensure_ascii=False)
        index["tiles"][f"{lat0}/{lon0}"] = len(items)
        index["tracks"] += len(items)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, separators=(",", ":"))
    return index


# --- validating the estimate -----------------------------------------------------------------

def _angle_diff(a, b):
    return abs((a - b + 180) % 360 - 180)


def validate_estimates(db_path, cache_dir, align_with_pit=True, offline=False, pause=10):
    """Estimate the start/finish of every track in db_path that has a real one, from the OpenStreetMap
    raceways within 2.5 km of it, and print the distance to the real line. Returns the distances."""
    db = json.load(open(db_path, encoding="utf-8"))
    results = []
    for tr in db.get("tracks", []):
        sf = tr.get("startFinish") or ((tr.get("layouts") or [{}])[0].get("startFinish"))
        if not sf:
            continue
        q = (f'[out:json][timeout:60];way["highway"="raceway"](around:2500,{sf["lat"]},{sf["lon"]});out geom tags;')
        path = os.path.join(cache_dir, f"validate-{tr['id']}.json")
        try:
            doc, cached = cached_fetch(path, q, offline=offline, timeout=120)
        except OverpassError as e:
            print(f"  {tr['id']}: {e}")
            continue
        if doc is None:
            continue
        if not cached:
            time.sleep(pause)
        ways = [{"tags": e.get("tags", {}), "pts": [[g["lat"], g["lon"]] for g in e["geometry"]]}
                for e in doc.get("elements", []) if e.get("geometry")]
        pits = [w for w in ways if is_pit_tags(w["tags"])]
        circ = [w for w in ways if not is_pit_tags(w["tags"])]
        est = estimate_from_ways(circ, pits, align_with_pit=align_with_pit)
        if not est:
            print(f"  {tr['id']:<20} {len(ways)} raceways, no pit lane mapped: no estimate")
            results.append((tr["id"], None))
            continue
        d = dist_m([sf["lat"], sf["lon"]], [est["lat"], est["lon"]])
        hd = f", heading off by {_angle_diff(est['heading'], sf['heading']):.0f} deg" if "heading" in sf else ""
        print(f"  {tr['id']:<20} estimate is {d:6.0f} m from the real line (heading {est['heading']:.0f}{hd})")
        results.append((tr["id"], d))
    return results


# --- main ------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--countries", help="ISO 3166-1 alpha-2 codes, comma separated (default: every country)")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "build", "world"))
    ap.add_argument("--cache", default=os.path.join(REPO_ROOT, "build", "osm-cache"),
                    help="where the raw Overpass answers are kept, one <CC>.json per country")
    ap.add_argument("--from-cache", action="store_true", help="use only the cache; download nothing")
    ap.add_argument("--refresh", action="store_true", help="download again even when cached")
    ap.add_argument("--pause", type=float, default=15, help="seconds between Overpass requests (default 15)")
    ap.add_argument("--include-dirt", action="store_true")
    ap.add_argument("--include-drag", action="store_true")
    ap.add_argument("--no-karting", action="store_true")
    ap.add_argument("--validate-estimates", action="store_true",
                    help="compare the start/finish estimate with the real lines in --db and exit")
    ap.add_argument("--db", default=DEFAULT_DB)
    args = ap.parse_args()

    if args.validate_estimates:
        r = [d for _, d in validate_estimates(args.db, args.cache, offline=args.from_cache, pause=args.pause) if d is not None]
        if r:
            r.sort()
            print(f"\n{len(r)} estimated: median {r[len(r) // 2]:.0f} m, within 150 m: {sum(d <= 150 for d in r)}")
        return

    codes = [c.strip().upper() for c in args.countries.split(",") if c.strip()] if args.countries else sorted(COUNTRIES)
    bad = [c for c in codes if c not in COUNTRIES]
    if bad:
        sys.exit(f"Unknown country code(s): {', '.join(bad)}")

    tracks, t0 = [], time.time()
    for n, cc in enumerate(codes, 1):
        path = os.path.join(args.cache, f"{cc}.json")
        print(f"[{n}/{len(codes)}] {cc} {COUNTRIES[cc]}")
        try:
            doc, cached = cached_fetch(path, country_query(cc, 900), refresh=args.refresh, offline=args.from_cache)
        except OverpassError as e:
            print(f"  skipped: {e}", file=sys.stderr)
            continue
        if doc is None:
            print("  not in the cache, skipped")
            continue
        got = build_world_tracks(doc, cc, args.include_dirt, args.include_drag, not args.no_karting)
        print(f"  {len(got)} track(s){' (from cache)' if cached else ''}")
        tracks += got
        if not cached and n < len(codes):
            time.sleep(args.pause)

    index = write_tiles(tracks, args.out)
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(args.out) for f in fs)
    est = sum(1 for t in tracks if "startFinish" in t)
    print(f"\n{index['tracks']} track(s) in {len(index['tiles'])} tile(s), {size / 1024 / 1024:.1f} MB, "
          f"{est} with an estimated start/finish, in {time.time() - t0:.0f} s -> {args.out}")


if __name__ == "__main__":
    main()
