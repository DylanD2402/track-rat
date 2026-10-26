#!/usr/bin/env python3
"""
Host test for tools/fetch_world_tracks.py and tools/osm_tracks.py: tiles (negative
coordinates, the 2-degree floor), ids, names, the start/finish estimate on a synthetic
circuit with a pit lane, layouts, the index and the Overpass retry/cache logic. Small
synthetic Overpass answers, no network.

    python test/test_world_tracks.py
"""
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import sys, json, math, tempfile, urllib.error
sys.path.insert(0, os.path.abspath('tools'))
import fetch_world_tracks as w
import osm_tracks as o

M_LAT = 1 / 111194.9  # degrees of latitude per metre


def way(i, pts, **tags):
    tags.setdefault("highway", "raceway")
    return {"type": "way", "id": i, "tags": tags, "geometry": [{"lat": a, "lon": b} for a, b in pts]}


def venue(t, i, lat, lon, **tags):
    tags.setdefault("leisure", "track")
    tags.setdefault("sport", "motor")
    return {"type": t, "id": i, "tags": tags, "center": {"lat": lat, "lon": lon}}


def oval(lat, lon, rx_m=300, ry_m=150, n=60, ccw=False):
    """A closed circuit round (lat, lon), clockwise seen from above unless ccw."""
    pts = []
    for k in range(n + 1):
        a = 2 * math.pi * k / n * (-1 if ccw else 1)
        pts.append((lat + ry_m * math.cos(a) * M_LAT, lon + rx_m * math.sin(a) * M_LAT / math.cos(math.radians(lat))))
    return pts


# --- tiles ---------------------------------------------------------------------
assert w.tile_of(44.5, -77.9) == (44, -78)
assert w.tile_of(45.99, -76.0) == (44, -76)
assert w.tile_of(46.0, -76.0) == (46, -76)
assert w.tile_of(-33.9, 150.1) == (-34, 150)
assert w.tile_of(-0.5, -0.5) == (-2, -2)
assert w.tile_of(0.0, 0.0) == (0, 0)
assert w.tile_of(51.5, 359.0 - 360) == (50, -2)

# --- a venue with a circuit, a pit lane and an English name -------------------------
LAT, LON = 44.5, -77.9
circuit = oval(LAT, LON)
doc = {"elements": [
    venue("way", 111, LAT, LON, name="Circuit Mont Tremblant", **{"name:en": "Mont Tremblant Circuit", "addr:city": "Lac-Superieur"}),
    way(900, circuit, name="Main"),
]}
ts = w.build_world_tracks(doc, "CA")
assert len(ts) == 1, ts
t = ts[0]
assert t["id"] == "osm-w111", t["id"]
assert t["name"] == "Mont Tremblant Circuit" and t["localName"] == "Circuit Mont Tremblant", t
assert t["country"] == "Canada" and t["location"] == "Lac-Superieur", t
assert "kind" not in t and "startFinish" not in t, "no pit lane: no start/finish"
assert len(t["map"]) == 1 and t["length"] > 1000, t["length"]
assert t["center"] == {"lat": LAT, "lon": LON}
# rounded to 6 decimals, simplified
for p in t["map"][0]:
    assert round(p[0], 6) == p[0] and round(p[1], 6) == p[1]
assert len(t["map"][0]) <= len(circuit)

# same name in both: no localName
d2 = {"elements": [venue("relation", 7, LAT, LON, name="Plain Raceway"), way(1, circuit)]}
t2 = w.build_world_tracks(d2, "CA")[0]
assert t2["id"] == "osm-r7" and "localName" not in t2 and t2["name"] == "Plain Raceway"
# an area wins over a node of the same venue, whatever the order
d3 = {"elements": [venue("node", 3, LAT, LON, name="X Park"), venue("way", 9, LAT, LON, name="X Park"), way(1, circuit)]}
assert w.build_world_tracks(d3, "CA")[0]["id"] == "osm-w9"

# --- names -------------------------------------------------------------------------------
assert w.pick_name({"name": "Fuji Speedway"}, False, "Oyama", "Japan") == ("Fuji Speedway", None)
assert w.pick_name({"name": "富士スピードウェイ", "name:en": "Fuji Speedway"}, False, "", "Japan") == ("Fuji Speedway", "富士スピードウェイ")
assert w.pick_name({"name": "富士スピードウェイ", "int_name": "Fuji"}, False, "", "Japan") == ("Fuji", "富士スピードウェイ")
assert w.pick_name({"name": "Автодром"}, True, "Kazan", "Russia") == ("Kart track, Kazan", "Автодром")
assert w.pick_name({"name": "サーキット"}, False, "Japan", "Japan") == ("Race track, Japan", "サーキット")
assert w.pick_name({}, False, "", "Chile")[0] == "Race track, Chile"
# a CJK-only venue gets a non-empty name through the whole pipeline, and the local name is kept
d4 = {"elements": [venue("way", 21, 35.0, 139.0, name="鈴鹿サーキット", sport="karting", **{"addr:city": "鈴鹿市"}),
                   way(2, oval(35.0, 139.0, 100, 50), sport="karting")]}
