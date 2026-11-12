// The "Try it" page: runs the real firmware (trackrat.wasm, through host.js) and feeds it
// a screen, touch, the BOOT button and GPS (a simulated car, the visitor's location, or none).
import { createCar } from "./car.js";

const FIX_MS = 50;          // one simulated GPS fix every 50 ms of virtual time
const MAX_STEPS = 40;       // most GPS steps in one animation frame
const MAX_FRAME_MS = 100;   // most real time one frame may add to the clock
const START_MS = 1000;      // the virtual clock at power-on
const SAVE_EVERY_MS = 3000;
const MAX_STATE_BYTES = 24 * 1024 * 1024;
const LOC_TIMEOUT_MS = 5000;

const $ = (id) => document.getElementById(id);
const canvas = $("screen");
const ctx = canvas.getContext("2d");
const statusEl = $("status");
const offNote = $("off-note");
const locStatusEl = $("loc-status");
const driveBtn = $("drive");
const bootBtn = $("boot");

// Set once the pieces are loaded.
let W = 320, H = 240;
let createSim = null;
let wasmBytes = null;
let tracksBytes = null;
let car = null;

// Running state.
let sim = null;
let starting = false;       // a new sim is being made
let fatal = null;           // a message when the page cannot run at all
let haltHandled = false;    // the current sim's halt has been dealt with
let vNow = START_MS;        // the virtual clock, ms
let utc0 = Date.now();      // the real date and time at power-on
let nextFixAt = START_MS;
let lastFrameReal = null;
let rgba = null;
let image = null;

// Controls.
let source = "car";         // "car" | "loc" | "off"
let speed = 1;              // 1, 4 or 10

// Saved data.
let noSave = false;
let savedSeq = null;
let lastSaveReal = 0;
let latestState = null;     // the newest state we have, as bytes

// ---------------------------------------------------------------- saved data (IndexedDB)

function openDb() {
  return new Promise((resolve, reject) => {
    if (!window.indexedDB) { reject(new Error("no IndexedDB")); return; }
    const req = indexedDB.open("trackrat-try", 1);
    req.onupgradeneeded = () => req.result.createObjectStore("kv");
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
    req.onblocked = () => reject(new Error("IndexedDB blocked"));
  });
}

// Runs one request on the store; resolves with its result.
async function dbRun(mode, makeRequest) {
  const db = await openDb();
  try {
    return await new Promise((resolve, reject) => {
      const tx = db.transaction("kv", mode);
      const req = makeRequest(tx.objectStore("kv"));
      tx.oncomplete = () => resolve(req.result);
      tx.onerror = tx.onabort = () => reject(tx.error);
    });
  } finally {
    db.close();
  }
}

async function loadState() {
  try {
    const v = await dbRun("readonly", (store) => store.get("state"));
    return v ? new Uint8Array(v) : null;
  } catch (e) {
    return null;
  }
}

async function saveState(bytes) {
  try { await dbRun("readwrite", (store) => store.put(bytes, "state")); } catch (e) { /* carry on without saving */ }
}

async function deleteState() {
  try { await dbRun("readwrite", (store) => store.delete("state")); } catch (e) { /* nothing to delete */ }
}

// Saves the unit's data if it changed. `force` skips the 3 s limit.
function maybeSave(force) {
  if (noSave || !sim) return;
  const now = performance.now();
  if (!force && now - lastSaveReal < SAVE_EVERY_MS) return;
  let seq;
  try { seq = sim.stateSeq(); } catch (e) { return; }
  if (seq === savedSeq) return;
  lastSaveReal = now;
  savedSeq = seq;
  try {
    const bytes = sim.exportState();
    latestState = bytes;
    if (bytes.length <= MAX_STATE_BYTES) saveState(bytes);
  } catch (e) { /* carry on */ }
}

// ---------------------------------------------------------------- the firmware

// Makes a new unit from the newest saved data.
async function powerOn() {
  starting = true;
  try {
    sim = await createSim({
      wasm: wasmBytes,
      tracksJson: tracksBytes,
      state: latestState,
      startMs: START_MS,
      utcMs: Date.now(),
      log: (line) => console.debug(line),
    });
  } catch (e) {
    console.error(e);
    fatal = "The simulator could not load.";
    sim = null;
    starting = false;
    renderStatus();
    return;
  }
  vNow = START_MS;
  utc0 = Date.now();
  nextFixAt = vNow;
  lastFrameReal = null;
  haltHandled = false;
  savedSeq = sim.stateSeq();
  locGpsOffSent = false;
  locLastPosReal = performance.now();
  offNote.hidden = true;
  if (source === "off") sim.gpsOff();
  starting = false;
  renderStatus();
}

