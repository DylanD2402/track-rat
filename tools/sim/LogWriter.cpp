// Stand-in for src/LogWriter.cpp (tools/build_sim.py): no writer task, so each call writes straight to the
// file. Same header (include/LogWriter.h).
#include "LogWriter.h"
#include "Config.h"

namespace LogWriter {

static fs::FS* fsys = nullptr;
static File file;
static String filePath;
static uint32_t written = 0;

bool begin(fs::FS& fs) {
  fsys = &fs;
  return true;
}

bool running() { return fsys != nullptr; }

bool open(const String& path, const char* header, size_t headerLen) {
  if (!fsys) return false;
  if (file) file.close();
  filePath = path;
  file = fsys->open(path, FILE_WRITE);
  written = 0;
  if (!file) {
    DEBUG_PRINTF("Log: could not create %s\n", path.c_str());
    return true;  // (as on the device: the buffer took it, and the task found it could not make the file)
  }
  written += file.write((const uint8_t*)header, headerLen);
  return true;
}

bool append(const char* data, size_t len) {
  if (!fsys) return false;
  if (file) written += file.write((const uint8_t*)data, len);
  return true;
}

bool rename(const String& from, const String& to) {
  if (!fsys) return false;
  const bool wasOpen = file && filePath == from;
  if (wasOpen) file.close();
  const bool ok = fsys->rename(from, to);
  if (wasOpen) {
    if (ok) filePath = to;
    file = fsys->open(filePath, FILE_APPEND);
  }
  return true;
}

void close() {
  if (file) file.close();
}

void poll() {}
bool waitIdle(uint32_t) { return true; }
uint32_t bytesWritten() { return written; }
uint32_t bufferedBytes() { return 0; }
uint32_t slowestWriteMs() { return 0; }

}  // namespace LogWriter