t4 = w.build_world_tracks(d4, "JP")[0]
assert t4["name"] == "Kart track, Japan" and t4["localName"] == "鈴鹿サーキット" and t4["kind"] == "kart", t4
assert t4["id"] == "osm-w21"
assert w.location_of({"addr:city": "Zandvoort", "addr:state": "Noord-Holland"}, "Netherlands") == "Zandvoort, Noord-Holland"
assert w.location_of({}, "Chile") == "Chile"
assert w.location_of({"is_in": "Ommen, Overijssel, Netherlands"}, "Netherlands") == "Ommen"

# --- dirt, drag, karting rules (same as the Canada tool) -----------------------------------------
d5 = {"elements": [
    venue("way", 31, 10.5, 10.5, name="Dirt Oval", surface="dirt"), way(31, oval(10.5, 10.5, 100, 50), surface="dirt"),
    venue("way", 32, 11.5, 10.5, name="Drag Strip", raceway="drag_strip"), way(32, oval(11.5, 10.5, 100, 50), raceway="drag_strip"),
    venue("way", 33, 12.5, 10.5, name="Kartland", sport="karting"), way(33, oval(12.5, 10.5, 100, 50), sport="karting"),
]}
assert [t["name"] for t in w.build_world_tracks(d5, "ZZ")] == ["Kartland"]
assert len(w.build_world_tracks(d5, "ZZ", include_dirt=True, include_drag=True)) == 3
assert w.build_world_tracks(d5, "ZZ", karting=False) == []

# --- the start/finish estimate -------------------------------------------------------------------
# A rectangular circuit 1000 m x 300 m, run clockwise (north up): the bottom straight goes west.
# The pit lane runs along the bottom straight, 20 m outside it, drawn the way the lap runs.
LAT0, LON0 = 52.0, 0.5
def at(x, y):  # metres east/north of (LAT0, LON0)
    return (LAT0 + y * M_LAT, LON0 + x * M_LAT / math.cos(math.radians(LAT0)))
rect = [at(0, 0), at(0, 300), at(1000, 300), at(1000, 0), at(0, 0)]  # up the west side, east along the top, down, west along the bottom
pit_cw = [at(800, -20), at(200, -20)]  # drawn westwards, like the bottom straight
est = o.estimate_start_finish([rect], [pit_cw])
assert est, "an estimate"
want = at(500, 0)
assert o.dist_m([est["lat"], est["lon"]], want) < 2, (est, want)
assert abs(est["heading"] - 270) < 1, est  # westwards along the bottom straight
# a pit lane drawn the other way round while the circuit way runs west: heading follows the pit lane
est2 = o.estimate_start_finish([rect], [list(reversed(pit_cw))])
assert abs(est2["heading"] - 90) < 1, est2
assert o.estimate_start_finish([rect], []) is None
assert o.estimate_start_finish([], [pit_cw]) is None
# a pit lane near the middle of the pit lane, not near its ends: an asymmetric pit lane
pit_long = [at(950, -20), at(950, -20), at(900, -20), at(100, -20)]
e3 = o.estimate_start_finish([rect], [pit_long])
assert abs(o.dist_m([e3["lat"], e3["lon"]], at(525, 0))) < 5, e3

# end to end: the venue gets the estimate, flagged, with the schema's names, and a pit index
d6 = {"elements": [venue("way", 41, LAT0, LON0, name="Rect Raceway"),
                   way(1, rect, name="Rect"), way(2, pit_cw, name="Pit Lane", service="pit_lane")]}
t6 = w.build_world_tracks(d6, "GB")[0]
sf = t6["startFinish"]
assert sf["estimated"] is True and sf["radius"] == 15 and abs(sf["heading"] - 270) < 1, sf
assert sorted(sf) == ["estimated", "heading", "lat", "lon", "radius"], sf
assert o.dist_m([sf["lat"], sf["lon"]], want) < 2
assert t6["pit"] == [1] and len(t6["map"]) == 2, t6.get("pit")
assert abs(t6["length"] - 2600) < 10, "the pit lane isn't part of the length"
# "Pitt Race" isn't a pit lane; Boxengasse and a service=pit_lane are
assert not o.is_pit_tags({"name": "Pitt Race"}) and o.is_pit_tags({"name": "Boxengasse"})
assert o.is_pit_tags({"service": "pit_lane"}) and o.is_pit_tags({"raceway": "pit"}) and o.is_pit_tags({"name": "Pit lane"})
# a support pit lane doesn't beat the proper one even when longer
sup = [at(900, 330), at(0, 330)]
d7 = {"elements": [venue("way", 42, LAT0, LON0, name="Two Pits"), way(1, rect), way(2, pit_cw, service="pit_lane"),
                   way(3, sup, name="Support Pit Lane")]}
