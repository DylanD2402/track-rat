/**
 * Track Rat - saving JSON files safely: implementation
 */

#include "JsonFile.h"
#include "Config.h"
#include "JsonText.h"
#include "esp_heap_caps.h"

static const size_t BLOCK_BYTES = 8192;  // one file write

// ArduinoJson writes a few bytes at a time: gather them into blocks for the
// file. With lineDepth, also starts a line for each element that deep.
class BlockWriter : public Print {
public:
  BlockWriter(File& file, uint8_t* block, size_t size, int lineDepth)
      : file_(file), block_(block), size_(size), lines_(lineDepth), breakLines_(lineDepth > 0) {}

  size_t write(uint8_t c) override {
    if (breakLines_ && lines_.breakBefore((char)c)) put('\n');
    put(c);
    return 1;
  }
  size_t write(const uint8_t* s, size_t n) override {
    for (size_t i = 0; i < n; i++) write(s[i]);
    return n;
  }

  // Writes what's left. False if any of the text didn't reach the file.
  bool finish() {
    writeBlock();
    return written_ == total_;
  }
  size_t total() const { return total_; }
  size_t written() const { return written_; }

private:
  File& file_;
  uint8_t* block_;
  size_t size_, used_ = 0;
  size_t total_ = 0, written_ = 0;
  jsontext::LineBreaker lines_;
  bool breakLines_;

  void put(uint8_t c) {
    if (used_ == size_) writeBlock();
    block_[used_++] = c;
    total_++;
  }
  void writeBlock() {
    if (used_ && written_ + used_ == total_) written_ += file_.write(block_, used_);  // nothing more after a short write
    used_ = 0;
  }
};

// lineDepth 0: compact or pretty; else see writeJsonFileByLine.
static size_t saveJson(fs::FS& fs, const String& path, const JsonDocument& doc, bool pretty, int lineDepth) {
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
  uint8_t small[256];  // short of memory: smaller writes
  uint8_t* block = (uint8_t*)heap_caps_malloc(BLOCK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  BlockWriter out(f, block ? block : small, block ? BLOCK_BYTES : sizeof(small), lineDepth);
  if (pretty) serializeJsonPretty(doc, out);
  else serializeJson(doc, out);
  bool complete = out.finish();
  free(block);
  f.close();
  if (!complete) {
    DEBUG_PRINTF("Save: writing %s stopped at %u of %u bytes - not saved\n", tmp.c_str(), (unsigned)out.written(),
                 (unsigned)out.total());
    fs.remove(tmp);
    return 0;
  }
  return replaceFile(fs, tmp, path) ? out.total() : 0;
}

bool replaceFile(fs::FS& fs, const String& tmp, const String& path) {
  if (fs.rename(tmp, path)) return true;  // LittleFS replaces the old file in one step
  fs.remove(path);                        // FAT won't rename over an existing file
  return fs.rename(tmp, path);            // if this fails the whole tmp is still there for finishLeftoverSave
}

bool writeFileSafe(fs::FS& fs, const String& path, const uint8_t* data, size_t len) {
  const String tmp = path + ".tmp";
  File f = fs.open(tmp, "w");
  if (!f) return false;
  const size_t written = f.write(data, len);
  f.close();
  if (written != len) {
    fs.remove(tmp);
    return false;
  }
  return replaceFile(fs, tmp, path);
}

bool finishLeftoverSave(fs::FS& fs, const String& path, FileCheck whole, const String& src) {
  if (fs.exists(path)) return true;
  const String from = src.length() ? src : path + ".tmp";
  return fs.exists(from) && (!whole || whole(fs, from)) && fs.rename(from, path);
}

bool jsonFileWhole(fs::FS& fs, const String& path) {
  File f = fs.open(path, "r");
  if (!f) return false;
  StaticJsonDocument<16> filter;  // checks the syntax of the whole file, keeps nothing
  filter["-"] = true;
  StaticJsonDocument<64> doc;
  const DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  return !err;
}

size_t writeJsonFile(fs::FS& fs, const String& path, const JsonDocument& doc, bool pretty) {
  return saveJson(fs, path, doc, pretty, 0);
}

size_t writeJsonFileByLine(fs::FS& fs, const String& path, const JsonDocument& doc) {
  return saveJson(fs, path, doc, false, 2);  // 2: inside the top-level object's arrays
}
