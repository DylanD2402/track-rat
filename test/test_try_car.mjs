// Test of site/try/car.js. Run: node test/test_try_car.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createCar } from "../site/try/car.js";

const tracks = JSON.parse(readFileSync(new URL("../data/tracks.json", import.meta.url), "utf8"));

const M = 111320;
function metres(a, b) {
  const dy = (b.lat - a.lat) * M;
  const dx = (b.lon - a.lon) * M * Math.cos(a.lat * Math.PI / 180);
  return Math.hypot(dx, dy);
}
function bearing(a, b) {
  const dy = b.lat - a.lat;
  const dx = (b.lon - a.lon) * Math.cos(a.lat * Math.PI / 180);
  return (Math.atan2(dx, dy) * 180 / Math.PI + 360) % 360;
}
function angleDiff(a, b) {
  const d = Math.abs(a - b) % 360;
  return d > 180 ? 360 - d : d;
}

// Drives for a few laps and returns everything that can be compared between runs.
function run(seed) {
  const car = createCar(tracks, seed);
  const log = [];
  car.drive(true);
  for (let i = 0; i < 20 * 400; i++) log.push(car.step(50));
  return log;
}

const sf = tracks.tracks.find((t) => t.id === "calabogie").startFinish;

// The loop is about as long as the track says.
{
  const car = createCar(tracks);
  assert.ok(Math.abs(car.length - 5050) / 5050 < 0.03, `loop length ${car.length}`);
  assert.ok(metres(sf, car.latLonAt(0)) < 20, "s = 0 is near the start/finish");
  assert.ok(metres(car.latLonAt(0), car.latLonAt(car.length)) < 0.01, "the loop closes");
  console.log(`loop length ${car.length.toFixed(0)} m`);
}

// Parked 40 m before the line.
{
  const car = createCar(tracks);
  const fix = car.step(50);
  const d = metres(sf, fix);
  assert.ok(d > 32 && d < 48, `parked ${d} m from the line`);
  assert.equal(fix.speedKmh, 0);
  assert.equal(fix.accuracyM, 1.4);
  assert.equal(fix.sats, 14);
  assert.ok(!car.driving);
}

// Accelerates gently, and a lap takes a sensible time.
{
  const car = createCar(tracks);
  car.drive(true);
  let t = 0, prev = 0, reached = null, maxAcc = 0;
  let crossStart = null, lapTime = null;
  let worstHeading = 0;
  let lastFix = car.step(0);
  while (t < 400000 && lapTime === null) {
    const sBefore = car.s;
    const fix = car.step(50);
    t += 50;
    const acc = (fix.speedKmh - prev) / 3.6 / 0.05;
    maxAcc = Math.max(maxAcc, acc);
    prev = fix.speedKmh;
    if (reached === null && fix.speedKmh > 80) reached = t;
    if (metres(lastFix, fix) > 0.5) {
      worstHeading = Math.max(worstHeading, angleDiff(fix.headingDeg, bearing(lastFix, fix)));
    }
    lastFix = fix;
    if (sBefore < 0 && car.s >= 0) crossStart = t;
    if (sBefore < car.length && car.s >= car.length) lapTime = t - crossStart;
  }
  assert.ok(reached !== null && reached < 12000, `80 km/h reached at ${reached} ms`);
  assert.ok(maxAcc <= 4.5, `max acceleration ${maxAcc} m/s2`);
  assert.ok(lapTime >= 150000 && lapTime <= 190000, `lap time ${lapTime / 1000} s`);
  assert.ok(worstHeading < 25, `worst heading error ${worstHeading} degrees`);
  const m = Math.floor(lapTime / 60000), sec = (lapTime % 60000) / 1000;
  console.log(`80 km/h after ${reached / 1000} s, max acceleration ${maxAcc.toFixed(2)} m/s2`);
  console.log(`lap ${m}:${sec.toFixed(1).padStart(4, "0")}, worst heading error ${worstHeading.toFixed(1)} degrees`);
}

// Braking: stops within 15 s and stays stopped.
{
  const car = createCar(tracks);
  car.drive(true);
  for (let i = 0; i < 20 * 60; i++) car.step(50);
  assert.ok(car.speedKmh > 50);
  car.drive(false);
  let t = 0;
  while (car.speedKmh > 0 && t < 30000) { car.step(50); t += 50; }
  assert.ok(t <= 15000, `stopped after ${t} ms`);
  const at = car.s;
  for (let i = 0; i < 100; i++) assert.equal(car.step(50).speedKmh, 0);
  assert.equal(car.s, at);
  console.log(`stopped from speed in ${t / 1000} s`);
}

// Repeatable.
assert.deepEqual(run(1), run(1));
assert.notDeepEqual(run(1), run(2));

console.log("test_try_car: ok");
