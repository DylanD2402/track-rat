// Runs the firmware's WebAssembly build (tools/build_sim.py) through a drive, in Node:
//
//     node tools/sim/smoke.mjs [path/to/trackrat.wasm]      (default site/try/trackrat.wasm)
//
// A new unit asks its Setup questions, finds Calabogie by itself from a parked fix, is driven round the
// circuit for two and a half laps, stops, is saved and started again (its PB kept), is switched off with BOOT,
// and runs the built-in Bench Test. It exits 0 only if everything held. Pictures of the screen are written to
// build/sim/ as PPM files at each step, for looking at (convert them with Pillow to open them).
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createSim, W, H } from "../../site/try/host.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const wasmPath = path.resolve(process.argv[2] || path.join(ROOT, "site", "try", "trackrat.wasm"));
const outDir = path.join(ROOT, "build", "sim");
fs.mkdirSync(outDir, { recursive: true });
for (const f of fs.readdirSync(outDir)) if (f.endsWith(".ppm")) fs.rmSync(path.join(outDir, f));

const wasm = fs.readFileSync(wasmPath);
const tracksJson = fs.readFileSync(path.join(ROOT, "data", "tracks.json"));
const tracks = JSON.parse(tracksJson.toString("utf8")).tracks;

let failures = 0;
function check(ok, what) {
  console.log(`${ok ? "ok  " : "FAIL"} ${what}`);
  if (!ok) failures++;
}

// --- the circuit: Calabogie's longest map line, walked from the start/finish line --------------------------

const calabogie = tracks.find((t) => t.id === "calabogie");
const SF = calabogie.startFinish;
const KLAT = 111320, KLON = 111320 * Math.cos((SF.lat * Math.PI) / 180);
const local = (lat, lon) => [(lon - SF.lon) * KLON, (lat - SF.lat) * KLAT];

const lap = (() => {
  const line = calabogie.map.reduce((a, b) => (b.length > a.length ? b : a));
  let best = Infinity, seg = 0, bestU = 0;
  for (let i = 0; i + 1 < line.length; i++) {
    const [ax, ay] = local(...line[i]), [bx, by] = local(...line[i + 1]);
    const dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    const u = len2 > 0 ? Math.min(1, Math.max(0, -(ax * dx + ay * dy) / len2)) : 0;
    const d = Math.hypot(ax + u * dx, ay + u * dy);
    if (d < best) (best = d), (seg = i), (bestU = u);
  }
  const pts = [], dist = [];
  let length = 0;
  const add = (lat, lon) => {
    if (pts.length) {
      const [x0, y0] = local(...pts[pts.length - 1]), [x1, y1] = local(lat, lon);
      length += Math.hypot(x1 - x0, y1 - y0);
    }
    pts.push([lat, lon]);
    dist.push(length);
  };
  const n = line.length;
  add(line[seg][0] + bestU * (line[seg + 1][0] - line[seg][0]), line[seg][1] + bestU * (line[seg + 1][1] - line[seg][1]));
  for (let k = 1; k < n; k++) add(...line[(seg + k) % (n - 1)]);
  add(...pts[0]);
  return {
    length,
    // position and heading at s metres from the line
    at(s) {
      s = ((s % length) + length) % length;
      let i = dist.findIndex((d) => d > s);
      if (i <= 0) i = 1;
      const f = (s - dist[i - 1]) / Math.max(1e-9, dist[i] - dist[i - 1]);
      const [a, b] = [pts[i - 1], pts[i]];
      const dy = (b[0] - a[0]) * KLAT, dx = (b[1] - a[1]) * KLON;
      return { lat: a[0] + f * (b[0] - a[0]), lon: a[1] + f * (b[1] - a[1]), heading: ((Math.atan2(dx, dy) * 180) / Math.PI + 360) % 360 };
    },
  };
})();

// --- a unit, and a way to run it in virtual time ---------------------------------------------------------

const DAY = Date.UTC(2026, 6, 4, 14, 0, 0); // the fixes' date and time at the first one
let shotNo = 0;

