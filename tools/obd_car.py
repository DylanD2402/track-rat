#!/usr/bin/env python3
"""
Make a Track Rat car profile (brake pressure, steering angle...) from OBDb.

    python tools/obd_car.py list BMW-1-Series --year 2010 --find brake
    python tools/obd_car.py make BMW-1-Series --year 2010 --signals BMW_BRAKE_P,BMW_STEERING_WHEEL_ANGLE \
        --name "BMW 128i" --vin WBAUP --hz 10
    (then copy the obd folder to the SD card's root)

OBDb (https://github.com/OBDb) documents each car's manufacturer requests,
one repository per model ("BMW-1-Series", "Honda-Civic") plus one per make
("BMW", "Honda"). This downloads both, keeps the requests for the model
year, and lists their signals, marking those OBDb has recorded a car of that
year answering ("seen") or not ("not seen"), and those it hasn't checked for
that year ("unverified"). `make` writes obd/<name>.json with just the
signals you choose (up to 8).

Licence: OBDb's data is CC BY-SA 4.0, so a profile made from it is too: the
file says where it came from and what was changed. Track Rat's code (MIT)
contains no OBDb data. Only uses the Python standard library.
"""

import argparse
import datetime
import json
import os
import re
import sys
import urllib.request

RAW = "https://raw.githubusercontent.com/OBDb/{repo}/main/{path}"
MAX_SIGNALS = 8


