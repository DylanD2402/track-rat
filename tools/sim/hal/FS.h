// Stand-in for the Arduino-ESP32 FS library (tools/build_sim.py): files in memory, in two volumes.
// The firmware sees the same File and FS it sees on the device; the files live in tools/sim/vfs.cpp.
#pragma once
#include <Arduino.h>
#include <memory>
#include <string>
#include <time.h>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace sim {
struct Volume;
struct OpenFile;
}  // namespace sim

struct sdmmc_card_t;

namespace fs {

enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

class File : public Stream {
public:
  File() {}
  explicit File(std::shared_ptr<sim::OpenFile> f) : f_(std::move(f)) {}

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t size) override;
  int available() override;
  int read() override;
  int peek() override;
  void flush() override {}
  size_t read(uint8_t* buf, size_t size);
  size_t readBytes(char* buf, size_t size) override { return read((uint8_t*)buf, size); }
  size_t readBytes(uint8_t* buf, size_t size) { return read(buf, size); }
  bool seek(uint32_t pos, SeekMode mode = SeekSet);
  size_t position() const;
  size_t size() const;
  void close();
  time_t getLastWrite();
  const char* path() const;
  const char* name() const;  // the name alone
  bool isDirectory() const;
  File openNextFile(const char* mode = FILE_READ);
  void rewindDirectory();
  operator bool() const { return (bool)f_; }

private:
  std::shared_ptr<sim::OpenFile> f_;
};

class FS {
public:
  explicit FS(sim::Volume* v = nullptr) : vol_(v) {}
  File open(const char* path, const char* mode = FILE_READ, const bool create = false);
  File open(const String& path, const char* mode = FILE_READ, const bool create = false) { return open(path.c_str(), mode, create); }
  bool exists(const char* path);
  bool exists(const String& path) { return exists(path.c_str()); }
  bool remove(const char* path);
  bool remove(const String& path) { return remove(path.c_str()); }
  bool rename(const char* from, const char* to);
  bool rename(const String& from, const String& to) { return rename(from.c_str(), to.c_str()); }
  bool mkdir(const char* path);
  bool mkdir(const String& path) { return mkdir(path.c_str()); }
  bool rmdir(const char* path);
  bool rmdir(const String& path) { return rmdir(path.c_str()); }
  uint64_t totalBytes() { return 16ULL * 1024 * 1024 * 1024; }
  uint64_t usedBytes();

protected:
  sim::Volume* vol_;
};

class LittleFSFS : public FS {
public:
  LittleFSFS();
  bool begin(bool = false, const char* = "/littlefs", uint8_t = 10, const char* = "spiffs") { return true; }
  void end() {}
};

class SDMMCFS : public FS {
public:
  SDMMCFS();
  bool setPins(int, int, int, int = -1, int = -1, int = -1) { return true; }
  bool begin(const char* = "/sdcard", bool = false, bool = false, int = 20000) { return true; }
  void end() {}
  uint64_t cardSize() { return 16ULL * 1024 * 1024 * 1024; }

protected:
  sdmmc_card_t* _card = nullptr;  // (Storage.cpp reads it)
};

}  // namespace fs

using fs::File;
using fs::FS;
