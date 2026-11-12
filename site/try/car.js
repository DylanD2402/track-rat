// A simulated car that drives round Calabogie Motorsports Park, for the "Try it" page.
// No DOM use, so Node can test it (test/test_try_car.mjs).
//
// Positions are worked out on a flat plane around the start/finish line:
// 111320 m per degree north, and 111320 * cos(latitude) m per degree east.

const M_PER_DEG = 111320;

// A small seeded generator (mulberry32), so every run drives the same.
function seededRandom(seed) {
  let a = seed >>> 0;
  a = (a + 0x6D2B79F5) >>> 0;
  let t = a;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}

function polylineLength(points) {
  let sum = 0;
  for (let i = 1; i < points.length; i++) {
    const dy = (points[i][0] - points[i - 1][0]) * M_PER_DEG;
    const dx = (points[i][1] - points[i - 1][1]) * M_PER_DEG * Math.cos(points[i][0] * Math.PI / 180);
    sum += Math.hypot(dx, dy);
  }
  return sum;
}

// tracks: the parsed tracks.json ({tracks: [...]}). seed: changes the pace of each lap.
export function createCar(tracks, seed = 1) {
  const track = tracks.tracks.find((t) => t.id === "calabogie");
  if (!track) throw new Error("calabogie is not in the track list");
  const lat0 = track.startFinish.lat;
  const lon0 = track.startFinish.lon;
  const kEast = M_PER_DEG * Math.cos(lat0 * Math.PI / 180);

  // The circuit is the longest line; the others are pit lanes.
  let line = track.map[0];
  for (const l of track.map) if (polylineLength(l) > polylineLength(line)) line = l;

  let pts = line.map(([lat, lon]) => [(lon - lon0) * kEast, (lat - lat0) * M_PER_DEG]);
  const first = pts[0], last = pts[pts.length - 1];
  if (Math.hypot(first[0] - last[0], first[1] - last[1]) < 0.01) pts = pts.slice(0, -1);  // drop the repeat
  const n = pts.length;

  // The point on the loop nearest the start/finish line (the origin).
  let best = { d: Infinity, i: 0, p: pts[0] };
  for (let i = 0; i < n; i++) {
    const a = pts[i], b = pts[(i + 1) % n];
    const ex = b[0] - a[0], ey = b[1] - a[1];
    const len2 = ex * ex + ey * ey;
    const t = len2 > 0 ? Math.min(1, Math.max(0, -(a[0] * ex + a[1] * ey) / len2)) : 0;
    const p = [a[0] + t * ex, a[1] + t * ey];
    const d = Math.hypot(p[0], p[1]);
    if (d < best.d) best = { d, i, p };
  }

  // The loop, starting and ending at that point, with the distance along it.
  const ring = [best.p];
  for (let k = 1; k <= n; k++) ring.push(pts[(best.i + k) % n]);
  ring.push(best.p);
  const loop = [ring[0]];
  for (let i = 1; i < ring.length; i++) {
    const prev = loop[loop.length - 1];
    if (Math.hypot(ring[i][0] - prev[0], ring[i][1] - prev[1]) > 1e-6) loop.push(ring[i]);
  }
  const cum = [0];
  for (let i = 1; i < loop.length; i++) {
    cum.push(cum[i - 1] + Math.hypot(loop[i][0] - loop[i - 1][0], loop[i][1] - loop[i - 1][1]));
  }
  const length = cum[cum.length - 1];

  // x, y (metres) at distance s along the loop; s may be any real number.
  function xyAt(s) {
    const u = ((s % length) + length) % length;
    let lo = 0, hi = cum.length - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (cum[mid] <= u) lo = mid; else hi = mid;
    }
    const f = (u - cum[lo]) / (cum[hi] - cum[lo]);
    return [loop[lo][0] + f * (loop[hi][0] - loop[lo][0]), loop[lo][1] + f * (loop[hi][1] - loop[lo][1])];
  }

  function latLonAt(s) {
    const [x, y] = xyAt(s);
    return { lat: lat0 + y / M_PER_DEG, lon: lon0 + x / kEast };
  }

  // Direction of travel at s, degrees (0 north, 90 east): the chord from 2 m behind to 2 m ahead.
  function headingAt(s) {
    const a = xyAt(s - 2), b = xyAt(s + 2);
    const deg = Math.atan2(b[0] - a[0], b[1] - a[1]) * 180 / Math.PI;
    return (deg + 360) % 360;
  }

  // Each lap has its own pace, 0.985 to 1.02.
  function paceFor(lap) {
    return 0.985 + 0.035 * seededRandom(seed * 7919 + lap * 104729);
  }

  // The speed the car aims for at s, in m/s.
  function targetMps(s) {
    const lap = Math.floor(s / length);
    const u = s - lap * length;
    const kmh = 118 + 52 * Math.sin(2 * Math.PI * 3 * u / length) + 18 * Math.sin(2 * Math.PI * 8 * u / length);
    return kmh * paceFor(lap) / 3.6;
  }

  let s = -40;          // distance along the loop; a lap is `length`
  let v = 0;            // m/s
  let driving = false;

  return {
    length,
    startFinish: { lat: lat0, lon: lon0 },
    latLonAt,
    headingAt,
    drive(on) { driving = !!on; },
    get driving() { return driving; },
    get s() { return s; },
    get speedKmh() { return v * 3.6; },

    // Moves the car on by dtMs and returns the GPS fix for where it is now.
    step(dtMs) {
      const dt = dtMs / 1000;
      const v0 = v;
      if (driving) {
        const target = targetMps(s);
        v = v < target ? Math.min(target, v + 4 * dt) : Math.max(target, v - 8 * dt);
      } else {
        v = Math.max(0, v - 6 * dt);
      }
      s += (v0 + v) / 2 * dt;
      const { lat, lon } = latLonAt(s);
      return { lat, lon, speedKmh: v * 3.6, headingDeg: headingAt(s), accuracyM: 1.4, sats: 14 };
    },
  };
}