// The firmware has stopped (switched off, asleep, restarting or crashed).
function handleHalt() {
  haltHandled = true;
  const why = sim.halted;
  if (why === "error") {
    console.error("The firmware stopped:", sim.error);
    fatal = "The firmware stopped. Reload the page to start again.";
    renderStatus();
    return;
  }
  // Keep what the unit saved, so the next power-on starts from it.
  try { latestState = sim.exportState(); } catch (e) { /* use the last one */ }
  maybeSave(true);
  if (why === "restart") powerOn();
  else offNote.hidden = false;     // "off"
}

// A tap, click or BOOT press while the unit is off switches it on. Returns true if it did.
function wakeIfOff() {
  if (sim && sim.halted === "off" && !starting && !fatal) {
    offNote.hidden = true;
    powerOn();
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- the clock and the frame loop

const utcAt = (virtualMs) => utc0 + (virtualMs - START_MS);

// Moves the virtual clock on by dtMs and runs the firmware up to it.
function advance(dtMs) {
  vNow += dtMs;
  if (source === "car") {
    let steps = 0;
    while (nextFixAt <= vNow && steps < MAX_STEPS) {
      const t = nextFixAt;
      nextFixAt += FIX_MS;
      steps++;
      sim.gpsFix({ ...car.step(FIX_MS), utcMs: utcAt(t) });
      if (!sim.tick(t)) return;
    }
    if (nextFixAt <= vNow) nextFixAt = vNow;    // too far behind: drop the excess time
  } else {
    nextFixAt = vNow;
    if (source === "loc" && !locGpsOffSent && performance.now() - locLastPosReal > LOC_TIMEOUT_MS) {
      sim.gpsOff();
      locGpsOffSent = true;
      locRecent.length = 0;
      setLocStatus("Waiting for your location…");
    }
  }
  sim.tick(vNow);
}

function frame(nowReal) {
  requestAnimationFrame(frame);
  if (!sim || starting || fatal) { lastFrameReal = nowReal; return; }
  if (document.hidden) { lastFrameReal = null; return; }

  if (!sim.halted) {
    const dt = lastFrameReal === null ? 0 : Math.min(MAX_FRAME_MS, nowReal - lastFrameReal);
    advance(dt * (source === "car" ? speed : 1));
  }
  lastFrameReal = nowReal;

  if (sim.halted && !haltHandled) {
    handleHalt();
    if (fatal) return;
  }
  if (starting) return;     // restarting: the next frame draws the new unit
  if (sim.halted === "off" || !sim.halted) draw();
  maybeSave(false);
  renderStatus();
}

function draw() {
  const backlight = sim.frame(rgba);
  ctx.putImageData(image, 0, 0);
  canvas.style.opacity = String(Math.max(0.08, Math.min(1, backlight)));
}

// ---------------------------------------------------------------- status line

let lastStatus = null;
function renderStatus() {
  let text;
  if (fatal) text = fatal;
  else if (!sim) text = "Loading the firmware…";
  else if (source === "car") {
    text = `${Math.round(car.speedKmh)} km/h · ${car.driving ? "driving" : "parked"}`;
  } else text = "";
  if (text !== lastStatus) {
    lastStatus = text;
    statusEl.textContent = text;
  }
}

// ---------------------------------------------------------------- touch screen

let screenPointer = null;   // the id of the one pointer that counts

function screenXY(e) {
  const r = canvas.getBoundingClientRect();
  const x = Math.floor((e.clientX - r.left) / r.width * W);
  const y = Math.floor((e.clientY - r.top) / r.height * H);
  return [Math.min(W - 1, Math.max(0, x)), Math.min(H - 1, Math.max(0, y))];
}

function canSend() { return sim && !starting && !sim.halted; }

canvas.addEventListener("pointerdown", (e) => {
  if (screenPointer !== null) return;
  e.preventDefault();
  if (wakeIfOff()) return;
  screenPointer = e.pointerId;
  try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* not capturable */ }
  if (canSend()) { const [x, y] = screenXY(e); sim.touch(x, y, true); }
});
canvas.addEventListener("pointermove", (e) => {
  if (e.pointerId !== screenPointer) return;
  e.preventDefault();
  if (canSend()) { const [x, y] = screenXY(e); sim.touch(x, y, true); }
});
function endTouch(e) {
  if (e.pointerId !== screenPointer) return;
  screenPointer = null;
  if (canSend()) { const [x, y] = screenXY(e); sim.touch(x, y, false); }
}
canvas.addEventListener("pointerup", endTouch);
canvas.addEventListener("pointercancel", endTouch);
canvas.addEventListener("contextmenu", (e) => e.preventDefault());

// ---------------------------------------------------------------- BOOT button

let bootHeld = false;

function bootDown() {
  if (bootHeld) return;
  bootHeld = true;
  bootBtn.classList.add("down");
  if (wakeIfOff()) return;
  if (canSend()) sim.button(true);
}
function bootUp() {
  if (!bootHeld) return;
  bootHeld = false;
  bootBtn.classList.remove("down");
  if (canSend()) sim.button(false);
}

bootBtn.addEventListener("pointerdown", (e) => {
  e.preventDefault();
  try { bootBtn.setPointerCapture(e.pointerId); } catch (err) { /* not capturable */ }
  bootDown();
});
bootBtn.addEventListener("pointerup", bootUp);
bootBtn.addEventListener("pointercancel", bootUp);
bootBtn.addEventListener("pointerleave", bootUp);
bootBtn.addEventListener("contextmenu", (e) => e.preventDefault());
// The browser's own click from a key press on the focused button is not the BOOT button.
bootBtn.addEventListener("keydown", (e) => { if (e.key === " " || e.key === "Enter") e.preventDefault(); });

// B and the space bar, unless the visitor is using a control.
function keyIsForBoot(e) {
  if (e.ctrlKey || e.metaKey || e.altKey) return false;
  if (e.key !== "b" && e.key !== "B" && e.key !== " ") return false;
  const el = document.activeElement;
  const tag = el && el.tagName;
  if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA" || tag === "A") return false;
  if (tag === "BUTTON" && el !== bootBtn) return false;
  return true;
}
document.addEventListener("keydown", (e) => {
  if (!keyIsForBoot(e)) return;
  e.preventDefault();
  if (!e.repeat) bootDown();
});
document.addEventListener("keyup", (e) => {
  if (e.key === "b" || e.key === "B" || e.key === " ") {
    if (bootHeld && keyIsForBoot(e)) { e.preventDefault(); bootUp(); }
  }
});
window.addEventListener("blur", () => { bootUp(); });

// ---------------------------------------------------------------- GPS source: my location

let locWatch = null;
let locGpsOffSent = false;
let locLastPosReal = 0;
let locPrev = null;         // the previous position: {lat, lon, t}
let locBearingFrom = null;  // where the heading was last worked out from
let locHeading = 0;
let locRecent = [];         // arrival times (performance.now) of recent positions

const M_PER_DEG = 111320;
function offsetM(a, b) {    // metres east and north from a to b (flat earth)
  return [(b.lon - a.lon) * M_PER_DEG * Math.cos(a.lat * Math.PI / 180), (b.lat - a.lat) * M_PER_DEG];
}

function setLocStatus(text) { locStatusEl.textContent = text; }

function startLocation() {
  locPrev = null;
  locBearingFrom = null;
  locHeading = 0;
  locRecent = [];
  locGpsOffSent = false;
  locLastPosReal = performance.now();
  if (!navigator.geolocation) {
    setLocStatus("This browser has no location.");
    return;
  }
  setLocStatus("Waiting for your location…");
  locWatch = navigator.geolocation.watchPosition(onPosition, onPositionError,
    { enableHighAccuracy: true, maximumAge: 0 });
}

function stopLocation() {
  if (locWatch !== null) navigator.geolocation.clearWatch(locWatch);
  locWatch = null;
}

function onPosition(pos) {
  const c = pos.coords;
  const here = { lat: c.latitude, lon: c.longitude, t: pos.timestamp };

  // Speed in km/h.
  let speedKmh = 0;
  if (typeof c.speed === "number" && !Number.isNaN(c.speed)) speedKmh = c.speed * 3.6;
  else if (locPrev && here.t > locPrev.t) {
    const [dx, dy] = offsetM(locPrev, here);
    speedKmh = Math.hypot(dx, dy) / ((here.t - locPrev.t) / 1000) * 3.6;
  }

  // Heading in degrees; the last one is kept while the visitor is stopped.
  if (typeof c.heading === "number" && !Number.isNaN(c.heading)) {
    locHeading = c.heading;
    locBearingFrom = here;
  } else if (locBearingFrom) {
    const [dx, dy] = offsetM(locBearingFrom, here);
    if (Math.hypot(dx, dy) >= 2) {
      locHeading = (Math.atan2(dx, dy) * 180 / Math.PI + 360) % 360;
      locBearingFrom = here;
    }
  } else {
    locBearingFrom = here;
  }
  locPrev = here;

  const now = performance.now();
  locLastPosReal = now;
  locGpsOffSent = false;
  locRecent.push(now);
  while (locRecent.length > 2 && now - locRecent[0] > 10000) locRecent.shift();

  if (sim && !starting && !sim.halted) {
    sim.gpsFix({
      lat: c.latitude, lon: c.longitude, speedKmh, headingDeg: locHeading,
      accuracyM: c.accuracy, utcMs: utcAt(vNow), sats: 10,
    });
  }

  let text = `±${Math.round(c.accuracy)} m`;
  if (locRecent.length >= 2) {
    const rate = (locRecent.length - 1) / ((now - locRecent[0]) / 1000);
    if (rate >= 1.5) text += ` · ${Math.round(rate)} fixes a second`;
    else if (rate >= 0.75) text += " · 1 fix a second";
    else text += ` · 1 fix every ${Math.round(1 / rate)} seconds`;
  }
  setLocStatus(text);
}

function onPositionError(err) {
  if (err.code === 1) setLocStatus("Location was refused. Allow it for this site in the browser, or use the simulated car.");
  else setLocStatus("No position yet. Go outside, or use the simulated car.");
}

// ---------------------------------------------------------------- controls

function setPressed(group, attr, value) {
  for (const b of group.querySelectorAll("button")) {
    b.setAttribute("aria-pressed", String(b.dataset[attr] === String(value)));
  }
}

function setSource(next) {
  if (next === source) return;
  stopLocation();
  source = next;
  if (source !== "car") speed = 1;
  nextFixAt = vNow;
  setPressed($("source"), "source", source);
  setPressed($("speed"), "speed", speed);
  $("panel-car").hidden = source !== "car";
  $("speed-row").hidden = source === "off";
  locStatusEl.hidden = source !== "loc";
  $("off-info").hidden = source !== "off";
  for (const b of $("speed").querySelectorAll("button")) b.disabled = source !== "car";
  if (source === "loc") startLocation();
  else if (source === "off" && sim && !starting && !sim.halted) sim.gpsOff();
  renderStatus();
}

for (const b of $("source").querySelectorAll("button")) {
  b.addEventListener("click", () => setSource(b.dataset.source));
}
for (const b of $("speed").querySelectorAll("button")) {
  b.addEventListener("click", () => {
    speed = Number(b.dataset.speed);
    setPressed($("speed"), "speed", speed);
  });
}
driveBtn.addEventListener("click", () => {
  if (!car) return;
  car.drive(!car.driving);
  driveBtn.textContent = car.driving ? "Park" : "Drive";
  renderStatus();
});

$("reset").addEventListener("click", async () => {
  if (!confirm("Start again as a new unit? This deletes what it saved in this browser.")) return;
  noSave = true;
  await deleteState();
  location.reload();
});

// ---------------------------------------------------------------- saving when the page goes away

window.addEventListener("pagehide", () => maybeSave(true));
document.addEventListener("visibilitychange", () => {
  if (document.hidden) maybeSave(true);
  else lastFrameReal = null;
});

// ---------------------------------------------------------------- start

async function main() {
  setSource("car");
  $("panel-car").hidden = false;
  for (const b of $("speed").querySelectorAll("button")) b.disabled = false;
  requestAnimationFrame(frame);
  try {
    const host = await import("./host.js");
    createSim = host.createSim;
    W = host.W;
    H = host.H;
    rgba = new Uint8ClampedArray(W * H * 4);
    image = new ImageData(rgba, W, H);
    const [wasmRes, tracksRes, saved] = await Promise.all([fetch("trackrat.wasm"), fetch("tracks.json"), loadState()]);
    if (!wasmRes.ok) throw new Error(`trackrat.wasm: HTTP ${wasmRes.status}`);
    if (!tracksRes.ok) throw new Error(`tracks.json: HTTP ${tracksRes.status}`);
    wasmBytes = new Uint8Array(await wasmRes.arrayBuffer());
    tracksBytes = new Uint8Array(await tracksRes.arrayBuffer());
    latestState = saved;
    car = createCar(JSON.parse(new TextDecoder().decode(tracksBytes)));
  } catch (e) {
    console.error(e);
    fatal = "The simulator could not load.";
    renderStatus();
    return;
  }
  await powerOn();
}

main();
