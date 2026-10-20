/**
 * Track Rat - best laps on the card (implementation)
 */

#include "BestLapStore.h"
#include "Config.h"
#include "Storage.h"

namespace BestLapStore {

static const char* DIR = "/tracks";

static String pathFor(const String& trackId, Kind kind) {
  return String(DIR) + "/" + trackId + (kind == Kind::Ever ? "/best.bin" : "/day.bin");
}

static bool readFile(fs::FS& fs, const String& path, bestlap::Record& out, bool withPath) {
  File f = fs.open(path, "r");
  if (!f) return false;
  uint8_t head[bestlap::HEADER_BYTES];
  bool ok = false;
  if (f.read(head, sizeof(head)) == (int)sizeof(head)) {
    long n = bestlap::pointCount(head, sizeof(head));
    if (n >= 0 && !withPath) {
      ok = bestlap::decode(head, sizeof(head), out, false);
    } else if (n >= 0) {
      std::vector<uint8_t> data(bestlap::HEADER_BYTES + (size_t)n * bestlap::POINT_BYTES);
      memcpy(data.data(), head, sizeof(head));
      size_t want = data.size() - sizeof(head);
      ok = f.read(data.data() + sizeof(head), want) == want && bestlap::decode(data.data(), data.size(), out, true);
    }
  }
  f.close();
  return ok;
}

bool load(const String& trackId, Kind kind, bestlap::Record& out, bool withPath) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || trackId.length() == 0) return false;
  const String path = pathFor(trackId, kind);
  if (!fs->exists(path)) {
    // A save caught between writing the new copy and renaming it: finish it if the copy is whole
    const String tmp = path + ".tmp";
    bestlap::Record r;
    if (!fs->exists(tmp) || !readFile(*fs, tmp, r, true) || !fs->rename(tmp, path)) return false;
  }
  return readFile(*fs, path, out, withPath);
}

bool save(const String& trackId, Kind kind, const bestlap::Record& r) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || trackId.length() == 0 || !r.valid()) return false;
  const String dir = String(DIR) + "/" + trackId;
  if (!fs->exists(DIR)) fs->mkdir(DIR);
  if (!fs->exists(dir)) fs->mkdir(dir);

  const String path = pathFor(trackId, kind);
  const String tmp = path + ".tmp";
  const std::vector<uint8_t> data = bestlap::encode(r);
  File f = fs->open(tmp, "w");
  if (!f) return false;
  const size_t written = f.write(data.data(), data.size());
  f.close();
  if (written != data.size()) {
    fs->remove(tmp);
    return false;
  }
  fs->remove(path);  // FAT won't rename onto an existing file
  if (!fs->rename(tmp, path)) return false;
  DEBUG_PRINTF("Best lap: %s %s saved to %s:%s (%u points)\n", trackId.c_str(),
               kind == Kind::Ever ? "PB" : "best of the day", Storage::sessionFSName(), path.c_str(),
               (unsigned)r.path.size());
  return true;
}

}  // namespace BestLapStore