sf7 = w.build_world_tracks(d7, "GB")[0]["startFinish"]
assert o.dist_m([sf7["lat"], sf7["lon"]], want) < 2, sf7

# --- layouts: two separate circuits at one venue ----------------------------------------------------
far = [(a + 600 * M_LAT, b) for a, b in oval(LAT0, LON0, 150, 80)]
d8 = {"elements": [venue("way", 51, LAT0, LON0, name="Two Tracks"), way(1, rect, name="Grand Prix"), way(2, far, name="Club"),
                   way(3, [at(5000, 5000), at(5050, 5000)])]}
t8 = w.build_world_tracks(d8, "GB")[0]
assert "map" not in t8 and [l["name"] for l in t8["layouts"]] == ["Grand Prix", "Club"], t8
assert all(l["map"] for l in t8["layouts"]) and t8["length"] == t8["layouts"][0]["length"]
# a short connector is not a layout of its own
d9 = {"elements": [venue("way", 52, LAT0, LON0, name="One Track"), way(1, rect), way(2, [at(1000, 0), at(1000, -60)])]}
assert "layouts" not in w.build_world_tracks(d9, "GB")[0]
# the same circuit drawn twice is one layout, not two
d9b = {"elements": [venue("way", 53, LAT0, LON0, name="Twice"), way(1, rect), way(2, rect)]}
assert "layouts" not in w.build_world_tracks(d9b, "GB")[0]
# the nearest city or town within 20 km names the location; villages and far towns don't
d9c = {"elements": [venue("way", 54, LAT0, LON0, name="Near Town"), way(1, rect),
                    {"type": "node", "id": 1, "lat": LAT0 + 0.05, "lon": LON0, "tags": {"place": "town", "name": "Closer"}},
                    {"type": "node", "id": 2, "lat": LAT0 + 0.15, "lon": LON0, "tags": {"place": "city", "name": "Farther"}},
                    {"type": "node", "id": 3, "lat": LAT0 + 0.01, "lon": LON0, "tags": {"place": "village", "name": "Hamlet"}},
                    {"type": "node", "id": 4, "lat": LAT0 + 0.45, "lon": LON0, "tags": {"place": "city", "name": "Out of range"}}]}
t9c = w.build_world_tracks(d9c, "GB")
assert len(t9c) == 1 and t9c[0]["location"] == "Closer", t9c
assert "place" in o.country_query("GB", places=True) and "place" not in o.country_query("GB")
# unnamed raceways with no venue: a long one is a track, a short one isn't
d10 = {"elements": [way(61, oval(20.0, 20.0, 200, 100)), way(62, [at(0, 0), at(0, 50)])]}
t10 = w.build_world_tracks(d10, "ZZ")
assert len(t10) == 1 and t10[0]["id"] == "osm-w61" and t10[0]["name"] == "Race track, ZZ", t10

# --- writing tiles and the index ----------------------------------------------------------------------
def tr(i, lat, lon, **kw):
    return dict({"id": i, "name": i, "location": "", "country": "X", "length": 1, "center": {"lat": lat, "lon": lon}}, **kw)

tracks = [tr("osm-w2", 44.5, -77.9), tr("osm-w1", 45.9, -76.1), tr("osm-w3", -33.8, 150.9), tr("osm-w4", 0.5, -0.5),
          tr("osm-w2", 44.5, -77.9)]  # a duplicate (a venue on a border, in two countries)
