// Runs the Track Rat firmware, built as WebAssembly (tools/build_sim.py), in a browser or in Node 18+.
// No dependencies. This file is the only one that knows the module's own function names.
//
//   const sim = await createSim({ wasm, tracksJson, state: null, startMs: 0, utcMs: Date.now() });
//   sim.tick(nowMs); sim.touch(x, y, true); sim.gpsFix({ ... }); sim.frame(rgba);
//
// The unit's time is virtual: it only moves when tick() is called, to the time it is given.

export const W = 320, H = 240;

// Thrown from inside the module when the unit sleeps or restarts, so the call unwinds
class HaltSignal extends Error {
  constructor(why) {
    super(why === 2 ? "restart" : "off");
    this.why = why;
  }
}

async function toBytes(wasm) {
  let w = await wasm;
  if (typeof Response !== "undefined" && w instanceof Response) w = await w.arrayBuffer();
  if (w instanceof ArrayBuffer) return new Uint8Array(w);
  if (ArrayBuffer.isView(w)) return new Uint8Array(w.buffer, w.byteOffset, w.byteLength);
  throw new Error("createSim: opts.wasm must be an ArrayBuffer, a Uint8Array or a Response");
}

export async function createSim(opts) {
  const bytes = await toBytes(opts.wasm);
  const startMs = opts.startMs ?? 0;
  const utcAtStart = opts.utcMs ?? Date.now();
  const log = opts.log || (() => {});
  const text = new TextDecoder();

  let ex = null; // the module's exports
  let now = startMs; // the unit's clock, as last given
  let clockAdjust = 0; // the system clock set from the GPS, ms
  let stdout = "";

  const mem = () => ex.memory.buffer;
  const wallMs = () => utcAtStart + (now - startMs) + clockAdjust;

  // The few system calls the module's C library makes
  const wasi = {
    fd_write(fd, iovs, iovsLen, outPtr) {
      const dv = new DataView(mem());
      let n = 0;
      for (let i = 0; i < iovsLen; i++) {
        const ptr = dv.getUint32(iovs + i * 8, true), len = dv.getUint32(iovs + i * 8 + 4, true);
        stdout += text.decode(new Uint8Array(mem(), ptr, len));
        n += len;
      }
      dv.setUint32(outPtr, n, true);
      let nl;
      while ((nl = stdout.indexOf("\n")) >= 0) {
        log(stdout.slice(0, nl));
        stdout = stdout.slice(nl + 1);
      }
      return 0;
    },
    fd_close: () => 0,
    fd_seek: () => 70, // ESPIPE
    environ_sizes_get(countPtr, sizePtr) {
      const dv = new DataView(mem());
      dv.setUint32(countPtr, 0, true);
      dv.setUint32(sizePtr, 0, true);
      return 0;
    },
    environ_get: () => 0,
    clock_time_get(id, _precision, outPtr) {
      const ms = id === 0 ? wallMs() : now - startMs; // 0: the wall clock; else a clock that only counts
      new DataView(mem()).setBigUint64(outPtr, BigInt(Math.max(0, Math.round(ms * 1e6))), true);
      return 0;
    },
    proc_exit(code) {
      throw new Error("the firmware exited (" + code + ")");
    },
  };

  const env = {
    trackrat_log(ptr, len) {
      log(text.decode(new Uint8Array(mem(), ptr, len)));
    },
    trackrat_halt(why) {
      throw new HaltSignal(why);
    },
    trackrat_set_realtime(ms) {
      clockAdjust += ms - wallMs();
    },
  };

  const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: wasi, env });
  ex = instance.exports;

  // Copies bytes into the module's memory (freed with sim_free)
  const put = (data) => {
    const ptr = ex.sim_alloc(data.length || 1);
    new Uint8Array(mem(), ptr, data.length).set(data);
    return ptr;
  };

  const sim = {
    halted: null,
    error: null,

    tick(nowMs) {
      if (this.halted) return false;
      now = nowMs;
      this._run(() => ex.sim_tick(nowMs));
      return !this.halted;
    },
    touch(x, y, down) {
      if (this.halted) return;
      ex.sim_touch(Math.round(x), Math.round(y), down ? 1 : 0);
    },
    button(down) {
      if (this.halted) return;
      ex.sim_button(down ? 1 : 0);
    },
    gpsFix(f) {
      if (this.halted) return;
      ex.sim_gps_fix(f.lat, f.lon, f.speedKmh ?? 0, f.headingDeg ?? 0, f.accuracyM ?? 1.0, f.utcMs ?? wallMs(), f.sats ?? 12);
    },
    gpsOff() {
      if (this.halted) return;
      ex.sim_gps_off();
    },
    frame(rgba) {
      // RGB565 to RGBA. The picture is upright whichever way up the unit is set.
      const px = new Uint16Array(mem(), fb, W * H);
      for (let i = 0, o = 0; i < W * H; i++, o += 4) {
        const c = px[i];
        const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        rgba[o] = (r << 3) | (r >> 2);
        rgba[o + 1] = (g << 2) | (g >> 4);
        rgba[o + 2] = (b << 3) | (b >> 2);
        rgba[o + 3] = 255;
      }
      try {
        return ex.sim_backlight() / 100;
      } catch (e) {
        return 0;
      }
    },
    stateSeq() {
      return ex.sim_state_seq();
    },
    exportState() {
      const ptr = ex.sim_export(), len = ex.sim_export_len();
      return new Uint8Array(mem(), ptr, len).slice();
    },
    debug() {
      const ptr = ex.sim_debug();
      const u8 = new Uint8Array(mem());
      let end = ptr;
      while (u8[end]) end++;
      const [state, laps, lastLapMs] = text.decode(u8.subarray(ptr, end)).split(",");
      return { state, laps: Number(laps), lastLapMs: Number(lastLapMs) };
    },

    // Runs a call into the module; a sleep or restart (HaltSignal) or a crash ends the run
    _run(fn) {
      try {
        fn();
      } catch (e) {
        if (e instanceof HaltSignal) {
          this.halted = e.why === 2 ? "restart" : "off";
        } else {
          this.halted = "error";
          this.error = e && e.message ? e.message : String(e);
        }
      }
    },
  };

  // Power on: the libc, the clock, the built-in tracks, what was saved, then the firmware's setup()
  ex._initialize();
  ex.sim_init(startMs);
  const tracks = opts.tracksJson instanceof Uint8Array ? opts.tracksJson : new Uint8Array(opts.tracksJson || 0);
  let p = put(tracks);
  ex.sim_set_tracks(p, tracks.length);
  ex.sim_free(p);
  if (opts.state && opts.state.length) {
    p = put(opts.state);
    if (!ex.sim_import(p, opts.state.length)) log("createSim: the saved state was not understood; starting as a new unit");
    ex.sim_free(p);
  }
  const fb = ex.sim_frame();
  sim._run(() => ex.sim_setup());
  return sim;
}