class Rig {
  constructor(sim, startMs, name) {
    this.sim = sim;
    this.t = startMs;
    this.t0 = startMs;
    this.name = name;
    this.logs = [];
  }
  // run for ms, a tick every 50 ms (20 a second); each() is called before each tick with the elapsed ms
  run(ms, each) {
    const end = this.t + ms;
    while (this.t < end) {
      this.t += 50;
      if (each) each(this.t - this.t0);
      this.sim.tick(this.t);
    }
  }
  fix(lat, lon, speedKmh, heading, ms = this.t - this.t0) {
    this.sim.gpsFix({ lat, lon, speedKmh, headingDeg: heading, accuracyM: 1.0, utcMs: DAY + ms, sats: 14 });
  }
  tap(x, y, holdMs = 200) {
    this.sim.touch(x, y, true);
    this.run(holdMs);
    this.sim.touch(x, y, false);
    this.run(150);
  }
  state() {
    return this.sim.debug().state;
  }
  shot(label) {
    const rgba = new Uint8ClampedArray(W * H * 4);
    const backlight = this.sim.frame(rgba);
    const body = Buffer.alloc(W * H * 3);
    for (let i = 0; i < W * H; i++) {
      body[i * 3] = rgba[i * 4];
      body[i * 3 + 1] = rgba[i * 4 + 1];
      body[i * 3 + 2] = rgba[i * 4 + 2];
    }
    const file = `${String(++shotNo).padStart(2, "0")}_${label}.ppm`;
    fs.writeFileSync(path.join(outDir, file), Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), body]));
    return { backlight, file, rgba };
  }
}

async function powerOn(state, name) {
  const startMs = 5000;
  const logs = [];
  const sim = await createSim({ wasm, tracksJson, state, startMs, utcMs: DAY, log: (l) => logs.push(l) });
  const rig = new Rig(sim, startMs, name);
  rig.logs = logs;
  return rig;
}

// --- the unit's saved data, read from an exported blob (tools/sim/vfs.cpp) ---------------------------------

function savedFiles(blob) {
  const dv = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
  let at = 4;
  const u32 = () => ((at += 4), dv.getUint32(at - 4, true));
  const bytes = () => {
    const n = u32();
    return blob.subarray(at, (at += n));
  };
  const entries = u32();
  for (let i = 0; i < entries; i++) bytes(), bytes(), bytes();
  const files = new Map();
  const n = u32();
  for (let i = 0; i < n; i++) {
    const name = Buffer.from(bytes()).toString("utf8");
    const dir = blob[at++];
    at += 8;
    const data = bytes();
    if (!dir) files.set(name, data);
  }
  return files;
}

// =========================================================================================================

// 1. A new unit boots to the Setup questions; answer them with taps
let u = await powerOn(null, "new");
check(!u.sim.halted, "a new unit boots");
u.run(2000);
check(u.state() === "SETUP", `a new unit asks the Setup questions (${u.state()})`);
u.shot("setup_which_way_up");
u.tap(50, 124); // the arrow that points up is on the left
u.run(300);
u.shot("setup_units");
u.tap(50, 124); // km/h
u.run(500);
check(u.state() === "GPS_WAIT", `Setup done: waiting for GPS (${u.state()})`);
u.shot("gps_wait");

// 2. Parked about 40 m before the start/finish line: it finds the track by itself
{
  const p = lap.at(lap.length - 40);
  u.run(500, () => u.fix(p.lat, p.lon, 0, p.heading));
  u.run(6000, () => u.fix(p.lat, p.lon, 0, p.heading));
  check(u.state() === "TRACK_CONFIRM", `parked near Calabogie: the Ready screen (${u.state()})`);
  u.shot("ready_calabogie");
}

