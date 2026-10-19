// Host-side tests for the Wi-Fi page's session list (the "session names"
// part of web/index.html: parsing file names and grouping by day and track)
// and for its firmware file check (the "firmware check" part).
//
//   node test/test_web.js

"use strict";
const fs = require("fs");
const path = require("path");

const html = fs.readFileSync(path.join(__dirname, "..", "web", "index.html"), "utf8");
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

console.log(failures ? `\n${failures} FAILURE(S)` : "\nALL PASSED");
process.exit(failures ? 1 : 0);
