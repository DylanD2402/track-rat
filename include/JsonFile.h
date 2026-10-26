/**
 * Track Rat - saving JSON files safely
 *
 * The document is written to "<path>.tmp", which then replaces the file, so
 * a power cut leaves either the old file or the new one, never half of one
 * (boot recovery finishes a save caught between the two). A document that
 * ran out of memory, or a write that fell short (card full or pulled out),
 * leaves the old file as it was. The text goes to the file in large blocks.
 */

#ifndef JSON_FILE_H
#define JSON_FILE_H

#include <ArduinoJson.h>
#include <FS.h>

// Bytes written, or 0 if the file wasn't saved.
size_t writeJsonFile(fs::FS& fs, const String& path, const JsonDocument& doc, bool pretty);
// The same, compact but with each element of the top-level object's arrays
// on a line of its own (tracks.json: one track per line). A third the size
// of pretty-printed, and still easy to edit by hand.
size_t writeJsonFileByLine(fs::FS& fs, const String& path, const JsonDocument& doc);

// --- The one way to save a file safely ----------------------------------------
//
// Write "<path>.tmp" completely and close it, then replaceFile(). FAT can't
// rename over an existing file, so the old one is removed first: a power cut
// between the two leaves no `path`, but a whole `.tmp`, which
// finishLeftoverSave() moves into place the next time the file is read. A cut
// while the .tmp is still being written leaves the old file alone.

// Does the file at path hold a whole thing (not cut off)? For finishLeftoverSave.
typedef bool (*FileCheck)(fs::FS& fs, const String& path);

// A complete, closed tmp becomes path. LittleFS replaces in one step; on FAT the old file is removed first.
bool replaceFile(fs::FS& fs, const String& tmp, const String& path);
// data to path.tmp, then replaceFile. False (the old file untouched) if any of it didn't reach the card.
bool writeFileSafe(fs::FS& fs, const String& path, const uint8_t* data, size_t len);
// Call before reading path: if it's missing and src (default path.tmp) is whole by `whole`, rename it into
// place. True if path exists afterwards. An existing path is never touched.
bool finishLeftoverSave(fs::FS& fs, const String& path, FileCheck whole = nullptr, const String& src = String());
// The syntax of a whole JSON file checks out (keeps nothing in memory)
bool jsonFileWhole(fs::FS& fs, const String& path);

#endif  // JSON_FILE_H
