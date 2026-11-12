// Stand-in for the ESP32 Preferences library (tools/build_sim.py): saved settings kept in memory.
// The store is exported with the SD card's files (tools/sim/sim.cpp), so a reload keeps them.
#pragma once
#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

namespace sim {
typedef std::map<std::string, std::vector<uint8_t>> PrefNs;
typedef std::map<std::string, PrefNs> PrefStore;
PrefStore& prefStore();
void bumpSaved();  // something saved changed
}  // namespace sim

class Preferences {
public:
  bool begin(const char* name, bool readOnly = false) {
    ns_ = name;
    ro_ = readOnly;
    open_ = true;
    return true;
  }
  void end() { open_ = false; }
  bool clear() {
    if (ro_) return false;
    sim::prefStore()[ns_].clear();
    sim::bumpSaved();
    return true;
  }
  bool remove(const char* key) {
    if (ro_) return false;
    const bool had = sim::prefStore()[ns_].erase(key) > 0;
    if (had) sim::bumpSaved();
    return had;
  }
  bool isKey(const char* key) { return find(key) != nullptr; }

  size_t putBytes(const char* key, const void* v, size_t n) {
    if (ro_) return 0;
    std::vector<uint8_t>& slot = sim::prefStore()[ns_][key];
    const uint8_t* p = (const uint8_t*)v;
    if (slot.size() != n || memcmp(slot.data(), p, n) != 0) {
      slot.assign(p, p + n);
      sim::bumpSaved();
    }
    return n;
  }
  size_t getBytesLength(const char* key) {
    const std::vector<uint8_t>* s = find(key);
    return s ? s->size() : 0;
  }
  size_t getBytes(const char* key, void* buf, size_t max) {
    const std::vector<uint8_t>* s = find(key);
    if (!s) return 0;
    const size_t n = s->size() < max ? s->size() : max;
    memcpy(buf, s->data(), n);
    return n;
  }

  size_t putBool(const char* k, bool v) { return putT<uint8_t>(k, v ? 1 : 0); }
  size_t putUChar(const char* k, uint8_t v) { return putT<uint8_t>(k, v); }
  size_t putChar(const char* k, int8_t v) { return putT<int8_t>(k, v); }
  size_t putShort(const char* k, int16_t v) { return putT<int16_t>(k, v); }
  size_t putUShort(const char* k, uint16_t v) { return putT<uint16_t>(k, v); }
  size_t putInt(const char* k, int32_t v) { return putT<int32_t>(k, v); }
  size_t putUInt(const char* k, uint32_t v) { return putT<uint32_t>(k, v); }
  size_t putLong(const char* k, int32_t v) { return putT<int32_t>(k, v); }
  size_t putULong(const char* k, uint32_t v) { return putT<uint32_t>(k, v); }
  size_t putLong64(const char* k, int64_t v) { return putT<int64_t>(k, v); }
  size_t putULong64(const char* k, uint64_t v) { return putT<uint64_t>(k, v); }
  size_t putFloat(const char* k, float v) { return putT<float>(k, v); }
  size_t putDouble(const char* k, double v) { return putT<double>(k, v); }
  size_t putString(const char* k, const char* v) { return putBytes(k, v, strlen(v) + 1) ? strlen(v) : 0; }
  size_t putString(const char* k, const String& v) { return putString(k, v.c_str()); }

  bool getBool(const char* k, bool d = false) { return getT<uint8_t>(k, d ? 1 : 0) != 0; }
  uint8_t getUChar(const char* k, uint8_t d = 0) { return getT<uint8_t>(k, d); }
  int8_t getChar(const char* k, int8_t d = 0) { return getT<int8_t>(k, d); }
  int16_t getShort(const char* k, int16_t d = 0) { return getT<int16_t>(k, d); }
  uint16_t getUShort(const char* k, uint16_t d = 0) { return getT<uint16_t>(k, d); }
  int32_t getInt(const char* k, int32_t d = 0) { return getT<int32_t>(k, d); }
  uint32_t getUInt(const char* k, uint32_t d = 0) { return getT<uint32_t>(k, d); }
  int32_t getLong(const char* k, int32_t d = 0) { return getT<int32_t>(k, d); }
  uint32_t getULong(const char* k, uint32_t d = 0) { return getT<uint32_t>(k, d); }
  int64_t getLong64(const char* k, int64_t d = 0) { return getT<int64_t>(k, d); }
  uint64_t getULong64(const char* k, uint64_t d = 0) { return getT<uint64_t>(k, d); }
  float getFloat(const char* k, float d = 0) { return getT<float>(k, d); }
  double getDouble(const char* k, double d = 0) { return getT<double>(k, d); }
  String getString(const char* k, String d = String()) {
    const std::vector<uint8_t>* s = find(k);
    if (!s || s->empty()) return d;
    return String((const char*)s->data());
  }
  size_t getString(const char* k, char* out, size_t max) {
    const std::vector<uint8_t>* s = find(k);
    if (!s || !max) return 0;
    const size_t n = s->size() < max ? s->size() : max;
    memcpy(out, s->data(), n);
    out[max - 1] = 0;
    return n;
  }

private:
  std::string ns_;
  bool ro_ = false, open_ = false;
  const std::vector<uint8_t>* find(const char* key) {
    sim::PrefStore& st = sim::prefStore();
    auto n = st.find(ns_);
    if (n == st.end()) return nullptr;
    auto k = n->second.find(key);
    return k == n->second.end() ? nullptr : &k->second;
  }
  template <class T>
  size_t putT(const char* k, T v) {
    return putBytes(k, &v, sizeof(T));
  }
  template <class T>
  T getT(const char* k, T d) {
    const std::vector<uint8_t>* s = find(k);
    if (!s || s->size() != sizeof(T)) return d;
    T v;
    memcpy(&v, s->data(), sizeof(T));
    return v;
  }
};