// 3. Drive off and round: two and a half laps at 20 fixes a second, a speed that ramps up from 0 and then
// follows the lap (quick on the straights, slower in the corners), about 100 to 130 km/h along the line
const PACE = 0.97;
class Car {
  constructor(rig, s) {
    Object.assign(this, { rig, s, v: 0, driven: 0 });
  }
  // one 50 ms step: the car moves, its fix goes to the unit, the unit runs
  step(stopping = false) {
    const dt = 0.05;
    const at = this.s % lap.length; // (the same speed at the same place every lap)
    const target = ((118 + 52 * Math.sin(at / 310) + 18 * Math.sin(at / 97)) * PACE) / 3.6;
    this.v = Math.max(0, this.v + Math.max(-9 * dt, Math.min(4 * dt, (stopping ? 0 : target) - this.v)));
    this.s += this.v * dt;
    this.driven += this.v * dt;
    const p = lap.at(this.s);
    this.rig.t += 50;
    this.rig.fix(p.lat, p.lon, this.v * 3.6, p.heading);
    this.rig.sim.tick(this.rig.t);
  }
  steps(ms, stopping) {
    for (let i = 0; i < ms / 50; i++) this.step(stopping);
  }
}

const differs = (a, b) => {
  let n = 0;
  for (let i = 0; i < a.length; i += 4) if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) n++;
  return n / (a.length / 4);
};

const lapTimes = [];
const car = new Car(u, lap.length - 40);
{
  const total = 40 + 2.5 * lap.length;
  let seen = 0, tookTiming = false, mapDone = false;
  let timing = null;
  while (car.driven < total) {
    car.step();
    const d = u.sim.debug();
    if (d.laps !== seen) {
      seen = d.laps;
      lapTimes.push(d.lastLapMs);
      if (lapTimes.length === 1) {
        car.steps(400);
        u.shot("lap_flash");
      }
    }
    if (!tookTiming && d.state === "RUNNING" && lapTimes.length === 0 && car.driven > 1200) {
      tookTiming = true;
      timing = u.shot("timing_screen");
    }
    if (!mapDone && lapTimes.length === 1 && car.driven > lap.length + 1500) {
      mapDone = true;
      // 4. tap the timing screen: the map. Tap again: back
      u.sim.touch(160, 120, true);
      car.steps(250);
      u.sim.touch(160, 120, false);
      car.steps(600);
      const map = u.shot("map");
      check(differs(map.rgba, timing.rgba) > 0.05, "a tap on the timing screen shows the map");
      u.sim.touch(160, 120, true);
      car.steps(250);
      u.sim.touch(160, 120, false);
      car.steps(600);
      const back = u.shot("timing_again");
      check(differs(back.rgba, map.rgba) > 0.05, "a second tap goes back to the timing screen");
    }
  }
  const d = u.sim.debug();
  check(d.state === "RUNNING" && d.laps >= 2, `two and a half laps driven: ${d.state}, ${d.laps} laps`);
  check(lapTimes.length >= 2 && lapTimes.every((ms) => ms >= 150000 && ms <= 190000),
        `lap times are plausible: ${lapTimes.map((ms) => (ms / 1000).toFixed(1) + " s").join(", ")}`);
  u.shot("third_lap");
}

// 5. Stop the car: parked for the time the firmware allows (PIT_PARK_MS, a minute), the debrief or summary shows
{
  car.steps(8000, true); // braking to a stop
  check(car.v === 0, "the car has stopped");
  car.steps(30000, true);
  u.shot("parked_30s");
  car.steps(40000, true);
  const st = u.state();
  check(st === "SUMMARY" || st === "RUNNING", `parked over a minute: ${st}`);
  u.shot("parked_70s_debrief");
}

