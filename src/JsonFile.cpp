/**
 * Track Rat - saving JSON files safely: implementation
 */

#include "JsonFile.h"
#include "Config.h"

size_t writeJsonFile(fs::FS& fs, const String& path, const JsonDocument& doc, bool pretty) {
  if (doc.overflowed()) {  // values were left out: don't put an incomplete file over a complete one
    DEBUG_PRINTF("Save: %s didn't fit in memory - not saved\n", path.c_str());
    return 0;
  }
  String tmp = path + ".tmp";
  File f = fs.open(tmp, "w");
  if (!f) {
    DEBUG_PRINTF("Save: could not open %s for writing\n", tmp.c_str());
    return 0;
  }
  size_t want = pretty ? measureJsonPretty(doc) : measureJson(doc);
  size_t n = pretty ? serializeJsonPretty(doc, f) : serializeJson(doc, f);
  f.close();
  if (n != want) {
    DEBUG_PRINTF("Save: writing %s stopped at %u of %u bytes - not saved\n", tmp.c_str(), (unsigned)n,
                 (unsigned)want);
    fs.remove(tmp);
    return 0;
  }
  if (fs.rename(tmp, path)) return n;  // LittleFS replaces the old file in one step
  fs.remove(path);                     // FAT won't rename over an existing file
  return fs.rename(tmp, path) ? n : 0;
}
