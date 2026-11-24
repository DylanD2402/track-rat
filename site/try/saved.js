// Reads the saved-data blob the firmware module exports (sim.exportState()), for the "Try it" page's
// "Download your session" button. No DOM use, so Node can test it (test/test_try_saved.mjs).
//
// The blob's format is written by exportSaved() in tools/sim/vfs.cpp; this reader and that writer
// must change together.

const MAGIC = [0x54, 0x52, 0x53, 0x31]; // "TRS1"
const SESSIONS = "/sessions/";

// bytes: a Uint8Array. Returns { prefs: [{ ns, key, value }], files: [{ path, dir, mtime, bytes }] },
// or null if it isn't a saved blob or is cut short.
export function readSaved(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 4) return null;
  for (let i = 0; i < 4; i++) if (bytes[i] !== MAGIC[i]) return null;
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const text = new TextDecoder();
  let at = 4;
  let bad = false;
  const u32 = () => {
    if (at + 4 > bytes.length) { bad = true; return 0; }
    at += 4;
    return dv.getUint32(at - 4, true);
  };
  const u64 = () => {
    const lo = u32();
    const hi = u32();
    return lo + hi * 4294967296;
  };
  const blob = () => {
    const n = u32();
    if (bad || at + n > bytes.length) { bad = true; return new Uint8Array(0); }
    at += n;
    return bytes.subarray(at - n, at);
  };

  const prefs = [];
  const nPrefs = u32();
  for (let i = 0; i < nPrefs && !bad; i++) {
    const ns = text.decode(blob());
    const key = text.decode(blob());
    const value = blob();
    prefs.push({ ns, key, value });
  }
  const files = [];
  const nFiles = bad ? 0 : u32();
  for (let i = 0; i < nFiles && !bad; i++) {
    const path = text.decode(blob());
    if (at + 1 > bytes.length) { bad = true; break; }
    const dir = bytes[at++] === 1;
    const mtime = u64();
    const data = blob();
    files.push({ path, dir, mtime, bytes: data });
  }
  return bad ? null : { prefs, files };
}

// files: readSaved(...).files. The newest session's log (.vbo) and its summary (.json, null while the
// session is still running), or null if there is no session.
export function latestSession(files) {
  let best = null;
  for (const f of files) {
    if (f.dir || !f.path.startsWith(SESSIONS) || !f.path.endsWith(".vbo")) continue;
    if (!best || f.mtime > best.mtime || (f.mtime === best.mtime && f.path > best.path)) best = f;
  }
  if (!best) return null;
  const base = best.path.slice(SESSIONS.length, -4);
  const jsonPath = `${SESSIONS}${base}.json`;
  const json = files.find((f) => !f.dir && f.path === jsonPath) || null;
  return {
    base,
    vbo: { name: `${base}.vbo`, bytes: best.bytes },
    json: json ? { name: `${base}.json`, bytes: json.bytes } : null,
  };
}
