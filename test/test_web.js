// Host-side tests for the Wi-Fi page's session list (the "session names"
// part of web/index.html: parsing file names and grouping by day and track).
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

console.log(failures ? `\n${failures} FAILURE(S)` : "\nALL PASSED");
process.exit(failures ? 1 : 0);
