/**
 * Track Rat - Storage
 *
 * Mounts the SD card (SDIO 4-bit) and the internal LittleFS partition.
 * Sessions are written to the SD card when present, otherwise to flash.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <FS.h>
#include <string>

namespace Storage {

void begin();
bool sdMounted();
bool flashMounted();

// Where session logs go: SD if mounted, else LittleFS, else nullptr.
fs::FS* sessionFS();
const char* sessionFSName();

// A folder read one entry at a time, which can be put down between loop passes.
// On the SD card it uses FatFs's f_readdir, which carries on from where the last call stopped; opening each
// entry by path (File::openNextFile) searches the folder from its start every time, so listing n files costs
// n^2 steps. LittleFS and anything else fall back to openNextFile, which is fine there.
class DirLister {
public:
  struct Entry {
    std::string name;  // the name alone, no folder
    uint32_t size = 0;
    bool isDir = false;
  };
  DirLister();
  ~DirLister();
  DirLister(const DirLister&) = delete;
  DirLister& operator=(const DirLister&) = delete;

  bool open(fs::FS& fs, const char* path);  // false if there is no such folder
  bool next(Entry& out);                    // false at the end (and the folder is closed)
  void close();
  bool isOpen() const { return state != nullptr; }

private:
  struct State;
  State* state = nullptr;
};

}  // namespace Storage

#endif  // STORAGE_H
