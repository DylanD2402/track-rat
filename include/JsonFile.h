/**
 * Track Rat - saving JSON files safely
 *
 * The document is written to "<path>.tmp", which then replaces the file, so
 * a power cut leaves either the old file or the new one, never half of one
 * (boot recovery finishes a save caught between the two). A document that
 * ran out of memory, or a write that fell short (card full or pulled out),
 * leaves the old file as it was.
 */

#ifndef JSON_FILE_H
#define JSON_FILE_H

#include <ArduinoJson.h>
#include <FS.h>

// Bytes written, or 0 if the file wasn't saved.
size_t writeJsonFile(fs::FS& fs, const String& path, const JsonDocument& doc, bool pretty);

#endif  // JSON_FILE_H
