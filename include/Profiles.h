/**
 * Track Rat - drivers and cars (8.5)
 *
 * Up to 8 drivers and 8 cars. Each has a fixed id (1 to 255, never used
 * twice: a counter only goes up) and a name; a car can also have a VIN. One
 * driver and one car are current. Best laps are kept per driver and car:
 * the first driver with the first car use the track folders as they always
 * were (/tracks/<id>/), every other pair has a folder of its own inside it
 * (/tracks/<id>/d3-c2/), so PBs from before drivers and cars stay put.
 *
 * Pure C++ (no Arduino), so it is tested on the PC in test/test_profiles.cpp.
 * Profiles.cpp keeps it in flash and ProfileStore.h is what the rest of the
 * firmware calls. (These cars are not the OBDb "car profiles" on the SD card.)
 */

#ifndef PROFILES_H
#define PROFILES_H

#include <initializer_list>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace profiles {

constexpr int MAX_PER_LIST = 8;
constexpr int NAME_MAX = 20;  // bytes, after trimming
constexpr int VIN_LEN = 17;
constexpr int MAX_ID = 255;

enum class Kind : uint8_t { Driver, Car };
enum class Err : uint8_t { Ok, Full, BadName, Duplicate, NotFound, Last, BadVin };

inline const char* errText(Err e) {
  switch (e) {
    case Err::Ok: return "";
    case Err::Full: return "The list is full (8)";
    case Err::BadName: return "Give it a name of 1 to 20 characters";
    case Err::Duplicate: return "That name or VIN is already used";
    case Err::NotFound: return "No such driver or car";
    case Err::Last: return "Keep at least one";
    default: return "A VIN is 17 letters and digits (no I, O or Q)";
  }
}

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
inline char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
inline bool sameText(const char* a, const char* b) {  // equal, ignoring ASCII case
  for (; *a && *b; a++, b++)
    if (upper(*a) != upper(*b)) return false;
  return *a == *b;
}

// A name as it is kept: trimmed, 1 to NAME_MAX bytes, no control characters. False if it can't be.
inline bool cleanName(const char* in, char* out) {
  out[0] = 0;
  if (!in) return false;
  while (isSpace(*in)) in++;
  size_t n = strlen(in);
  while (n > 0 && isSpace(in[n - 1])) n--;
  if (n == 0 || n > (size_t)NAME_MAX) return false;
  for (size_t i = 0; i < n; i++)
    if ((uint8_t)in[i] < 0x20 || in[i] == 0x7F) return false;
  memcpy(out, in, n);
  out[n] = 0;
  return true;
}

// A VIN as it is kept: 17 capitals and digits (I, O and Q are never used), or empty. False if it can't be.
inline bool cleanVin(const char* in, char* out) {
  out[0] = 0;
  if (!in) return true;
  while (isSpace(*in)) in++;
  size_t n = strlen(in);
  while (n > 0 && isSpace(in[n - 1])) n--;
  if (n == 0) return true;
  if (n != (size_t)VIN_LEN) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = upper(in[i]);
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z' && c != 'I' && c != 'O' && c != 'Q');
    if (!ok) return false;
    out[i] = c;
  }
  out[n] = 0;
  return true;
}

struct Entry {
  uint8_t id = 0;  // 0: not used
  char name[NAME_MAX + 1] = "";
  char vin[VIN_LEN + 1] = "";  // cars only
};

struct List {
  Entry items[MAX_PER_LIST];
  int count = 0;
  int nextId = 1;  // the id the next one added gets; only goes up
  int current = 0; // an id

  int indexOf(int id) const {
    for (int i = 0; i < count; i++)
      if (items[i].id == id) return i;
    return -1;
  }
  const Entry* find(int id) const {
    const int i = indexOf(id);
    return i < 0 ? nullptr : &items[i];
  }
  const Entry& currentEntry() const { return items[indexOf(current) < 0 ? 0 : indexOf(current)]; }
  bool nameUsed(const char* name, int exceptId) const {
    for (int i = 0; i < count; i++)
      if (items[i].id != exceptId && sameText(items[i].name, name)) return true;
    return false;
  }
};

struct Book {
  List drivers, cars;

  Book() { reset(); }

  // The two that every unit starts with
  void reset() {
    resetList(Kind::Driver);
    resetList(Kind::Car);
  }
  void resetList(Kind k) {
    List& l = list(k);
    l = List();
    l.count = 1;
    l.items[0].id = 1;
    snprintf(l.items[0].name, sizeof(l.items[0].name), "%s 1", k == Kind::Driver ? "Driver" : "Car");
    l.nextId = 2;
    l.current = 1;
  }

  List& list(Kind k) { return k == Kind::Driver ? drivers : cars; }
  const List& list(Kind k) const { return k == Kind::Driver ? drivers : cars; }

  int driverId() const { return drivers.currentEntry().id; }
  int carId() const { return cars.currentEntry().id; }
  const char* driverName() const { return drivers.currentEntry().name; }
  const char* carName() const { return cars.currentEntry().name; }

  // Adds one at the end. newId (if given) is the id it got.
  Err add(Kind k, const char* name, int* newId = nullptr) {
    List& l = list(k);
    char n[NAME_MAX + 1];
    if (!cleanName(name, n)) return Err::BadName;
    if (l.count >= MAX_PER_LIST || l.nextId > MAX_ID) return Err::Full;
    if (l.nameUsed(n, 0)) return Err::Duplicate;
    Entry& e = l.items[l.count++];
    e = Entry();
    e.id = (uint8_t)l.nextId++;
    memcpy(e.name, n, strlen(n) + 1);
    if (newId) *newId = e.id;
    return Err::Ok;
  }

