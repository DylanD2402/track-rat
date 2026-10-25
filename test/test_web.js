// Host-side tests for the Wi-Fi page's session list (the "session names"
// part of web/index.html: parsing file names and grouping by day and track),
// its firmware file check (the "firmware check" part), and its Settings tab
// (the "settings" part: the field list, the time zone and speed text, the
// form body, and that the names and values agree with include/SettingsData.h), and the
// track editor's rules (the "track editor" part: reading a .vbo, track ids, moving the line,
// splits, corner names, new layouts), and the lap comparison (the "lap compare" part: laps from a
// log, speed and gap against distance, who was faster where, the season), the viewer (the
// "viewer" part: when the page is a viewer, and pairing opened files into sessions), and the
// sample session in site/samples.
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
                        " setLine, flipLine, addSplit, removeSplit, renameSector, addLayout, isStage, setFinish, isKart};")();

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
const COOLANT_CONSTS = ["COOLANT_SET_MIN_C", "COOLANT_SET_MAX_C", "TEMP_STEP_C"];
const RANGE_CONSTS = {brightness: ["BRIGHTNESS_MIN", "BRIGHTNESS_MAX", "BRIGHTNESS_STEP"], coolantColdC: COOLANT_CONSTS,
                      coolantWarmC: COOLANT_CONSTS, coolantHotC: COOLANT_CONSTS, oilHotC: ["OIL_SET_MIN_C", "OIL_SET_MAX_C", "TEMP_STEP_C"]};

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

// Point to point: a course made on the device ("type": "stage") gets its finish set on the page
{
  const d = {tracks: [{id: "course-2026-10-20", name: "Course 2026-10-20", type: "stage",
                       startFinish: {lat: 45.1, lon: -76.1, radius: 15}},
                      {id: "calabogie", name: "Calabogie", startFinish: {lat: 45.3, lon: -76.7, radius: 15}}]};
  const course = ED.findTrack(d, "course-2026-10-20"), circuit = ED.findTrack(d, "calabogie");
  check(ED.isStage(course) && !ED.isStage(circuit), "a stage is a stage, a circuit isn't");
  ED.setFinish(course.obj, 45.10512345, -76.0987654321, 87.5);
  const f = d.tracks[0].finish;
  check(f && f.lat === 45.1051235 && f.lon === -76.0987654 && f.radius === 15 && f.heading === 87.5,
        "finish saved, rounded like the line: " + JSON.stringify(f));
  ED.setFinish(course.obj, 45.2, -76.2, null);
  check(d.tracks[0].finish.lat === 45.2 && d.tracks[0].finish.heading === null && d.tracks[0].type === "stage",
        "moved, no direction");
  check(JSON.stringify(d.tracks[0].startFinish) === JSON.stringify({lat: 45.1, lon: -76.1, radius: 15}), "start untouched");
}

