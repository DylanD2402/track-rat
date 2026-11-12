// The two file volumes of the simulated unit: the flash (LittleFS) and the SD card, in memory, behind the same
// File and FS the firmware uses (tools/sim/hal/FS.h). Also the saved-settings store (Preferences.h), and the
// export and import of everything that is saved: the SD card's files and the settings.
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "vfs.h"

namespace sim {

struct Node {
  bool dir = false;
  std::vector<uint8_t> data;
  int64_t mtime = 0;
};

struct Volume {
  std::map<std::string, std::shared_ptr<Node>> nodes;  // by full path ("/sessions/a.vbo"); the root is not in it
  bool fat = false;      // the SD card: rename() won't replace a file; its changes count as "saved data changed"
  bool counted = false;
};

struct OpenFile {
  Volume* vol = nullptr;
  std::shared_ptr<Node> node;
  std::string path, name;
  size_t pos = 0;
  bool writable = false, append = false;
  std::string lastChild;  // directory listing: the last entry given
  bool listed = false;
};

static double savedSeqValue = 0;
double savedSeq() { return savedSeqValue; }
void bumpSaved() { savedSeqValue += 1; }

Volume& flashVolume() {
  static Volume v;
  return v;
}
Volume& sdVolume() {
  static Volume v = [] {
    Volume x;
    x.fat = true;
    x.counted = true;
    return x;
  }();
  return v;
}

PrefStore& prefStore() {
  static PrefStore s;
  return s;
}

static int64_t nowSeconds() { return (int64_t)time(nullptr); }

static std::string normalize(const char* p) {
  std::string out = "/";
  std::string cur;
  for (const char* c = p ? p : ""; ; c++) {
    if (*c == '/' || *c == 0) {
      if (!cur.empty()) {
        if (out.size() > 1) out += '/';
        out += cur;
        cur.clear();
      }
      if (!*c) break;
    } else {
      cur += *c;
    }
  }
  return out;
}

static std::string baseName(const std::string& p) {
  const size_t s = p.rfind('/');
  return s == std::string::npos ? p : p.substr(s + 1);
}

static std::string parentOf(const std::string& p) {
  const size_t s = p.rfind('/');
  return s == 0 || s == std::string::npos ? "/" : p.substr(0, s);
}

static std::shared_ptr<Node> find(Volume* v, const std::string& path) {
  auto it = v->nodes.find(path);
  return it == v->nodes.end() ? nullptr : it->second;
}

static bool parentIsDir(Volume* v, const std::string& path) {
  const std::string par = parentOf(path);
  if (par == "/") return true;
  auto n = find(v, par);
  return n && n->dir;
}

static void changed(Volume* v) {
  if (v->counted) bumpSaved();
}

}  // namespace sim

using namespace sim;