with tempfile.TemporaryDirectory() as out:
    os.makedirs(os.path.join(out, "99")); open(os.path.join(out, "99", "9.json"), "w").write("{}")  # a stale tile
    idx = w.write_tiles(tracks, out, generated="2026-10-26")
    assert idx == {"generated": "2026-10-26", "source": "OpenStreetMap contributors, ODbL",
                   "tiles": {"-34/150": 1, "0/-2": 1, "44/-78": 2}, "tracks": 4}, idx
    assert json.load(open(os.path.join(out, "index.json"))) == idx
    assert not os.path.exists(os.path.join(out, "99")), "stale tiles are cleared"
    raw = open(os.path.join(out, "44", "-78.json"), encoding="utf-8").read()
    assert raw == ('{"tracks":[{"id":"osm-w1","name":"osm-w1","location":"","country":"X","length":1,"center":{"lat":45.9,"lon":-76.1}},'
                   '{"id":"osm-w2","name":"osm-w2","location":"","country":"X","length":1,"center":{"lat":44.5,"lon":-77.9}}]}'), raw
    assert [t["id"] for t in json.load(open(os.path.join(out, "-34", "150.json")))["tracks"]] == ["osm-w3"]
    assert os.path.exists(os.path.join(out, "0", "-2.json"))

# --- Overpass: retries, backoff, 429/504 and the cache -------------------------------------------------------
class Boom(urllib.error.HTTPError):
    def __init__(self, code, retry_after=None):
        from email.message import Message
        h = Message()
        if retry_after:
            h["Retry-After"] = str(retry_after)
        super().__init__("http://x", code, "err", h, None)

calls, sleeps = [], []
def post_factory(script):
    it = iter(script)
    def post(url, data, timeout):
        calls.append(url)
        r = next(it)
        if isinstance(r, Exception):
            raise r
        return json.dumps(r).encode()
    return post

quiet = lambda *a, **k: None
doc = o.fetch_overpass("q", mirrors=["A", "B"], post=post_factory([Boom(429), Boom(504), {"elements": [1]}]),
                       sleep=sleeps.append, log=quiet)
assert doc == {"elements": [1]} and calls == ["A", "B", "A"], calls
assert 30 in sleeps or len(sleeps) >= 3, sleeps  # waited between rounds
calls.clear(); sleeps.clear()
doc = o.fetch_overpass("q", mirrors=["A"], post=post_factory([{"remark": "runtime error: Query timed out", "elements": []}, {"elements": [2]}]),
                       sleep=sleeps.append, log=quiet)
assert doc == {"elements": [2]}, "a timed-out (partial) answer is retried"
calls.clear(); sleeps.clear()
try:
    o.fetch_overpass("q", mirrors=["A"], post=post_factory([Boom(429, 120)] * 3), sleep=sleeps.append, log=quiet, rounds=3)
    assert False
except o.OverpassError:
    pass
assert 120 in sleeps, sleeps  # Retry-After honoured
assert len(calls) == 3
# A mirror missing the country's area answers with no raceways (as one did for the UK): ask another
ways = lambda d: any(e.get("type") == "way" for e in d.get("elements", []))
calls.clear(); sleeps.clear()
doc = o.fetch_overpass("q", mirrors=["A", "B"], post=post_factory([{"elements": [{"type": "node"}]}, {"elements": [{"type": "way"}]}]),
                       sleep=sleeps.append, log=quiet, has_data=ways)
assert doc["elements"] == [{"type": "way"}] and calls == ["A", "B"], calls
calls.clear()
doc = o.fetch_overpass("q", mirrors=["A", "B"], post=post_factory([{"elements": []}, {"elements": []}]),
                       sleep=sleeps.append, log=quiet, has_data=ways)
assert doc == {"elements": []} and calls == ["A", "B"], "two empty answers: the country has no tracks"
calls.clear()
doc = o.fetch_overpass("q", mirrors=["A", "B"], post=post_factory([Boom(502), {"elements": []}, Boom(502), Boom(502)]),
                       sleep=sleeps.append, log=quiet, has_data=ways, rounds=2)
assert doc == {"elements": []} and len(calls) == 4, "one empty answer, the rest errors: kept after every round"
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, "sub", "CA.json")
    calls.clear()
    got, cached = o.cached_fetch(p, "q", mirrors=["A"], post=post_factory([{"elements": [3]}]), sleep=quiet, log=quiet)
    assert got == {"elements": [3]} and not cached and os.path.exists(p)
    got, cached = o.cached_fetch(p, "q", mirrors=["A"], post=post_factory([]), sleep=quiet, log=quiet)
    assert got == {"elements": [3]} and cached and len(calls) == 1, "the rerun uses the cache"
    got, cached = o.cached_fetch(os.path.join(d, "none.json"), "q", offline=True)
    assert got is None

# --- country list -----------------------------------------------------------------------------------------------
assert w.COUNTRIES["CA"] == "Canada" and w.COUNTRIES["GB"] and w.COUNTRIES["JP"] and len(w.COUNTRIES) > 240
assert all(len(c) == 2 and c.isupper() for c in w.COUNTRIES)
assert '"ISO3166-1"="JP"' in o.country_query("JP")

print("fetch_world_tracks checks OK")
