// Host-side tests for the Wi-Fi page's session list (the "session names"
// part of web/index.html: parsing file names and grouping by day and track),
// its firmware file check (the "firmware check" part), and its Settings tab
// (the "settings" part: the field list, the time zone and speed text, the
// form body, and that the names and values agree with include/SettingsData.h), and the
// track editor's rules (the "track editor" part: reading a .vbo, track ids, moving the line,
// splits, corner names, new layouts), and the lap comparison (the "lap compare" part: laps from a
// log, speed and gap against distance, who was faster where, the season).
//
//   node test/test_web.js

"use strict";
const fs = require("fs");
const path = require("path");

// Line endings are normalised: a Windows checkout has CRLF, and the patterns below expect \n.
const readText = (...parts) => fs.readFileSync(path.join(__dirname, "..", ...parts), "utf8").replace(/\r\n/g, "\n");
const html = readText("web", "index.html");
const m = html.match(/\/\/ ---- session names \(begin[^\n]*\n([\s\S]*?)\/\/ ---- session names \(end\)/);
if (!m) {
  console.log("FAIL: session names section not found in web/index.html");
  process.exit(1);
}
const {parseFile, groupSessions, trackName} = new Function(m[1] + "\nreturn {parseFile, groupSessions, trackName};")();

const fm = html.match(/\/\/ ---- firmware check \(begin[^\n]*\n([\s\S]*?)\/\/ ---- firmware check \(end\)/);
if (!fm) {
  console.log("FAIL: firmware check section not found in web/index.html");
  process.exit(1);
}
const {checkFirmware, MAX_FIRMWARE} = new Function(fm[1] + "\nreturn {checkFirmware, MAX_FIRMWARE};")();

const sm = html.match(/\/\/ ---- settings \(begin[^\n]*\n([\s\S]*?)\/\/ ---- settings \(end\) ----/);
if (!sm) {
  console.log("FAIL: settings section not found in web/index.html");
  process.exit(1);
}
const {SETTINGS, tzText, speedText, settingBody} = new Function(sm[1] + "\nreturn {SETTINGS, tzText, speedText, settingBody};")();
const header = readText("include", "SettingsData.h");

const em = html.match(/\/\/ ---- track editor \(begin[^\n]*\n([\s\S]*?)\/\/ ---- track editor \(end\) ----/);
if (!em) {
  console.log("FAIL: track editor section not found in web/index.html");
  process.exit(1);
}
const cm = html.match(/\/\/ ---- lap compare \(begin[^\n]*\n([\s\S]*?)\/\/ ---- lap compare \(end\) ----/);
if (!cm) {
  console.log("FAIL: lap compare section not found in web/index.html");
  process.exit(1);
}
// The comparison uses the editor's parseVbo and toLocal, as on the page
const CMP = new Function(em[1] + cm[1] + "\nreturn {parseVbo, lapTrace, atDistance, compareLaps, fasterRuns, season, lapTimeText, gapText};")();
const ED = new Function(em[1] + "\nreturn {toLocal, fromLocal, parseVbo, slug, trackList, findTrack, nearestIndex, headingAt," +
                        " setLine, flipLine, addSplit, removeSplit, renameSector, addLayout};")();

let failures = 0;
function check(cond, msg) {
  if (!cond) {
    failures++;
    console.log("FAIL: " + msg);
  }
}
const file = (name, fsName = "sd", size = 100) => ({name, fs: fsName, size});
const utcDay = (d) => d.toISOString().slice(0, 10);

console.log("Wi-Fi page session list tests");

// Parsing
let p = parseFile(file("20261018_093012_calabogie.json"));
check(p && p.track === "calabogie" && p.label === "JSON", "plain session");
check(p && p.when.toISOString() === "2026-10-18T09:30:12.000Z", "UTC time " + (p && p.when.toISOString()));
p = parseFile(file("20261018_093012_calabogie_perf.json"));
check(p && p.track === "calabogie" && p.label === "PERF JSON", "perf file");
check(p && p.key === parseFile(file("20261018_093012_calabogie.vbo")).key, "perf file in the same session");
p = parseFile(file("20261018_093012_calabogie_2.json"));
check(p && p.track === "calabogie", "second session that minute: same track");
check(p && p.key !== parseFile(file("20261018_093012_calabogie.json")).key, "_2 is a different session");
p = parseFile(file("20261018_093012_my-track-12.json"));
check(p && p.track === "my-track-12", "track id ending in digits after a hyphen");
p = parseFile(file("20261019_101500_calabogie.full.vbo"));
check(p && p.track === "calabogie.full" && p.label === "VBO", "layout id with a dot");
p = parseFile(file("SIM_12_bench.vbo"));
check(p && p.track === "bench" && p.label === "VBO", "simulated session name");
p = parseFile(file("20261018_093012_calabogie.json.bak"));
check(p && p.track === "calabogie" && p.label === "JSON.BAK", "extra extension stays with its session");
p = parseFile(file("20261018_093012_calabogie"));
check(p && p.label === "FILE", "no extension");
check(parseFile(file("20261018_093012_freeroam.json")).track === "freeroam", "free roam");
check(parseFile(file("20261018_093012_perf.json")).track === "perf", "a track called perf");
check(parseFile(file("20261018_093012__2.json")) !== null, "odd but harmless name");
for (const bad of ["session_bench.json", "tracks.json", "", "2026101_093012_x.json", "20261018-093012_x.json",
                   "20261340_093012_x.json", "20260231_093012_x.json", "20261018_256012_x.json",
                   "20261018_096012_x.json", "20261018_093012_.json"]) {
  check(parseFile(file(bad)) === null, `"${bad}" goes to Other files`);
}
check(parseFile(file("20261018_093012_calabogie.json", "sd")).key !== parseFile(file("20261018_093012_calabogie.json", "flash")).key,
      "same name on SD and in flash: two sessions");

// Track names
check(trackName("calabogie") === "Calabogie", "track name");
check(trackName("nurburgring-gp") === "Nurburgring Gp", "hyphens");
check(trackName("my-track-3") === "My Track 3", "my-track-3");
check(trackName("freeroam") === "Free Roam" && trackName("bench") === "Bench Test", "special names");
check(trackName("perf") === "Performance", "perf is Performance");
p = parseFile(file("SIM_12_perf.json"));
check(p && p.track === "perf" && trackName(p.track) === "Performance", "simulated performance demo");
check(trackName("-x-") === "X", "stray hyphens");

// Grouping
const g = groupSessions([
  file("20261017_200000_freeroam.json"),
  file("20261018_093012_calabogie.json"),
  file("20261018_093012_calabogie.vbo", "sd", 3500000),
  file("20261018_093012_calabogie_perf.json"),
  file("20261018_151500_calabogie.json"),
  file("20261018_120000_shannonville.json"),
  file("session_bench.json"),
  file("notes.txt"),
], utcDay);
check(g.days.length === 2, "two days");
check(g.days[0][0] === "2026-10-18" && g.days[1][0] === "2026-10-17", "newest day first");
const oct18 = g.days[0][1];
check(oct18.length === 2 && oct18[0][0] === "calabogie" && oct18[1][0] === "shannonville",
      "tracks ordered by their newest session: " + oct18.map((t) => t[0]));
const cal = oct18[0][1];
check(cal.length === 2, "two Calabogie sessions");
check(cal[0].when > cal[1].when, "newest session first");
check(cal[1].files.map((x) => x.label).join(",") === "JSON,PERF JSON,VBO", "files together, sorted: " +
      cal[1].files.map((x) => x.label));
check(g.other.length === 2, "two other files");

const empty = groupSessions([], utcDay);
check(empty.days.length === 0 && empty.other.length === 0, "no files");

// Time zones: a session at 02:00 UTC is the previous day in Ontario
const local = groupSessions([file("20261018_020000_calabogie.json")], (d) => new Date(d - 4 * 3600e3).toISOString().slice(0, 10));
check(local.days[0][0] === "2026-10-17", "day uses the phone's time zone");
// Names written in local time (device told UTC-4): 22:00 local on the 17th is 02:00 UTC on the 18th
p = parseFile(file("20261017_220000_calabogie.json"), -240);
check(p && p.when.toISOString() === "2026-10-18T02:00:00.000Z", "local name to UTC " + (p && p.when.toISOString()));
const named = groupSessions([file("20261017_220000_calabogie.json")], (d) => new Date(d - 4 * 3600e3).toISOString().slice(0, 10), -240);
check(named.days[0][0] === "2026-10-17", "local name stays on its own day");
p = parseFile(file("20261340_220000_calabogie.json"), -240);
check(p === null, "bad date with a time zone still rejected");

// Logbook notes: kept with their session, not as a file chip, under the log's name
const ng = groupSessions([
  file("20261018_093012_calabogie.json"),
  file("20261018_093012_calabogie_note.json"),
  file("20261018_093012_calabogie.vbo"),
  file("20261018_151500_calabogie.json"),
  file("SIM_12_bench.vbo"),
  file("SIM_12_bench_note.json"),
], utcDay);
const ncal = ng.days[0][1].find((t) => t[0] === "calabogie")[1];
check(ncal.length === 2, "a note isn't a session of its own: " + ncal.length);
const withNote = ncal.find((x) => x.noteFile);
check(withNote && withNote.noteFile.name === "20261018_093012_calabogie_note.json", "note found");
check(withNote && withNote.files.map((x) => x.label).join(",") === "JSON,VBO", "note not a file chip: " +
      (withNote && withNote.files.map((x) => x.label)));
check(withNote && withNote.base === "20261018_093012_calabogie", "base " + (withNote && withNote.base));
const noNote = ncal.find((x) => !x.noteFile);
check(noNote && noNote.base === "20261018_151500_calabogie", "base from the summary " + (noNote && noNote.base));
const sim = ng.days.flatMap((d) => d[1]).find((t) => t[0] === "bench")[1][0];
check(sim.noteFile && sim.base === "SIM_12_bench" && sim.files.length === 1, "simulated session's note");
p = parseFile(file("20261018_093012_calabogie_note.json.bak"));
check(p && p.label === "JSON.BAK", "only _note.json is a note: " + (p && p.label));
const perfNote = groupSessions([file("20261018_093012_perf_perf.json"), file("20261018_093012_perf_perf_note.json")], utcDay);
const ps = perfNote.days[0][1][0][1];
check(ps.length === 1 && ps[0].noteFile && ps[0].base === "20261018_093012_perf_perf", "performance session's note: " +
      (ps[0] && ps[0].base));

// Firmware files: same words as firmware::describe in include/FirmwareImage.h
function image(size, chip = 0x0009, appDesc = true) {
  const b = new Uint8Array(size);
  b[0] = 0xE9;
  b[1] = 5;
  b[12] = chip & 0xFF;
  b[13] = chip >> 8;
  if (appDesc) b.set([0x32, 0x54, 0xCD, 0xAB], 32);
  return b;
}
const fw = (b, fileSize = b.length) => checkFirmware(b.subarray(0, 128), fileSize);
check(fw(image(800000)) === null, "good app image refused: " + fw(image(800000)));
check(fw(image(128)) === null, "smallest image refused");
check(fw(image(MAX_FIRMWARE)) === null, "exactly slot-sized image refused");
check(fw(image(1000), MAX_FIRMWARE + 1) === "file is too big for the app slot", "oversize accepted");
check(fw(image(100)) === "file is too small to be firmware", "100-byte file accepted");
check(checkFirmware(image(800000).subarray(0, 50), 800000) === "file is too small to be firmware", "short read accepted");
const text = image(4096);
text.set(new TextEncoder().encode("Hello, this is not firmware"));
check(fw(text) === "not a firmware image", "text file accepted");
const noSegs = image(4096);
noSegs[1] = 0;
check(fw(noSegs) === "not a firmware image", "zero segments accepted");
noSegs[1] = 17;
check(fw(noSegs) === "not a firmware image", "17 segments accepted");
check(fw(image(4096, 0x0000)) === "firmware is for a different chip", "original ESP32 image accepted");
check(fw(image(4096, 0x0100)) === "firmware is for a different chip", "chip id is little-endian, both bytes count");
check(fw(image(4096, 0x0009, false)) === "not the app image (use firmware.bin)", "bootloader accepted");
const hi = image(4096);
hi[35] = 0xAA;  // top byte of the descriptor magic: must not be a signed-shift accident
check(fw(hi) === "not the app image (use firmware.bin)", "wrong top byte of the app descriptor magic accepted");

// Settings tab: the page's own helpers
check(tzText(0) === "UTC+0:00", "tzText(0): " + tzText(0));
check(tzText(-240) === "UTC−4:00", "tzText(-240): " + tzText(-240));
check(tzText(330) === "UTC+5:30", "tzText(330): " + tzText(330));
check(tzText(-30) === "UTC−0:30", "tzText(-30): " + tzText(-30));
check(tzText(840) === "UTC+14:00", "tzText(840): " + tzText(840));
check(tzText(-720) === "UTC−12:00", "tzText(-720): " + tzText(-720));
check(speedText(100, "kmh") === "100 km/h", "speedText 100 km/h: " + speedText(100, "kmh"));
check(speedText(100, "mph") === "62 mph", "speedText 100 in mph: " + speedText(100, "mph"));
check(speedText(160.934, "mph") === "100 mph", "speedText 160.934 in mph: " + speedText(160.934, "mph"));
check(speedText(0, "kmh") === "0 km/h", "speedText 0: " + speedText(0, "kmh"));
check(settingBody("units", "mph", "0042").toString() === "units=mph&code=0042",
      "settingBody units: " + settingBody("units", "mph", "0042").toString());
check(settingBody("upsideDown", true, "0042").toString() === "upsideDown=true&code=0042", "boolean true goes as \"true\"");
check(settingBody("bluetooth", false, "0042").toString() === "bluetooth=false&code=0042", "boolean false goes as \"false\"");
check(settingBody("brightness", 70, "0042").get("brightness") === "70", "number goes as text");
check(settingBody("brightness", 70, "0042") instanceof URLSearchParams, "settingBody returns URLSearchParams");

// Settings tab: the page's names and values against the device (include/SettingsData.h).
// Each function's text is cut from the header: from its "inline" line to the closing brace at column 0.
const functionText = (name) => {
  const f = header.match(new RegExp("inline [\\w:*]+ " + name + "\\([\\s\\S]*?\\n\\}\\n"));
  return f ? f[0] : "";
};
const setFieldSrc = functionText("setField");
const toJsonSrc = functionText("toJson");
const parseBoolSrc = functionText("parseBool");
check(setFieldSrc !== "" && toJsonSrc !== "" && parseBoolSrc !== "", "setField, toJson and parseBool found in SettingsData.h");
const headerInt = (name) => {
  const m = header.match(new RegExp("\\b" + name + " = (-?\\d+)"));
  return m ? Number(m[1]) : NaN;
};
// The part of setField for one key: from its "if" up to the next branch
const setFieldBranch = (key) => {
  const start = setFieldSrc.indexOf(`!strcmp(key, "${key}")`);
  if (start < 0) return "";
  const rest = setFieldSrc.slice(start);
  const end = rest.slice(1).search(/\} else (if \(|\{)/);
  return end < 0 ? rest : rest.slice(0, end + 1);
};
// The header writes a key inside a C string as \"name\":
const jsonKey = (k) => '\\"' + k + '\\":';
// Page range fields and the header constants that give their limits and step
const RANGE_CONSTS = {brightness: ["BRIGHTNESS_MIN", "BRIGHTNESS_MAX", "BRIGHTNESS_STEP"]};

check(parseBoolSrc.includes('"true"') && parseBoolSrc.includes('"false"'), "parseBool takes \"true\" and \"false\"");
const fieldNames = SETTINGS.map((f) => f.name);
check(new Set(fieldNames).size === fieldNames.length, "setting names are unique");
for (const f of SETTINGS) {
  const where = `"${f.name}"`;
  check(setFieldSrc.includes(`!strcmp(key, "${f.name}")`), `setField takes ${where}`);
  check(toJsonSrc.includes(jsonKey(f.name)), `toJson writes ${where}`);
  if (f.options) {
    const vals = f.options.map((o) => o[0]);
    if (vals.every((v) => typeof v === "boolean")) {
      check(vals.includes(true) && vals.includes(false), `${where} offers true and false`);
      check(setFieldBranch(f.name).includes("parseBool(v, &b)"), `setField reads ${where} with parseBool`);
      check(toJsonSrc.includes('"true"') && toJsonSrc.includes('"false"'), `toJson sends ${where} as true or false`);
    } else {
      for (const v of vals) {
        check(typeof v === "string", `${where} option "${v}" is text or true/false`);
        check(setFieldBranch(f.name).includes(`!strcmp(v, "${v}")`), `setField takes ${where} = "${v}"`);
        check(toJsonSrc.includes(`"${v}"`), `toJson can send ${where} = "${v}"`);
      }
    }
  }
  if (f.range) {
    const [lo, hi, step] = f.range;
    const consts = RANGE_CONSTS[f.name] || [];
    check(consts.length === 3 && lo === headerInt(consts[0]) && hi === headerInt(consts[1]) && step === headerInt(consts[2]),
          `${where} range ${f.range} matches the header limits`);
    check(setFieldBranch(f.name).includes("parseInt("), `setField reads ${where} as a number`);
    check(toJsonSrc.includes(jsonKey(f.name) + "%d"), `toJson sends ${where} as a number`);
  }
}
// Keys the page reads that aren't settings
check(toJsonSrc.includes(jsonKey("tzMin") + "%d"), "toJson writes a number \"tzMin\" (the page shows it)");
check(toJsonSrc.includes(jsonKey("bluetoothBuilt")), "toJson writes \"bluetoothBuilt\" (the page reads it)");

// Settings tab: every entry has a label and either options (two or more, unique) or a range
for (const f of SETTINGS) {
  const where = `"${f.name}"`;
  check(typeof f.label === "string" && f.label !== "", `${where} has a label`);
  const hasOptions = f.options !== undefined, hasRange = f.range !== undefined;
  check(hasOptions !== hasRange, `${where} has options or a range, not both`);
  if (hasOptions) {
    const vals = f.options.map((o) => String(o[0]));
    check(f.options.length >= 2 && new Set(vals).size === vals.length, `${where} has two or more unique options`);
    check(f.options.every((o) => typeof o[1] === "string" && o[1] !== ""), `${where} options all have text`);
  }
  if (hasRange) {
    check(f.range.length === 3 && f.range.every((n) => typeof n === "number"), `${where} range is [lo, hi, step] numbers`);
  }
}


// ---- the track editor ----
console.log("Wi-Fi page track editor tests");
{
  const near = (a, b, tol, msg) => check(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b}`);
  // toLocal / fromLocal: the device's numbers (GeoMath.h), and back again
  const p = ED.toLocal(45.001, -74.999, 45, -75);
  near(p.y, 111.195, 0.01, "1/1000 degree north");
  near(p.x, 78.63, 0.01, "1/1000 degree east at 45 N");
  const back = ED.fromLocal(p.x, p.y, 45, -75);
  near(back.lat, 45.001, 1e-9, "back to lat");
  near(back.lon, -74.999, 1e-9, "back to lon");

  // A log as the device writes it (VboFormat.cpp): minutes, west positive, CRLF, a cut last line
  const vbo = ["File created on 18/10/2026 @ 10:00:00", "", "[header]", "satellites", "time", "", "[comments]",
    "Track Rat GPS lap timer", "Track: calabogie", "Start/finish: 45.1 -76.6 15", "", "[column names]",
    "sats time lat long velocity heading height HDOP Lap LateralG LongitudinalG LapTag", "", "[data]",
    "014 100000.000 +02706.00000000 +04596.00000000 120.000 090.00 +00100.00 0.6 0 +0.00 +0.00 0",
    "014 100000.050 +02706.00060000 +04595.99900000 121.000 090.00 +00100.00 0.6 1 +0.00 +0.00 0",
    "014 100000.100 +02706.0012"].join("\r\n");
  const log = ED.parseVbo(vbo);
  check(log.comments["Track"] === "calabogie" && log.comments["Start/finish"] === "45.1 -76.6 15", "comments");
  check(log.points.length === 2, "two whole fixes, the cut one left out: " + log.points.length);
  near(log.points[0].lat, 45.1, 1e-9, "lat from minutes");
  near(log.points[0].lon, -76.6, 1e-9, "lon from minutes, sign reversed");
  check(log.points[0].kmh === 120 && log.points[0].lap === 0 && log.points[1].lap === 1, "speed and lap");
  check(ED.parseVbo("").points.length === 0, "an empty file");

  // Track ids, as TrackDatabase.cpp makes them
  const db = () => ({tracks: [
    {id: "calabogie", name: "Calabogie", startFinish: {lat: 45.1, lon: -76.6, radius: 15}, map: [[[45, -76], [45.1, -76.1]]],
     sectors: [{lat: 45.2, lon: -76.7, radius: 15, heading: 90}]},
    {id: "mosport", name: "CTMP", layouts: [{name: "Grand Prix", startFinish: {lat: 44, lon: -78, radius: 15}},
                                             {name: "Driver Development Track", id: "ddt"}, {name: "Grand Prix"}]},
  ]});
  check(ED.slug("Short Course!") === "short-course" && ED.slug("  -GP 2- ") === "gp-2", "slug");
  const ids = ED.trackList(db()).map((t) => t.id);
  check(JSON.stringify(ids) === JSON.stringify(["calabogie", "mosport.grand-prix", "mosport.ddt", "mosport.grand-prix-3"]),
        "track ids " + ids.join(", "));
  check(ED.findTrack(db(), "mosport.ddt").obj.id === "ddt" && ED.findTrack(db(), "nope") === null, "findTrack");

  // A path: east along y = 0, then north
  const path = [];
  for (let x = 0; x <= 100; x += 5) path.push({x, y: 0});
  for (let y = 5; y <= 100; y += 5) path.push({x: 100, y});
  check(ED.nearestIndex(path, 52, 3) === 10, "nearest point");
  near(ED.headingAt(path, 5), 90, 0.01, "heading east");
  near(ED.headingAt(path, 30), 0, 0.01, "heading north");
  near(ED.headingAt(path, 20), 45, 1, "round the corner");

  // Moving the line drops the corner sectors (numbered from the old line), keeps the gate's width
  let d = db();
  let t = ED.findTrack(d, "calabogie");
  check(ED.setLine(t.obj, 45.10000004, -76.6, 90) === true, "sectors were there");
  check(t.obj.startFinish.lat === 45.1 && t.obj.startFinish.heading === 90 && t.obj.startFinish.radius === 15, "line moved");
  check(!("sectors" in t.obj), "sectors dropped");
  check(ED.setLine(t.obj, 45.1, -76.6, 90) === false, "no sectors the second time");
  check(ED.flipLine(t.obj) && t.obj.startFinish.heading === 270, "flipped");
  check(ED.flipLine(t.obj) && t.obj.startFinish.heading === 90, "flipped back");
  check(!ED.flipLine(ED.findTrack(d, "mosport.grand-prix").obj), "no direction to flip");

  // Splits are kept in lap order, however they're added
  const ref = {lat: 45.1, lon: -76.6};
  const at = (x, y) => ED.fromLocal(x, y, ref.lat, ref.lon);
  let q = at(100, 50);
  ED.addSplit(t.obj, q.lat, q.lon, 0, path, ref.lat, ref.lon);
  q = at(50, 0);
  ED.addSplit(t.obj, q.lat, q.lon, 90, path, ref.lat, ref.lon);
  check(t.obj.splits.length === 2 && t.obj.splits[0].heading === 90 && t.obj.splits[1].heading === 0, "splits in lap order");
  check(t.obj.splits[0].radius === 15, "split as wide as the line");
  check(ED.removeSplit(t.obj, 0) && t.obj.splits.length === 1 && t.obj.splits[0].heading === 0, "removed S1");
  check(ED.removeSplit(t.obj, 0) && !("splits" in t.obj), "no splits left: the key goes");
  check(!ED.removeSplit(t.obj, 0), "nothing to remove");
  // Counted from the start/finish, wherever the log's lap began: with the line moved round the corner,
  // a split just after it is S1 and one before the corner is the last
  const moved = {startFinish: (() => { const m = at(100, 50); return {lat: m.lat, lon: m.lon, radius: 15, heading: 0}; })()};
  q = at(50, 0);
  ED.addSplit(moved, q.lat, q.lon, 90, path, ref.lat, ref.lon);
  q = at(100, 75);
  ED.addSplit(moved, q.lat, q.lon, 0, path, ref.lat, ref.lon);
  check(moved.splits[0].heading === 0 && moved.splits[1].heading === 90, "splits counted from the moved line");

  // Corner names
  d = db();
  t = ED.findTrack(d, "calabogie");
  check(ED.renameSector(t.obj, 0, "  The Wall  ") && t.obj.sectors[0].name === "The Wall", "named");
  check(ED.renameSector(t.obj, 0, "") && !("name" in t.obj.sectors[0]), "name cleared");
  check(!ED.renameSector(t.obj, 5, "x"), "no such corner");
  check(ED.renameSector(t.obj, 0, "x".repeat(40)) && t.obj.sectors[0].name.length === 24, "names are kept short");

  // A venue split into layouts: what it had becomes "Main"
  d = db();
  const id = ED.addLayout(d, "calabogie", "Short");
  const v = d.tracks[0];
  check(id === "calabogie.short", "new layout id " + id);
  check(v.layouts.length === 2 && v.layouts[0].name === "Main" && v.layouts[1].name === "Short", "Main and Short");
  check(!("startFinish" in v) && !("map" in v) && !("sectors" in v), "moved off the venue");
  check(v.layouts[0].sectors && !v.layouts[1].sectors, "corners stay with Main");
  check(JSON.stringify(v.layouts[1].startFinish) === JSON.stringify(v.layouts[0].startFinish), "Short starts from Main's line");
  v.layouts[1].startFinish.lat = 1;
  check(v.layouts[0].startFinish.lat === 45.1, "a copy, not the same object");
  check(ED.findTrack(d, "calabogie.main") && ED.findTrack(d, "calabogie.short"), "both listed");
  check(ED.addLayout(d, "calabogie.short", "main") === null, "a taken name");
  check(ED.addLayout(d, "calabogie.short", "  ") === null, "no name");
  check(ED.addLayout(d, "calabogie.short", "Club") === "calabogie.club" && v.layouts.length === 3, "a third layout");
  check(ED.addLayout(d, "nope", "X") === null, "no such track");
}


// ---- lap comparison ----
console.log("Wi-Fi page lap comparison tests");
{
  const near = (a, b, tol, msg) => check(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b}`);
  // A 1000 m straight east from 45 N, logged at 20 Hz: lap 1 at 100 km/h, lap 2 at 100 for the
  // first half and 50 for the second, and a fix of lap 3 that runs past midnight
  const pts = [];
  const mPerDegLon = 6371000 * Math.PI / 180 * Math.cos(45 * Math.PI / 180);
  let ms = 86400000 - 30000;  // starts 30 s before midnight
  const drive = (lap, speedAt) => {
    let x = 0;
    while (x <= 1000) {
      const kmh = speedAt(x);
      pts.push({lat: 45, lon: -75 + x / mPerDegLon, ms: ms % 86400000, kmh, lap});
      x += kmh / 3.6 * 0.05;
      ms += 50;
    }
  };
  drive(1, () => 100);
  drive(2, (x) => (x < 500 ? 100 : 50));
  const a = CMP.lapTrace(pts, 1, 45, -75), b = CMP.lapTrace(pts, 2, 45, -75);
  near(a[a.length - 1].d, 1000, 3, "lap A is 1000 m");
  near(a[a.length - 1].t, 36000, 60, "lap A takes 36 s, across midnight");
  check(a.every((p, i) => i === 0 || p.t > a[i - 1].t), "time only goes up, even past midnight");
  near(b[b.length - 1].t, 54000, 120, "lap B takes 54 s");
  check(CMP.lapTrace(pts, 7, 45, -75).length === 0, "no such lap");

  const mid = CMP.atDistance(a, 250);
  near(mid.t, 9000, 60, "250 m in: 9 s");
  near(mid.x, 250, 3, "and 250 m east");
  check(CMP.atDistance(a, -5).t === 0 && CMP.atDistance(a, 5000).t === a[a.length - 1].t, "clamped at the ends");
  check(CMP.atDistance([], 5) === null, "no trace");

  const cmp = CMP.compareLaps(a, b, 10);
  check(cmp.d.length >= 100 && cmp.d[1] === 10, "every 10 m");
  near(cmp.gap[0], 0, 1, "level at the line");
  near(cmp.gap[49], 0, 80, "level half way: " + cmp.gap[49]);
  near(cmp.gap[cmp.gap.length - 1], 18000, 200, "B 18 s behind at the end");
  check(cmp.kmhA[70] === 100 && cmp.kmhB[70] === 50, "speeds in the second half");

  // B lost all its time in the second half: A is faster there, nobody before
  const runs = CMP.fasterRuns(cmp);
  const last = runs[runs.length - 1];
  check(last.who === "A" && cmp.d[last.from] <= 520 && last.to === cmp.d.length - 1, "A faster through the second half");
  check(runs.every((r, i) => i === 0 || r.who !== runs[i - 1].who), "runs alternate");
  check(runs[0].from === 0, "runs start at the line");

  // B's lap measured 2% long (a wider line): stretched to A's, so the same point lines up
  const longer = b.map((p) => Object.assign({}, p, {d: p.d * 1.02}));
  const c2 = CMP.compareLaps(a, longer, 10);
  near(c2.gap[49], 0, 80, "a longer lap still lines up half way");
  check(CMP.compareLaps(a.slice(0, 1), b).d.length === 0, "a lap of one fix can't be compared");

  // The log's time column, read back by parseVbo
  const vbo = ["[comments]", "Track: t", "", "[column names]", "sats time lat long velocity heading height HDOP Lap LateralG LongitudinalG LapTag",
    "", "[data]", "014 235959.950 +02700.00000000 +04500.00000000 100.000 090.00 +00100.00 0.6 1 +0.00 +0.00 0",
    "014 000000.000 +02700.00000000 +04499.99900000 100.000 090.00 +00100.00 0.6 1 +0.00 +0.00 0"].join("\r\n");
  const lp = CMP.parseVbo(vbo).points;
  check(lp[0].ms === 86399950 && lp[1].ms === 0, "times from hhmmss.sss: " + lp.map((p) => p.ms));
  near(CMP.lapTrace(lp, 1, 45, -75)[1].t, 50, 0, "50 ms across midnight");

  // The season: best per visit, oldest first, and the PB as it stood
  const se = CMP.season([
    {started: "20260601_100000", bestTimeMs: 105000}, {started: "20260601_140000", bestTimeMs: 103500},
    {started: "20260715_090000", bestTimeMs: 104000}, {started: "20260820_090000", bestTimeMs: 101900},
    {started: "20260901_090000"}, {started: "nonsense", bestTimeMs: 90000}, {started: "20260902_090000", bestTimeMs: 0}]);
  check(se.length === 3, "three visits with a timed lap: " + se.length);
  check(se[0].day === "20260601" && se[0].best === 103500 && se[0].isPb, "first visit, its best session");
  check(se[1].best === 104000 && se[1].pb === 103500 && !se[1].isPb, "a slower visit keeps the PB");
  check(se[2].best === 101900 && se[2].pb === 101900 && se[2].isPb, "a new PB");
  check(CMP.season([]).length === 0, "no sessions");

  check(CMP.lapTimeText(102345) === "1:42.345" && CMP.lapTimeText(59999) === "0:59.999", "lap time text");
  check(CMP.gapText(310) === "+0.31" && CMP.gapText(-1500) === "−1.50" && CMP.gapText(0) === "0.00", "gap text");
}

console.log(failures ? `\n${failures} FAILURE(S)` : "\nALL PASSED");
process.exit(failures ? 1 : 0);