def fetch(repo, path):
    req = urllib.request.Request(RAW.format(repo=repo, path=path), headers={"User-Agent": "track-rat-obd-car"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode("utf-8")


def year_matches(f, year):
    """OBDb's filter: any of 'to' (up to), 'from' (on), 'years' (exactly). Empty: every year."""
    if not f:
        return True
    return ("to" in f and year <= f["to"]) or ("from" in f and year >= f["from"]) or year in f.get("years", [])


def command_key(c):
    svc, did = next(iter(c["cmd"].items()))
    return (c["hdr"], c.get("rax") or "", svc + did)


def for_year(commands, year):
    return [c for c in commands if year_matches(c.get("filter"), year)]


def merge(model, make, year):
    """The model's commands for the year, then the make's for the year that the model doesn't have
    (a model command for other years doesn't hide the make's)."""
    mine = for_year(model.get("commands", []), year)
    seen = {command_key(c) for c in mine}
    out = [dict(c, source="model") for c in mine]
    out += [dict(c, source="make") for c in for_year(make.get("commands", []), year) if command_key(c) not in seen]
    return out


def parse_support(text):
    """OBDb's tests/test_cases/<year>/command_support.yaml: (seen, not seen) request keys 'HDR.RAX.SVCDID' / 'HDR.SVCDID'."""
    seen, unseen, cur = set(), set(), None
    for line in text.splitlines():
        if line.startswith("supported_commands_by_ecu"):
            cur = seen
        elif line.startswith("unsupported_commands_by_ecu"):
            cur = unseen
        elif cur is not None and line.strip().startswith("- "):
            cur.add(line.strip()[2:].split("|")[0].split(":")[0])
    return seen, unseen


def status(c, year, seen, unseen):
    hdr, rax, req = command_key(c)
    keys = {f"{hdr}.{rax}.{req}", f"{hdr}.{req}"} if rax else {f"{hdr}.{req}"}
    if keys & seen:
        return "seen"
    if keys & unseen:
        return "not seen"
    if c.get("dbg") or year_matches(c.get("dbgfilter"), year) and c.get("dbgfilter"):
        return "unverified"
    return ""


def load(repo, year):
    make = repo.split("-")[0]
    model = json.loads(fetch(repo, "signalsets/v3/default.json"))
    try:
        makeset = json.loads(fetch(make, "signalsets/v3/default.json")) if make != repo else {"commands": []}
    except Exception:
        makeset = {"commands": []}
    try:
        seen, unseen = parse_support(fetch(repo, f"tests/test_cases/{year}/command_support.yaml"))
    except Exception:
        seen, unseen = set(), set()
    return merge(model, makeset, year), seen, unseen, make


def build_profile(commands, ids, name, vin, year, repo, make, hz=None):
    """The profile: the chosen signals' commands, each with only those signals."""
    want, out, found = list(ids), [], set()
    for c in commands:
        sigs = [s for s in c.get("signals", []) if s["id"] in want and s["id"] not in found]
        if not sigs:
            continue
        found.update(s["id"] for s in sigs)
        cmd = {k: c[k] for k in ("hdr", "rax", "eax", "pri", "fcm1", "cmd") if k in c}
        cmd["freq"] = round(1.0 / hz, 3) if hz else c.get("freq", 1)
        cmd["signals"] = [{"id": s["id"], "name": s.get("name", s["id"]), "fmt": s["fmt"]} for s in sigs]
        out.append(cmd)
    missing = [i for i in want if i not in found]
    if missing:
        raise SystemExit(f"Not found for {year}: {', '.join(missing)} (python tools/obd_car.py list {repo} --year {year})")
    if len(found) > MAX_SIGNALS:
        raise SystemExit(f"{len(found)} signals: Track Rat logs up to {MAX_SIGNALS}")
    sources = [f"https://github.com/OBDb/{repo}"] + ([f"https://github.com/OBDb/{make}"] if make != repo else [])
    return {
        "name": name, "vin": vin or "", "year": year,
        "license": "CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/)",
        "attribution": "Adapted from OBDb (" + ", ".join(sources) + "), CC BY-SA 4.0, on " + datetime.date.today().isoformat()
                       + " by Track Rat's tools/obd_car.py. Changes: only the chosen signals, for model year %d%s." % (
                           year, ", asked %g times a second" % hz if hz else ""),
        "commands": out,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["list", "make"])
    ap.add_argument("repo", help="the OBDb model repository, e.g. BMW-1-Series or Honda-Civic")
    ap.add_argument("--year", type=int, required=True, help="the car's model year")
    ap.add_argument("--find", help="list: only signals whose name or ID has this (e.g. brake)")
    ap.add_argument("--signals", help="make: signal IDs, comma separated")
    ap.add_argument("--name", help="make: the car's name on Track Rat (default: the repository)")
    ap.add_argument("--vin", default="", help="make: the start of the car's VIN (picks this profile for that car)")
    ap.add_argument("--hz", type=float, help="make: ask this often (default: OBDb's)")
    ap.add_argument("--out", default="obd", help="make: the folder to write to (copy it to the SD card's root)")
    a = ap.parse_args()
    commands, seen, unseen, make = load(a.repo, a.year)
    if a.action == "list":
        for c in commands:
            st = status(c, a.year, seen, unseen)
            for s in c.get("signals", []):
                if a.find and a.find.lower() not in (s["id"] + " " + s.get("name", "")).lower():
                    continue
                svc, did = next(iter(c["cmd"].items()))
                print(f"{s['id']:32} {s.get('name', ''):40} {c['hdr']}/{c.get('rax', '-')} {svc} {did:6} {c['source']:5} {st}")
        if not seen and not unseen:
            print(f"(OBDb has no recorded answers from a {a.year} car)")
        return
    if not a.signals:
        sys.exit("make needs --signals")
    p = build_profile(commands, [s.strip() for s in a.signals.split(",") if s.strip()], a.name or a.repo, a.vin, a.year, a.repo,
                      make, a.hz)
    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, re.sub(r"[^A-Za-z0-9_-]+", "-", p["name"]).strip("-") + ".json")
    json.dump(p, open(path, "w", encoding="utf-8"), indent=1)
    print(f"Wrote {path}: {sum(len(c['signals']) for c in p['commands'])} signal(s). Copy the {a.out} folder to the SD card.")


if __name__ == "__main__":
    main()