// 6. Everything saved goes into a new unit: no Setup, and the PB is the best lap just driven
const best = Math.min(...lapTimes);
let saved;
{
  car.steps(1500, true);
  // the session ends with the summary: hold the screen to end it, as a driver would
  u.sim.touch(160, 120, true);
  u.run(2300);
  u.sim.touch(160, 120, false);
  u.run(500);
  u.shot("session_ended");
  saved = u.sim.exportState();
  const files = savedFiles(saved);
  const pb = files.get("/tracks/calabogie/best.bin");
  check(!!pb && pb.length > 32, "the PB is in what was saved");
  const pbMs = pb ? Buffer.from(pb).readUInt32LE(4) : 0;
  check(pbMs === best, `the saved PB (${pbMs} ms) is the best lap driven (${best} ms)`);
  check([...files.keys()].some((k) => k.startsWith("/sessions/") && k.endsWith(".vbo")), "the session's log is in what was saved");
  console.log(`     saved: ${(saved.length / 1024).toFixed(0)} KB, ${files.size} files`);
}
{
  const u2 = await powerOn(saved, "again");
  u2.run(3000);
  check(u2.state() !== "SETUP", `the unit started from saved data skips Setup (${u2.state()})`);
  const p = lap.at(lap.length - 40);
  u2.run(6000, () => u2.fix(p.lat, p.lon, 0, p.heading));
  check(u2.state() === "TRACK_CONFIRM", `and finds Calabogie again (${u2.state()})`);
  u2.shot("ready_with_pb");

  // 7. BOOT held until the unit switches off
  u2.sim.button(true);
  u2.run(1000);
  check(!u2.sim.halted, "BOOT held 1 s: still on");
  u2.run(1500);
  u2.shot("boot_held_power_bar");
  u2.run(900);
  check(u2.sim.halted === "off", `BOOT held 3 s: the unit switches off (${u2.sim.halted})`);
  check(u2.sim.tick(u2.t + 50) === false, "a unit that is off does not tick");
  u2.shot("switched_off");
}

// A unit set up upside down (the arrow that points down): the picture stays upright and a touch lands where it is seen
{
  const u4 = await powerOn(null, "upside down");
  u4.run(2000);
  u4.tap(270, 124); // the arrow on the right points down
  u4.run(300);
  const units = u4.shot("upside_down_units");
  u4.tap(270, 124); // mph, on the right
  u4.run(500);
  check(u4.state() === "GPS_WAIT", `Setup answered upside down: touches still land where they are seen (${u4.state()})`);
  u4.tap(160, 120);
  check(u4.state() === "MENU", `the home screen, upside down (${u4.state()})`);
  u4.tap(40, 140); // Track day, the left of the four tiles
  check(u4.state() === "TRACK_SELECT", `Track day, upside down (${u4.state()})`);
  u4.shot("upside_down_track_day");
  u4.tap(40, 14); // the top bar: back to the home screen
  check(u4.state() === "MENU", `the top bar, upside down (${u4.state()})`);
}

// 8. The built-in Bench Test: from the home screen, Track day, the Bench Test, start: laps count up by themselves
{
  const u3 = await powerOn(saved, "bench");
  u3.run(3000);
  u3.tap(160, 120); // GPS wait: on to the home screen
  check(u3.state() === "MENU", `the home screen (${u3.state()})`);
  u3.shot("home");
  u3.tap(80, 204); // Wi-Fi: on, with a network name (nothing is served)
  check(u3.state() === "WIFI", `the Wi-Fi screen (${u3.state()})`);
  u3.run(500);
  u3.shot("wifi");
  u3.tap(160, 120); // any tap goes back
  check(u3.state() === "MENU", `back from Wi-Fi (${u3.state()})`);
  u3.tap(40, 140); // Track day
  check(u3.state() === "TRACK_SELECT", `Track day: the list (${u3.state()})`);
  u3.tap(40, 120); // left from the first: round to the end, where the Bench Test is
  u3.shot("bench_in_list");
  u3.tap(160, 120); // pick it
  check(u3.state() === "TRACK_CONFIRM", `the Bench Test's Ready screen (${u3.state()})`);
  u3.shot("bench_ready");
  u3.tap(160, 120); // start
  check(u3.state() === "RUNNING", `the Bench Test runs (${u3.state()})`);
  u3.run(5000);
  u3.shot("bench_running");
  check(u3.logs.some((l) => l.includes("BENCH TEST")), "the simulated car is on");
  u3.run(60000);
  const d = u3.sim.debug();
  check(d.state === "RUNNING" && d.laps >= 3, `laps count up by themselves: ${d.laps} laps, last ${(d.lastLapMs / 1000).toFixed(1)} s`);
  u3.shot("bench_laps");
}

console.log(failures ? `\n${failures} check(s) FAILED` : "\nall checks passed");
process.exit(failures ? 1 : 0);