// Karting: a kart track's lines with no width stay without one (Track Rat makes them 8 m)
{
  const d = {tracks: [{id: "kartplex", name: "Kartplex", kind: "kart", center: {lat: 45, lon: -75}},
                      {id: "venue", name: "Venue", kind: "kart", layouts: [{name: "Short"}]},
                      {id: "calabogie", name: "Calabogie", startFinish: {lat: 45.3, lon: -76.7, radius: 15}}]};
  const k = ED.findTrack(d, "kartplex");
  check(ED.isKart(k) && ED.isKart(ED.findTrack(d, "venue.short")) && !ED.isKart(ED.findTrack(d, "calabogie")),
        "kart tracks, a kart venue's layouts too");
  ED.setLine(k.obj, 45.001, -75.001, 90);
  check(!("radius" in k.obj.startFinish) && k.obj.startFinish.heading === 90, "no width given: " + JSON.stringify(k.obj.startFinish));
  ED.addSplit(k.obj, 45.002, -75.0, 0, [], 45, -75);
  check(k.obj.splits.length === 1 && !("radius" in k.obj.splits[0]), "a split with no width");
  const c = ED.findTrack(d, "calabogie");
  ED.setLine(c.obj, 45.31, -76.71, 10);
  check(c.obj.startFinish.radius === 15, "a line's own width is kept");
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

// ---- CSV for overlay tools (6.4) ----
{
  const near = (a, b, tol, msg) => check(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b}`);
  const xm = html.match(/\/\/ ---- csv \(begin[^\n]*\n([\s\S]*?)\/\/ ---- csv \(end\) ----/);
  check(!!xm, "the page has the CSV section");
  const {vboToCsv, CSV_COLUMNS} = new Function(xm[1] + "\nreturn {vboToCsv, CSV_COLUMNS};")();
  // A 100 m circle: lap 1 at 20 m/s, lap 2 at 10 m/s, then a moment of lap 3; RPM and so on logged
  const R = 100, C = 2 * Math.PI * R, rows = [];
  let t = 23 * 3600 + 59 * 60 + 50, d = 0;  // starts 10 s before midnight
  for (const [lap, len, v] of [[1, C, 20], [2, C, 10], [3, 5, 20]]) {
    const end = d + len;
    for (; d < end - 1e-9; d += v * 0.1, t += 0.1) {
      const a = d / R;
      const lat = 45 + R * Math.sin(a) / 6371000 * 180 / Math.PI;
      const lon = -75 + R * (1 - Math.cos(a)) / (6371000 * Math.cos(45 * Math.PI / 180)) * 180 / Math.PI;
      const sod = t % 86400, hh = Math.floor(sod / 3600), mm = Math.floor(sod / 60) % 60, ss = sod % 60;
      const time = String(hh).padStart(2, "0") + String(mm).padStart(2, "0") + ss.toFixed(3).padStart(6, "0");
      rows.push(`012 ${time} ${(lat * 60).toFixed(8)} ${(-lon * 60).toFixed(8)} ${(v * 3.6).toFixed(3)} 090.00 +00100.00 0.6 ${lap} +0.408 -0.100 0 06500 087.5 +093.0 +025.0`);
    }
  }
  const vbo = ["File created on 31/12/2025 @ 23:59:50", "", "[column names]",
    "sats time lat long velocity heading height HDOP Lap LateralG LongitudinalG LapTag RPM Throttle CoolantTemp IntakeTemp", "", "[data]",
    ...rows].join("\r\n");
  const lines = vboToCsv(vbo, null).trim().split("\n");
  check(lines[0] === CSV_COLUMNS.join(",") && lines.length === rows.length + 1, "a header and a row per fix: " + lines.length);
  const col = (line, name) => line.split(",")[CSV_COLUMNS.indexOf(name)];
  check(col(lines[1], "utc") === "2025-12-31T23:59:50.000Z", "UTC with the file's date: " + col(lines[1], "utc"));
  check(lines.some((l) => col(l, "utc").startsWith("2026-01-01T00:00:0")), "past midnight: the next day");
  check(Math.abs(+col(lines[1], "lat") - 45) < 1e-6 && Math.abs(+col(lines[1], "lon") + 75) < 1e-6, "degrees, west negative");
  check(col(lines[1], "speed_kmh") === "72.00" && col(lines[1], "rpm") === "6500" && col(lines[1], "throttle_pct") === "87.5" &&
        col(lines[1], "coolant_c") === "93.0" && col(lines[1], "g_lat") === "0.408" && col(lines[1], "g_long") === "-0.100", "values");
  const lap2 = lines.filter((l) => col(l, "lap") === "2");
  const mid = lap2[Math.floor(lap2.length / 2)];
  near(+col(mid, "lap_time_s"), C / 2 / 10, 0.15, "lap time half way round lap 2");
  near(+col(mid, "delta_s"), C / 2 / 10 - C / 2 / 20, 0.15, "lap 2 half way: behind lap 1");
  near(+col(lines[10], "delta_s"), 0, 0.01, "lap 1 against itself");
  const sum = vboToCsv(vbo, {bestLap: 2}).trim().split("\n");
  near(+col(sum[10], "delta_s"), -0.9, 0.06, "against the summary's best lap (2) instead");
  const noEngine = vboToCsv(vbo.replace(" RPM Throttle CoolantTemp IntakeTemp", "").split(" 06500 087.5 +093.0 +025.0").join(""), null);
  check(col(noEngine.split("\n")[1], "rpm") === "" && col(noEngine.split("\n")[1], "coolant_c") === "", "no engine columns: empty");
  check(col(lines[1], "lap_time_s") !== "" && vboToCsv("", null).trim() === CSV_COLUMNS.join(","), "an empty log: just the header");
  // Oil temperature (6.5): a fifth engine column; 0 (not known) is left empty, like a log without it
  check(col(lines[1], "oil_c") === "", "a log from before oil temperature: oil empty");
  const withOil = vbo.replace("CoolantTemp IntakeTemp", "CoolantTemp IntakeTemp OilTemp").split("\r\n");
  const oilLog = withOil.map((l, i) => (/^012 /.test(l) ? l + (i % 2 ? " +121.5" : " +000.0") : l)).join("\r\n");
  const oilRows = vboToCsv(oilLog, null).trim().split("\n").slice(1);
  check(oilRows.length === rows.length && oilRows.some((l) => col(l, "oil_c") === "121.5") && oilRows.some((l) => col(l, "oil_c") === "") &&
        oilRows.every((l) => col(l, "coolant_c") === "93.0"), "oil read, 0 left empty");
  // A car profile's channels (brake pressure...) after the engine's: added at the end, under their own names
  const withCar = vbo.replace("CoolantTemp IntakeTemp", "CoolantTemp IntakeTemp OilTemp BrakePressure SteeringWheelAngle").split("\r\n");
  const carLog = withCar.map((l) => (/^012 /.test(l) ? l + " +121.5 +1.2345E+01 -4.5000E+00" : l)).join("\r\n");
  const carLines = vboToCsv(carLog, null).trim().split("\n");
  const carHead = carLines[0].split(",");
  check(carHead.length === CSV_COLUMNS.length + 2 && carHead[CSV_COLUMNS.length] === "brake_pressure" &&
        carHead[CSV_COLUMNS.length + 1] === "steering_wheel_angle", "car columns at the end: " + carLines[0]);
  const carRow = carLines[1].split(",");
  check(carRow.length === carHead.length && carRow[CSV_COLUMNS.length] === "12.35" && carRow[CSV_COLUMNS.length + 1] === "-4.5" &&
        carRow[CSV_COLUMNS.indexOf("oil_c")] === "121.5", "car values: " + carLines[1]);
  check(lines[0] === CSV_COLUMNS.join(","), "without car channels: the same columns as before");
}

// ---- the viewer (8.1a): the page without a Track Rat, on the website ----
console.log("Wi-Fi page viewer tests");
{
  const vm = html.match(/\/\/ ---- viewer \(begin[^\n]*\n([\s\S]*?)\/\/ ---- viewer \(end\)/);
  check(!!vm, "the page has the viewer section");
  const V = new Function(m[1] + vm[1] + "\nreturn {isViewerMode, localFiles, groupSessions};")();

  // The rule: the board serves plain http; anything else (or ?viewer) is the viewer
  const at = (protocol, search = "") => V.isViewerMode({protocol, search});
  check(at("https:") === true, "the website (https) is the viewer");
  check(at("file:") === true, "a saved copy opened from disk is the viewer");
  check(at("http:") === false, "the board (http) is not");
  check(at("http:", "?viewer") === true && at("http:", "?x=1&viewer") === true && at("http:", "?viewer=1") === true,
        "?viewer asks for it from a local server");
  check(at("http:", "?viewers") === false && at("http:", "?previewer") === false && at("http:", "") === false,
        "only the word viewer");
  check(V.isViewerMode({protocol: "http:"}) === false, "no search string");

  // Pairing opened files into sessions
  const open = (name, size = 100) => ({name, size});
  const picked = [
    open("20260926_101530_calabogie.vbo", 1200000), open("20260926_101530_calabogie.json", 900),
    open("20260926_101530_calabogie_note.json", 80), open("20260927_090000_shannonville.vbo", 500000),
    open("tracks.json", 4000), open("holiday.jpg"), open("notes.txt"), open("20260926_101530_calabogie.json.bak"),
    open("session_bench.json"),
  ];
  const lf = V.localFiles(picked);
  check(lf.files.map((f) => f.name).join() === "20260926_101530_calabogie.vbo,20260926_101530_calabogie.json," +
        "20260926_101530_calabogie_note.json,20260927_090000_shannonville.vbo", "only session files are kept: " + lf.files.map((f) => f.name));
  check(lf.files.every((f) => f.fs === "sd"), "they look like the board's SD card files, as the Compare tab wants");
  check(lf.tracks && lf.tracks.name === "tracks.json", "an opened tracks.json is kept apart");
  check(V.localFiles([open("a.txt")]).tracks === null, "no tracks.json opened: null");
  const dup = V.localFiles([open("20260926_101530_calabogie.vbo", 1), open("20260926_101530_calabogie.vbo", 2)]);
  check(dup.files.length === 1 && dup.files[0].size === 2, "the same file opened twice: the later one");

  const g = V.groupSessions(lf.files, (d) => d.toISOString().slice(0, 10));
  check(g.days.length === 2 && g.other.length === 0, "two days, nothing left over");
  const cal = g.days.find((d) => d[0] === "2026-09-26")[1][0][1][0];
  check(cal.files.map((x) => x.label).join() === "JSON,VBO" && cal.noteFile && cal.noteFile.name === "20260926_101530_calabogie_note.json" &&
        cal.base === "20260926_101530_calabogie", "the .vbo, its summary and its note are one session: " + cal.files.map((x) => x.label));
  const shan = g.days.find((d) => d[0] === "2026-09-27")[1][0][1][0];
  check(shan.files.length === 1 && shan.noteFile === null, "a log alone is a session of its own");
}

// ---- the sample session (site/samples), read with the page's own code ----
console.log("Sample session tests");
{
  const dir = path.join(__dirname, "..", "site", "samples");
  const names = fs.existsSync(dir) ? fs.readdirSync(dir) : [];
  const vboName = names.find((n) => /^\d{8}_\d{6}_calabogie\.vbo$/.test(n));
  check(!!vboName, "site/samples has a Calabogie .vbo named like the board's");
  const index = JSON.parse(fs.readFileSync(path.join(dir, "index.json"), "utf8"));
  check(index.files.every((n) => names.includes(n)) && index.files.includes(vboName), "index.json lists files that are there");
  check(fs.statSync(path.join(dir, vboName)).size < 1.5e6, "the log is under 1.5 MB");
  check(parseFile({name: vboName, fs: "sd"}).track === "calabogie", "the page reads its name");
  const text = readText("site", "samples", vboName);  // line endings normalised: a checkout may or may not have CRLF
  const sum = JSON.parse(fs.readFileSync(path.join(dir, vboName.replace(/\.vbo$/, ".json")), "utf8"));
  const log = CMP.parseVbo(text);
  check(/simulated/i.test(Object.values(log.comments).join(" ")), "the log says it is simulated");
  check(log.comments["Track"] === "calabogie" && sum.track === "calabogie" && sum.log === vboName, "log and summary agree on track and name");
  check(log.points.length > 5000 && log.points[0].lap === 0, "an out-lap, then the laps: " + log.points.length + " fixes");

  // The fixes are 20 Hz, and the laps run 1, 2, 3, then the in-lap (4)
  const gaps = log.points.slice(1).map((p, i) => p.ms - log.points[i].ms);
  check(gaps.every((d) => d === 50 || d === 49 || d === 51), "20 Hz: every fix 50 ms after the last");
  const lapsIn = [...new Set(log.points.map((p) => p.lap))];
  check(lapsIn.join() === "0,1,2,3,4", "laps in the log: " + lapsIn);
  // Lap times as the page cuts them (from where each lap's first fix is), against the summary
  const first = new Map();
  for (const p of log.points) if (!first.has(p.lap)) first.set(p.lap, p.ms);
  check(sum.laps.length === 3 && sum.lapCount === 3, "three timed laps in the summary");
  for (const l of sum.laps) {
    const fromFixes = first.get(l.lap + 1) - first.get(l.lap);
    check(Math.abs(fromFixes - l.timeMs) < 60, `lap ${l.lap}: ${l.timeMs} ms in the summary, ${fromFixes} from the fixes`);
    check(l.time === CMP.lapTimeText(l.timeMs), `lap ${l.lap} time text: ${l.time}`);
  }
  const fromLog = sum.laps.map((l) => ({lap: l.lap, ms: first.get(l.lap + 1) - first.get(l.lap)}));
  const bestFromLog = fromLog.reduce((a, b) => (b.ms < a.ms ? b : a));
  check(bestFromLog.lap === sum.bestLap, `the best lap from the log (${bestFromLog.lap}) is the summary's (${sum.bestLap})`);
  check(sum.laps.find((l) => l.lap === sum.bestLap).timeMs === sum.bestTimeMs, "bestTimeMs is that lap's time");
  const slowest = Math.max(...sum.laps.map((l) => l.timeMs));
  check(slowest - sum.bestTimeMs > 3000 && slowest < sum.bestTimeMs * 1.07, "one lap clearly slower, but not tagged slow");
  check(CMP.season([sum]).length === 1 && CMP.season([sum])[0].best === sum.bestTimeMs, "the season reads its summary");

  // The page's traces: three laps of about the same length, which compare
  const ref = log.points[0];
  const traces = [1, 2, 3].map((n) => CMP.lapTrace(log.points, n, ref.lat, ref.lon));
  for (const [i, t] of traces.entries()) {
    const len = t[t.length - 1].d;
    check(Math.abs(len - sum.laps[i].distanceM) < 60, `lap ${i + 1} is about ${sum.laps[i].distanceM} m long: ${Math.round(len)}`);
  }
  const cmp = CMP.compareLaps(traces[1], traces[2]);
  check(cmp.d.length > 500 && cmp.gap[cmp.gap.length - 1] > 3000, "lap 3 loses time to lap 2 over the lap: " + cmp.gap[cmp.gap.length - 1]);
  // Speeds: out-lap from a stop, no impossible jumps, a believable top speed
  const top = Math.max(...log.points.map((p) => p.kmh));
  check(log.points[0].kmh < 5 && top > 150 && top < 300 && Math.abs(top - sum.topSpeedKmh) < 0.1, "top speed " + top + " vs " + sum.topSpeedKmh);
  const jump = Math.max(...log.points.slice(1).map((p, i) => Math.abs(p.kmh - log.points[i].kmh)));
  check(jump < 5, "no jump over 5 km/h between fixes (1.4 m/s in 50 ms is 3 g): " + jump);
  // The CSV for overlay tools works on it, with the summary's best lap
  const csv = new Function(html.match(/\/\/ ---- csv \(begin[^\n]*\n([\s\S]*?)\/\/ ---- csv \(end\) ----/)[1] + "\nreturn vboToCsv;")()(text, sum).trim().split("\n");
  check(csv.length === log.points.length + 1, "a CSV row per fix");
  // The board's number formats: the data lines have the log's own widths
  const dataLines = text.split("\n").slice(text.split("\n").indexOf("[data]") + 1).filter(Boolean);
  const shape = (l) => l.replace(/\d/g, "9");
  check(dataLines.every((l) => shape(l).length === shape(dataLines[0]).length), "every data line has the same width, as on the board");
  check(shape(dataLines[0]) === "999 999999.999 999999.99999 999999.99999 999.999 999.99 +99999.99 99.99 999 +9.999 +9.999 999 99999 999.9 +999.9 +999.9 +999.9",
        "the board's format: " + shape(dataLines[0]));
  check(text.split("\n").includes("Lap tag") && text.includes(" RPM Throttle CoolantTemp IntakeTemp OilTemp\n"), "the board's channels and column names");
}

// ---- drivers and cars (the page's rules against include/Profiles.h) ----
{
  const dm = html.match(/\/\/ ---- drivers and cars \(begin[^\n]*\n([\s\S]*?)\/\/ ---- drivers and cars \(end\) ----/);
  check(!!dm, "drivers and cars section found in web/index.html");
  if (dm) {
    const DC = new Function(dm[1] + "\nreturn {PROFILE_MAX, PROFILE_NAME_LEN, PROFILE_VIN_LEN, checkProfileName, checkVin, profileBody, whoLabel};")();
    const ph = readText("include", "Profiles.h");
    const num = (n) => +(ph.match(new RegExp("constexpr int " + n + " = (\\d+);")) || [])[1];
    check(DC.PROFILE_MAX === num("MAX_PER_LIST"), "the page's list limit is Profiles.h's MAX_PER_LIST: " + DC.PROFILE_MAX);
    check(DC.PROFILE_NAME_LEN === num("NAME_LEN"), "the page's name length is Profiles.h's NAME_LEN: " + DC.PROFILE_NAME_LEN);
    check(DC.PROFILE_VIN_LEN === num("VIN_LEN"), "the page's VIN length is Profiles.h's VIN_LEN: " + DC.PROFILE_VIN_LEN);

    // Names: trimmed, 1 to 20 bytes, no control characters, unique in the list (any case)
    check(DC.checkProfileName("  Dylan ").value === "Dylan", "a name is trimmed");
    check(!DC.checkProfileName("   ").ok && !DC.checkProfileName("").ok, "an empty name is refused");
    check(DC.checkProfileName("a".repeat(20)).ok && !DC.checkProfileName("a".repeat(21)).ok, "20 bytes is the most");
    check(DC.checkProfileName("é".repeat(10)).ok && !DC.checkProfileName("é".repeat(11)).ok, "the length counts bytes, not letters");
    check(!DC.checkProfileName("a\tb").ok, "a control character is refused");
    check(!DC.checkProfileName("dylan", ["Dylan", "Sam"]).ok && DC.checkProfileName("Alex", ["Dylan"]).ok, "a name must be new to its list");
    check(DC.checkProfileName("Dylan", []).ok, "no others: fine");

    // VINs: empty is fine, else 17 of 0-9 A-Z without I, O, Q, kept in capitals
    check(DC.checkVin("").ok && DC.checkVin("  ").value === "", "no VIN is fine");
    check(DC.checkVin(" 1hgbh41jxmn109186 ").value === "1HGBH41JXMN109186".toUpperCase(), "a VIN is trimmed and capitalised");
    check(!DC.checkVin("1HGBH41JXMN10918").ok && !DC.checkVin("1HGBH41JXMN1091866").ok, "a VIN is 17 long");
    for (const c of "IOQ") check(!DC.checkVin("1HGBH41JXMN10918" + c).ok, c + " is not allowed in a VIN");
    check(!DC.checkVin("1HGBH41JXMN10918-").ok, "only letters and digits");

    // The POST body
    const b = DC.profileBody("rename", "car", {id: 3, name: "Civic"}, "1234");
    check(b.get("action") === "rename" && b.get("kind") === "car" && b.get("id") === "3" && b.get("name") === "Civic" && b.get("code") === "1234" && !b.has("vin"),
          "rename body: " + b);
    const v = DC.profileBody("setVin", "car", {id: 2, vin: ""}, 4321);
    check(v.has("vin") && v.get("vin") === "" && v.get("code") === "4321", "setVin with no VIN sends it empty");
    const a = DC.profileBody("add", "driver", {name: "A&B"}, "1");
    check(a.get("name") === "A&B" && !a.has("id") && a.toString().includes("A%26B"), "add body is encoded: " + a);

    // The label
    check(DC.whoLabel({driver: "Dylan", car: "Civic"}) === "Dylan · Civic", "driver and car");
    check(DC.whoLabel({driver: "Dylan"}) === "Dylan" && DC.whoLabel({car: "Civic"}) === "Civic", "only one named");
    check(DC.whoLabel({}) === "" && DC.whoLabel(null) === "" && DC.whoLabel({driver: 5, car: ""}) === "", "neither named: empty");
  }
}

console.log(failures ? `\n${failures} FAILURE(S)` : "\nALL PASSED");
process.exit(failures ? 1 : 0);