namespace fs {

size_t File::write(const uint8_t* buf, size_t size) {
  if (!f_ || !f_->writable || !f_->node || f_->node->dir) return 0;
  std::vector<uint8_t>& d = f_->node->data;
  if (f_->append) f_->pos = d.size();
  if (f_->pos + size > d.size()) d.resize(f_->pos + size);
  memcpy(d.data() + f_->pos, buf, size);
  f_->pos += size;
  f_->node->mtime = nowSeconds();
  changed(f_->vol);
  return size;
}

int File::available() {
  if (!f_ || !f_->node || f_->node->dir) return 0;
  const size_t n = f_->node->data.size();
  return f_->pos < n ? (int)(n - f_->pos) : 0;
}

int File::read() {
  if (!f_ || !f_->node || f_->node->dir) return -1;
  const std::vector<uint8_t>& d = f_->node->data;
  return f_->pos < d.size() ? d[f_->pos++] : -1;
}

int File::peek() {
  if (!f_ || !f_->node || f_->node->dir) return -1;
  const std::vector<uint8_t>& d = f_->node->data;
  return f_->pos < d.size() ? d[f_->pos] : -1;
}

size_t File::read(uint8_t* buf, size_t size) {
  if (!f_ || !f_->node || f_->node->dir) return 0;
  const std::vector<uint8_t>& d = f_->node->data;
  if (f_->pos >= d.size()) return 0;
  const size_t n = std::min(size, d.size() - f_->pos);
  memcpy(buf, d.data() + f_->pos, n);
  f_->pos += n;
  return n;
}

bool File::seek(uint32_t pos, SeekMode mode) {
  if (!f_ || !f_->node || f_->node->dir) return false;
  const size_t n = f_->node->data.size();
  const int64_t target = mode == SeekSet ? (int64_t)pos : mode == SeekCur ? (int64_t)f_->pos + (int32_t)pos : (int64_t)n + (int32_t)pos;
  if (target < 0 || (size_t)target > n) return false;
  f_->pos = (size_t)target;
  return true;
}

size_t File::position() const { return f_ ? f_->pos : 0; }
size_t File::size() const { return f_ && f_->node && !f_->node->dir ? f_->node->data.size() : 0; }
void File::close() { f_.reset(); }
time_t File::getLastWrite() { return f_ && f_->node ? (time_t)f_->node->mtime : 0; }
const char* File::path() const { return f_ ? f_->path.c_str() : ""; }
const char* File::name() const { return f_ ? f_->name.c_str() : ""; }
bool File::isDirectory() const { return f_ && f_->node && f_->node->dir; }

void File::rewindDirectory() {
  if (!f_) return;
  f_->lastChild.clear();
  f_->listed = false;
}

File File::openNextFile(const char* mode) {
  if (!f_ || !isDirectory()) return File();
  const std::string prefix = f_->path == "/" ? "/" : f_->path + "/";
  auto it = f_->listed ? f_->vol->nodes.upper_bound(f_->lastChild) : f_->vol->nodes.lower_bound(prefix);
  for (; it != f_->vol->nodes.end(); ++it) {
    const std::string& k = it->first;
    if (k.compare(0, prefix.size(), prefix) != 0) break;
    if (k.find('/', prefix.size()) != std::string::npos) continue;  // deeper down: not an entry of this folder
    f_->lastChild = k;
    f_->listed = true;
    auto of = std::make_shared<OpenFile>();
    of->vol = f_->vol;
    of->node = it->second;
    of->path = k;
    of->name = baseName(k);
    return File(of);
  }
  f_->listed = true;
  f_->lastChild = "\xff";  // past the end
  return File();
}

File FS::open(const char* p, const char* mode, const bool) {
  if (!vol_) return File();
  const std::string path = normalize(p);
  const bool read = !mode || mode[0] == 'r';
  auto of = std::make_shared<OpenFile>();
  of->vol = vol_;
  of->path = path;
  of->name = path == "/" ? "" : baseName(path);
  if (path == "/") {
    of->node = std::make_shared<Node>();
    of->node->dir = true;
    return File(of);
  }
  std::shared_ptr<Node> n = find(vol_, path);
  if (read) {
    if (!n) return File();
    of->node = n;
    return File(of);
  }
  if (n && n->dir) return File();
  if (!n) {
    if (!parentIsDir(vol_, path)) return File();
    n = std::make_shared<Node>();
    vol_->nodes[path] = n;
  } else if (mode[0] == 'w') {
    n->data.clear();
  }
  n->mtime = nowSeconds();
  of->node = n;
  of->writable = true;
  of->append = mode[0] == 'a';
  if (of->append) of->pos = n->data.size();
  changed(vol_);
  return File(of);
}

bool FS::exists(const char* p) {
  if (!vol_) return false;
  const std::string path = normalize(p);
  return path == "/" || find(vol_, path) != nullptr;
}

bool FS::remove(const char* p) {
  if (!vol_) return false;
  const std::string path = normalize(p);
  auto n = find(vol_, path);
  if (!n || n->dir) return false;
  vol_->nodes.erase(path);
  changed(vol_);
  return true;
}

bool FS::rename(const char* f, const char* t) {
  if (!vol_) return false;
  const std::string from = normalize(f), to = normalize(t);
  auto n = find(vol_, from);
  if (!n || from == to) return false;
  if (!parentIsDir(vol_, to)) return false;
  auto old = find(vol_, to);
  if (old && (vol_->fat || old->dir || n->dir)) return false;  // FAT will not rename over a file; nor does a folder
  std::vector<std::pair<std::string, std::shared_ptr<Node>>> moved;
  moved.push_back({to, n});
  if (n->dir) {
    const std::string pre = from + "/";
    for (auto it = vol_->nodes.lower_bound(pre); it != vol_->nodes.end() && it->first.compare(0, pre.size(), pre) == 0; ++it)
      moved.push_back({to + "/" + it->first.substr(pre.size()), it->second});
    for (auto it = vol_->nodes.begin(); it != vol_->nodes.end();)
      it = (it->first.compare(0, pre.size(), pre) == 0) ? vol_->nodes.erase(it) : ++it;
  }
  vol_->nodes.erase(from);
  for (auto& m : moved) vol_->nodes[m.first] = m.second;
  changed(vol_);
  return true;
}

bool FS::mkdir(const char* p) {
  if (!vol_) return false;
  const std::string path = normalize(p);
  if (path == "/" || find(vol_, path) || !parentIsDir(vol_, path)) return false;
  auto n = std::make_shared<Node>();
  n->dir = true;
  n->mtime = nowSeconds();
  vol_->nodes[path] = n;
  changed(vol_);
  return true;
}

bool FS::rmdir(const char* p) {
  if (!vol_) return false;
  const std::string path = normalize(p);
  auto n = find(vol_, path);
  if (!n || !n->dir) return false;
  const std::string pre = path + "/";
  auto it = vol_->nodes.lower_bound(pre);
  if (it != vol_->nodes.end() && it->first.compare(0, pre.size(), pre) == 0) return false;  // not empty
  vol_->nodes.erase(path);
  changed(vol_);
  return true;
}

uint64_t FS::usedBytes() {
  uint64_t n = 0;
  if (vol_)
    for (auto& kv : vol_->nodes) n += kv.second->data.size();
  return n;
}

LittleFSFS::LittleFSFS() : FS(&flashVolume()) {}
SDMMCFS::SDMMCFS() : FS(&sdVolume()) {}

}  // namespace fs

