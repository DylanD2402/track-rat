#!/usr/bin/env python3
"""
Host test for tools/obd_car.py, on made-up signal sets (no download): model
years (OBDb's from/to/years filters), the make's commands filling in for the
model's (but not hidden by a model command for other years), OBDb's
recorded answers ("seen", "not seen", "unverified"), and the profile it
writes (only the chosen signals, the request rate, the licence and source).

    python test/test_obd_car.py
"""
import os
import sys

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.abspath("tools"))
import obd_car as o  # noqa: E402

# Years: any of to / from / years
assert o.year_matches(None, 2010) and o.year_matches({}, 2010)
assert o.year_matches({"from": 2008}, 2010) and not o.year_matches({"from": 2011}, 2010)
assert o.year_matches({"to": 2007, "years": [2010], "from": 2022}, 2010)
assert not o.year_matches({"to": 2007, "years": [2009], "from": 2022}, 2010)

fmt = {"len": 16, "div": 10, "unit": "bars"}
model = {"commands": [
    {"hdr": "DA28", "rax": "28", "cmd": {"22": "4005"}, "freq": 1, "filter": {"from": 2016},
     "signals": [{"id": "CIVIC_BRK_P", "name": "Brake pressure", "fmt": fmt}]},
    {"hdr": "DA11", "rax": "11", "cmd": {"22": "2611"}, "freq": 1, "signals": [{"id": "CIVIC_AFR", "name": "Air fuel", "fmt": fmt}]},
]}
make = {"commands": [
    {"hdr": "DA28", "rax": "28", "cmd": {"22": "4005"}, "freq": 0.25, "dbgfilter": {"to": 2018},
     "signals": [{"id": "HONDA_BRK_P", "name": "Brake pressure", "fmt": fmt}]},
    {"hdr": "DA11", "rax": "11", "cmd": {"22": "2611"}, "freq": 1, "signals": [{"id": "HONDA_AFR", "name": "Air fuel", "fmt": fmt}]},
    {"hdr": "DA28", "cmd": {"22": "4C2D"}, "freq": 0.1, "signals": [
        {"id": "HONDA_STEER", "name": "Steering wheel angle", "fmt": {"len": 16, "sign": True, "unit": "degrees"}},
        {"id": "HONDA_OTHER", "name": "Other", "fmt": {"len": 8, "bix": 16}}]},
]}
cmds = o.merge(model, make, 2008)
ids = [s["id"] for c in cmds for s in c["signals"]]
assert "CIVIC_BRK_P" not in ids, "a model command for 2016 on isn't for a 2008 car"
assert "HONDA_BRK_P" in ids, "...and doesn't hide the make's for 2008"
assert "CIVIC_AFR" in ids and "HONDA_AFR" not in ids, "the model's own wins over the make's"
assert [c["source"] for c in cmds if c["signals"][0]["id"] == "CIVIC_AFR"] == ["model"]
newer = [s["id"] for c in o.merge(model, make, 2020) for s in c["signals"]]
assert "CIVIC_BRK_P" in newer and "HONDA_BRK_P" not in newer, "for 2020 the model's brake pressure"

# OBDb's recorded answers for the year
support = """model_year: 2008
supported_commands_by_ecu:
  "DA11":
  - 0105:ECT
  - DA11.11.222611|f=-2014:CIVIC_AFR
unsupported_commands_by_ecu:
  "DA28":
  - DA28.28.224005
"""
seen, unseen = o.parse_support(support)
assert "DA11.11.222611" in seen and "DA28.28.224005" in unseen
byid = {c["signals"][0]["id"]: c for c in cmds}
assert o.status(byid["CIVIC_AFR"], 2008, seen, unseen) == "seen"
assert o.status(byid["HONDA_BRK_P"], 2008, seen, unseen) == "not seen"
assert o.status(byid["HONDA_STEER"], 2008, set(), set()) == ""
assert o.status(byid["HONDA_BRK_P"], 2008, set(), set()) == "unverified", "a debug filter for the year"

# The profile: just the chosen signals, each command with only its chosen ones
p = o.build_profile(cmds, ["HONDA_BRK_P", "HONDA_STEER"], "Civic Si", "2HGFG", 2008, "Honda-Civic", "Honda", hz=10)
assert p["name"] == "Civic Si" and p["vin"] == "2HGFG" and p["year"] == 2008
assert "CC BY-SA 4.0" in p["license"] and "github.com/OBDb/Honda-Civic" in p["attribution"] and "github.com/OBDb/Honda" in p["attribution"]
assert [len(c["signals"]) for c in p["commands"]] == [1, 1], "HONDA_OTHER left out"
assert all(c["freq"] == 0.1 for c in p["commands"]), "asked 10 times a second"
steer = [c for c in p["commands"] if c["cmd"] == {"22": "4C2D"}][0]
assert "rax" not in steer and steer["signals"][0]["fmt"]["sign"] is True, "fields copied as OBDb has them"
assert o.build_profile(cmds, ["HONDA_BRK_P"], "x", "", 2008, "Honda-Civic", "Honda")["commands"][0]["freq"] == 0.25, "OBDb's rate"
for bad in (["NOPE"], ["CIVIC_BRK_P"]):  # unknown, and not for this year
    try:
        o.build_profile(cmds, bad, "x", "", 2008, "Honda-Civic", "Honda")
        raise AssertionError(f"{bad} should be refused")
    except SystemExit:
        pass
many = {"commands": [{"hdr": "7E0", "cmd": {"22": "%04X" % i}, "freq": 1, "signals": [{"id": f"S{i}", "fmt": fmt}]} for i in range(9)]}
try:
    o.build_profile(o.merge(many, {"commands": []}, 2010), [f"S{i}" for i in range(9)], "x", "", 2010, "A-B", "A")
    raise AssertionError("9 signals should be refused")
except SystemExit:
    pass
print("obd_car checks OK")
