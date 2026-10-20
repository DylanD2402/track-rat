#!/usr/bin/env python3
"""
Host test for tools/fetch_canada_tracks.py's karting marks: a karting venue,
or one whose every raceway is a kart track, gets "kind": "kart"; a circuit
with a kart track beside it doesn't; and a kart track already in tracks.json
gets the mark too (from a saved Overpass result, no download).

    python test/test_fetch_tracks.py
"""
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import sys, json, tempfile, subprocess
sys.path.insert(0, os.path.abspath('tools'))
import fetch_canada_tracks as f
doc = {"elements": [
  {"type": "way", "tags": {"leisure": "track", "sport": "karting", "name": "Kartplex"}, "center": {"lat": 45.0, "lon": -75.0}},
  {"type": "way", "tags": {"leisure": "track", "sport": "motor", "name": "Big Circuit"}, "center": {"lat": 46.0, "lon": -75.0}},
  {"type": "way", "tags": {"highway": "raceway", "sport": "karting"}, "geometry": [{"lat": 46.001, "lon": -75.0}, {"lat": 46.002, "lon": -75.001}, {"lat": 46.003, "lon": -75.0}]},
]}
ts = {t["name"]: t for t in f.build_tracks(doc)}
assert ts["Kartplex"].get("kind") == "kart", ts["Kartplex"]
assert "kind" not in ts["Big Circuit"], "a circuit with a kart track beside it stays a circuit"
doc2 = {"elements": [{"type": "way", "tags": {"leisure": "track", "name": "Kart Town"}, "center": {"lat": 47.0, "lon": -75.0}},
  {"type": "way", "tags": {"highway": "raceway", "sport": "karting"}, "geometry": [{"lat": 47.001, "lon": -75.0}, {"lat": 47.002, "lon": -75.001}]}]}
assert f.build_tracks(doc2)[0].get("kind") == "kart", "only kart raceways: a kart track"
doc["elements"].pop()
d = tempfile.mkdtemp()
db = os.path.join(d, "tracks.json"); src = os.path.join(d, "osm.json")
json.dump({"tracks": [{"id": "kartplex", "name": "Kartplex", "center": {"lat": 45.0001, "lon": -75.0}}]}, open(db, "w"))
json.dump(doc, open(src, "w"))
out = subprocess.run([sys.executable, "tools/fetch_canada_tracks.py", "--db", db, "--from-json", src], capture_output=True, text=True)
assert out.returncode == 0, out.stderr
res = json.load(open(db))
k = [t for t in res["tracks"] if t["id"] == "kartplex"][0]
assert k.get("kind") == "kart", k
assert "Marked 1" in out.stdout, out.stdout
print("fetch_canada_tracks kart checks OK")