fs::LittleFSFS LittleFS;
fs::SDMMCFS SD_MMC;

// ---------------------------------------------------------------------------
// Everything saved, as one blob: the settings, then the SD card's files.

namespace sim {

static void put32(std::vector<uint8_t>& o, uint32_t v) {
  for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i)));
}
static void put64(std::vector<uint8_t>& o, uint64_t v) {
  for (int i = 0; i < 8; i++) o.push_back((uint8_t)(v >> (8 * i)));
}
static void putBytes(std::vector<uint8_t>& o, const void* p, size_t n) {
  put32(o, (uint32_t)n);
  o.insert(o.end(), (const uint8_t*)p, (const uint8_t*)p + n);
}

std::vector<uint8_t> exportSaved() {
  std::vector<uint8_t> o;
  o.insert(o.end(), {'T', 'R', 'S', '1'});
  uint32_t entries = 0;
  for (auto& ns : prefStore()) entries += (uint32_t)ns.second.size();
  put32(o, entries);
  for (auto& ns : prefStore())
    for (auto& kv : ns.second) {
      putBytes(o, ns.first.data(), ns.first.size());
      putBytes(o, kv.first.data(), kv.first.size());
      putBytes(o, kv.second.data(), kv.second.size());
    }
  put32(o, (uint32_t)sdVolume().nodes.size());
  for (auto& kv : sdVolume().nodes) {
    putBytes(o, kv.first.data(), kv.first.size());
    o.push_back(kv.second->dir ? 1 : 0);
    put64(o, (uint64_t)kv.second->mtime);
    putBytes(o, kv.second->data.data(), kv.second->data.size());
  }
  return o;
}

namespace {
struct Reader {
  const uint8_t* p;
  size_t n, at = 0;
  bool ok = true;
  uint32_t u32() {
    if (at + 4 > n) return ok = false, 0;
    uint32_t v = p[at] | p[at + 1] << 8 | p[at + 2] << 16 | (uint32_t)p[at + 3] << 24;
    at += 4;
    return v;
  }
  uint64_t u64() {
    const uint64_t lo = u32();
    return lo | (uint64_t)u32() << 32;
  }
  uint8_t u8() { return at < n ? p[at++] : (ok = false, 0); }
  std::vector<uint8_t> bytes() {
    const uint32_t len = u32();
    if (!ok || at + len > n) return ok = false, std::vector<uint8_t>();
    std::vector<uint8_t> v(p + at, p + at + len);
    at += len;
    return v;
  }
};
}  // namespace

bool importSaved(const uint8_t* data, size_t len) {
  if (len < 8 || memcmp(data, "TRS1", 4) != 0) return false;
  Reader r{data + 4, len - 4};
  PrefStore prefs;
  std::map<std::string, std::shared_ptr<Node>> files;
  const uint32_t np = r.u32();
  for (uint32_t i = 0; i < np && r.ok; i++) {
    auto ns = r.bytes(), key = r.bytes(), val = r.bytes();
    if (r.ok) prefs[std::string(ns.begin(), ns.end())][std::string(key.begin(), key.end())] = val;
  }
  const uint32_t nf = r.u32();
  for (uint32_t i = 0; i < nf && r.ok; i++) {
    auto path = r.bytes();
    const bool dir = r.u8() != 0;
    const int64_t mtime = (int64_t)r.u64();
    auto body = r.bytes();
    if (!r.ok) break;
    auto n = std::make_shared<Node>();
    n->dir = dir;
    n->mtime = mtime;
    n->data = std::move(body);
    files[std::string(path.begin(), path.end())] = n;
  }
  if (!r.ok) return false;
  prefStore() = std::move(prefs);
  sdVolume().nodes = std::move(files);
  bumpSaved();
  return true;
}

void putFlashFile(const char* path, const uint8_t* data, size_t len) {
  auto n = std::make_shared<Node>();
  n->data.assign(data, data + len);
  n->mtime = nowSeconds();
  flashVolume().nodes[normalize(path)] = n;
}

}  // namespace sim