  Err rename(Kind k, int id, const char* name) {
    List& l = list(k);
    const int i = l.indexOf(id);
    if (i < 0) return Err::NotFound;
    char n[NAME_MAX + 1];
    if (!cleanName(name, n)) return Err::BadName;
    if (l.nameUsed(n, id)) return Err::Duplicate;
    memcpy(l.items[i].name, n, strlen(n) + 1);
    return Err::Ok;
  }

  // Never the last one. Removing the current one makes the first in the list the current.
  Err remove(Kind k, int id) {
    List& l = list(k);
    const int i = l.indexOf(id);
    if (i < 0) return Err::NotFound;
    if (l.count <= 1) return Err::Last;
    for (int j = i; j + 1 < l.count; j++) l.items[j] = l.items[j + 1];
    l.items[--l.count] = Entry();
    if (l.current == id) l.current = l.items[0].id;
    return Err::Ok;
  }

  Err select(Kind k, int id) {
    List& l = list(k);
    if (l.indexOf(id) < 0) return Err::NotFound;
    l.current = id;
    return Err::Ok;
  }

  // A car's VIN: 17 characters, or "" to clear it. Two cars can't share one.
  Err setVin(int carId, const char* vin) {
    const int i = cars.indexOf(carId);
    if (i < 0) return Err::NotFound;
    char v[VIN_LEN + 1];
    if (!cleanVin(vin, v)) return Err::BadVin;
    if (v[0])
      for (int j = 0; j < cars.count; j++)
        if (j != i && sameText(cars.items[j].vin, v)) return Err::Duplicate;
    memcpy(cars.items[i].vin, v, strlen(v) + 1);
    return Err::Ok;
  }

  // The car with this VIN (as the adapter reported it), or nullptr
  const Entry* carByVin(const char* vin) const {
    char v[VIN_LEN + 1];
    if (!vin || !cleanVin(vin, v) || !v[0]) return nullptr;
    for (int i = 0; i < cars.count; i++)
      if (sameText(cars.items[i].vin, v)) return &cars.items[i];
    return nullptr;
  }

  // "Dylan · Civic" (a middle dot): the screens' small line
  void label(char* out, size_t n) const { snprintf(out, n, "%s \xC2\xB7 %s", driverName(), carName()); }
  // Worth showing only once there is a choice
  bool showLabel() const { return drivers.count > 1 || cars.count > 1; }

  // After loading: drops what can't be right (a bad or repeated id or name), and makes sure there is a
  // driver and a car, that the counters are past every id in use, and that the current ones exist.
  void sanitize() {
    for (Kind k : {Kind::Driver, Kind::Car}) {
      List& l = list(k);
      Entry kept[MAX_PER_LIST];
      int n = 0;
      for (int i = 0; i < l.count && i < MAX_PER_LIST; i++) {
        Entry e = l.items[i];
        e.name[NAME_MAX] = 0;
        e.vin[VIN_LEN] = 0;
        char name[NAME_MAX + 1], vin[VIN_LEN + 1] = "";
        if (e.id < 1 || !cleanName(e.name, name)) continue;
        if (k == Kind::Driver || !cleanVin(e.vin, vin)) vin[0] = 0;
        bool clash = false;
        for (int j = 0; j < n; j++)
          if (kept[j].id == e.id || sameText(kept[j].name, name) || (vin[0] && sameText(kept[j].vin, vin))) clash = true;
        if (clash) continue;
        memcpy(e.name, name, strlen(name) + 1);
        memcpy(e.vin, vin, strlen(vin) + 1);
        kept[n++] = e;
      }
      if (n == 0) {
        const int next = l.nextId;
        resetList(k);
        if (next > l.nextId) l.nextId = next > MAX_ID + 1 ? MAX_ID + 1 : next;
        continue;
      }
      int maxId = 0;
      for (int i = 0; i < n; i++) {
        l.items[i] = kept[i];
        if (kept[i].id > maxId) maxId = kept[i].id;
      }
      for (int i = n; i < MAX_PER_LIST; i++) l.items[i] = Entry();
      l.count = n;
      if (l.nextId <= maxId) l.nextId = maxId + 1;
      if (l.nextId > MAX_ID + 1) l.nextId = MAX_ID + 1;
      if (l.indexOf(l.current) < 0) l.current = l.items[0].id;
    }
  }

  bool operator==(const Book& o) const {
    for (Kind k : {Kind::Driver, Kind::Car}) {
      const List &a = list(k), &b = o.list(k);
      if (a.count != b.count || a.nextId != b.nextId || a.current != b.current) return false;
      for (int i = 0; i < a.count; i++)
        if (a.items[i].id != b.items[i].id || strcmp(a.items[i].name, b.items[i].name) || strcmp(a.items[i].vin, b.items[i].vin)) return false;
    }
    return true;
  }
  bool operator!=(const Book& o) const { return !(*this == o); }
};

// The folder name for this pair's best laps, inside the track's folder: "" for driver 1 with car 1
// (the files as they were before drivers and cars), else "d3-c2".
inline void folderSuffix(int driverId, int carId, char* out, size_t n) {
  if (driverId == 1 && carId == 1) out[0] = 0;
  else snprintf(out, n, "d%d-c%d", driverId, carId);
}

}  // namespace profiles

#endif  // PROFILES_H
