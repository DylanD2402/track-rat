// Test of site/try/saved.js. Run: node test/test_try_saved.mjs
import assert from "node:assert/strict";
import { readSaved, latestSession } from "../site/try/saved.js";

// Writes a TRS1 blob the way exportSaved() in tools/sim/vfs.cpp does.
function blobOf(prefs, files) {
  const out = [];
  const enc = new TextEncoder();
  const u32 = (n) => out.push(n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255);
  const bytes = (b) => { u32(b.length); out.push(...b); };
  out.push(0x54, 0x52, 0x53, 0x31);
  u32(prefs.length);
  for (const p of prefs) { bytes(enc.encode(p.ns)); bytes(enc.encode(p.key)); bytes(p.value); }
  u32(files.length);
  for (const f of files) {
    bytes(enc.encode(f.path));
    out.push(f.dir ? 1 : 0);
    u32(f.mtime % 4294967296);
    u32(Math.floor(f.mtime / 4294967296));
    bytes(f.data ?? new Uint8Array(0));
  }
  return new Uint8Array(out);
}
const T = (s) => new TextEncoder().encode(s);
const file = (path, mtime, text = "", dir = false) => ({ path, mtime, data: T(text), dir });

// reading back
{
  const big = 5 * 4294967296 + 123;
  const blob = blobOf(
    [{ ns: "trackrat", key: "units", value: Uint8Array.of(1, 2, 3) }],
    [file("/sessions", 10, "", true), file("/sessions/a.vbo", big, "héllo"), file("/x/ü.txt", 7)],
  );
  const s = readSaved(blob);
  assert.equal(s.prefs.length, 1);
  assert.equal(s.prefs[0].ns, "trackrat");
  assert.equal(s.prefs[0].key, "units");
  assert.deepEqual([...s.prefs[0].value], [1, 2, 3]);
  assert.equal(s.files.length, 3);
  assert.equal(s.files[0].dir, true);
  assert.equal(s.files[1].dir, false);
  assert.equal(s.files[1].mtime, big);
  assert.equal(new TextDecoder().decode(s.files[1].bytes), "héllo");
  assert.equal(s.files[2].path, "/x/ü.txt");
}

// bad input
{
  const good = blobOf([{ ns: "a", key: "b", value: T("v") }], [file("/sessions/a.vbo", 1, "data")]);
  assert.equal(readSaved(null), null);
  assert.equal(readSaved([1, 2, 3]), null);
  assert.equal(readSaved(new Uint8Array(0)), null);
  const wrong = good.slice();
  wrong[3] = 0x32;
  assert.equal(readSaved(wrong), null);
  for (let n = 0; n < good.length; n++) assert.equal(readSaved(good.subarray(0, n)), null, `cut at ${n}`);
  assert.notEqual(readSaved(good), null);
  assert.deepEqual(readSaved(blobOf([], [])), { prefs: [], files: [] });
}

// latestSession
{
  const files = readSaved(blobOf([], [
    file("/sessions", 50, "", true),
    file("/sessions/20261123_100000_calabogie.vbo", 100, "old"),
    file("/sessions/20261123_100000_calabogie.json", 101, "{}"),
    file("/sessions/20261123_181502_calabogie.vbo", 200, "new"),
    file("/sessions/20261123_181502_calabogie.json.tmp", 201, "tmp"),
    file("/sessions/20261123_181502_calabogie_note.json", 202, "note"),
    file("/sessions/20261123_999999_x.vbo", 900, "dir", true),
    file("/other/20261123_999999_x.vbo", 900, "elsewhere"),
  ])).files;
  let s = latestSession(files);
  assert.equal(s.base, "20261123_181502_calabogie");
  assert.equal(s.vbo.name, "20261123_181502_calabogie.vbo");
  assert.equal(new TextDecoder().decode(s.vbo.bytes), "new");
  assert.equal(s.json, null);

  files.push(...readSaved(blobOf([], [file("/sessions/20261123_181502_calabogie.json", 205, "{\"a\":1}")])).files);
  s = latestSession(files);
  assert.equal(s.json.name, "20261123_181502_calabogie.json");
  assert.equal(new TextDecoder().decode(s.json.bytes), "{\"a\":1}");

  // a tie goes to the larger path
  const tie = readSaved(blobOf([], [file("/sessions/a.vbo", 5), file("/sessions/b.vbo", 5)])).files;
  assert.equal(latestSession(tie).base, "b");

  assert.equal(latestSession([]), null);
  assert.equal(latestSession(readSaved(blobOf([], [file("/sessions/a.json", 1), file("/sessions", 1, "", true)])).files), null);
}

console.log("test_try_saved: ok");
