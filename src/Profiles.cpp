/**
 * Track Rat - drivers and cars (implementation)
 *
 * Kept as one JSON text under one NVS key, e.g.
 *   {"d":[[1,"Driver 1"],[2,"Sam"]],"dn":3,"dc":2,"c":[[1,"Car 1","VIN"]],"cn":2,"cc":1}
 * (drivers as [id, name]; cars as [id, name, VIN]; "dn"/"cn" the next id,
 * "dc"/"cc" the current one). A unit that never stored it has the defaults.
 */

#include "ProfileStore.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include "Config.h"

namespace Profiles {

using profiles::Book;
using profiles::Err;
using profiles::Kind;

static Book current;
static volatile uint32_t changes = 0;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;  // everything below that is shared
static SemaphoreHandle_t writeLock = nullptr;             // one edit at a time (loop or web server)
static Preferences prefs;
static bool open = false;
static bool locked = false;
static int lockedDriver = 1, lockedCar = 1;
static char seenVin[profiles::VIN_LEN + 1] = "";
static bool vinPending = false;

static const char* KEY = "book";

static String toStoredJson(const Book& b) {
  DynamicJsonDocument doc(1536);
  doc["dn"] = b.drivers.nextId;
  doc["dc"] = b.drivers.current;
  doc["cn"] = b.cars.nextId;
  doc["cc"] = b.cars.current;
  JsonArray d = doc.createNestedArray("d");
  for (int i = 0; i < b.drivers.count; i++) {
    JsonArray e = d.createNestedArray();
    e.add(b.drivers.items[i].id);
    e.add(b.drivers.items[i].name);
  }
  JsonArray c = doc.createNestedArray("c");
  for (int i = 0; i < b.cars.count; i++) {
    JsonArray e = c.createNestedArray();
    e.add(b.cars.items[i].id);
    e.add(b.cars.items[i].name);
    e.add(b.cars.items[i].vin);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

static void fillList(JsonArray a, profiles::List& l, bool cars) {
  l.count = 0;
  for (JsonArray e : a) {
    if (l.count >= profiles::MAX_PER_LIST) break;
    profiles::Entry& it = l.items[l.count++];
    it = profiles::Entry();
    it.id = (uint8_t)(e[0] | 0);
    snprintf(it.name, sizeof(it.name), "%s", e[1] | "");
    if (cars) snprintf(it.vin, sizeof(it.vin), "%s", e[2] | "");
  }
}

static Book fromStoredJson(const String& text) {
  Book b;
  DynamicJsonDocument doc(1536);
  if (deserializeJson(doc, text)) return b;
  fillList(doc["d"].as<JsonArray>(), b.drivers, false);
  fillList(doc["c"].as<JsonArray>(), b.cars, true);
  b.drivers.nextId = doc["dn"] | 1;
  b.drivers.current = doc["dc"] | 0;
  b.cars.nextId = doc["cn"] | 1;
  b.cars.current = doc["cc"] | 0;
  b.sanitize();
  return b;
}

void begin() {
  writeLock = xSemaphoreCreateMutex();
  open = prefs.begin("profiles", false);
  Book b;
  if (open) {
    const String text = prefs.getString(KEY, "");
    if (text.length()) b = fromStoredJson(text);
  } else {
    DEBUG_PRINTLN("Profiles: flash storage unavailable - using the defaults, changes won't be kept");
  }
  portENTER_CRITICAL(&lock);
  current = b;
  portEXIT_CRITICAL(&lock);
  DEBUG_PRINTF("Profiles: %d driver(s), %d car(s); %s / %s\n", b.drivers.count, b.cars.count, b.driverName(), b.carName());
}

Book get() {
  portENTER_CRITICAL(&lock);
  Book b = current;
  portEXIT_CRITICAL(&lock);
  return b;
}

uint32_t changeCount() { return changes; }

// Applies an edit to a copy; if it worked and changed something, keeps and saves it
template <typename F>
static Err edit(F change) {
  if (writeLock) xSemaphoreTake(writeLock, portMAX_DELAY);
  Book n = get();
  const Book o = n;
  const Err e = change(n);
  if (e == Err::Ok && n != o) {
    portENTER_CRITICAL(&lock);
    current = n;
    changes = changes + 1;
    portEXIT_CRITICAL(&lock);
    if (open) prefs.putString(KEY, toStoredJson(n));  // flash write outside the spinlock: it takes milliseconds
  }
  if (writeLock) xSemaphoreGive(writeLock);
  return e;
}

Err add(Kind k, const char* name, int* newId) {
  return edit([&](Book& b) { return b.add(k, name, newId); });
}
Err rename(Kind k, int id, const char* name) {
  return edit([&](Book& b) { return b.rename(k, id, name); });
}
Err remove(Kind k, int id) {
  return edit([&](Book& b) { return b.remove(k, id); });
}
Err select(Kind k, int id) {
  return edit([&](Book& b) { return b.select(k, id); });
}
Err setVin(int carId, const char* vin) {
  return edit([&](Book& b) { return b.setVin(carId, vin); });
}

bool showLabel() { return get().showLabel(); }
void label(char* out, size_t n) { get().label(out, n); }

void lockSession() {
  portENTER_CRITICAL(&lock);
  if (!locked) {
    locked = true;
    lockedDriver = current.driverId();
    lockedCar = current.carId();
  }
  portEXIT_CRITICAL(&lock);
}
void unlockSession() {
  portENTER_CRITICAL(&lock);
  locked = false;
  portEXIT_CRITICAL(&lock);
}
bool sessionLocked() { return locked; }

Active active() {
  Active a;
  const Book b = get();
  portENTER_CRITICAL(&lock);
  const bool l = locked;
  const int ld = lockedDriver, lc = lockedCar;
  portEXIT_CRITICAL(&lock);
  const profiles::Entry* d = l ? b.drivers.find(ld) : nullptr;
  const profiles::Entry* c = l ? b.cars.find(lc) : nullptr;
  if (!d) d = &b.drivers.currentEntry();
  if (!c) c = &b.cars.currentEntry();
  a.driverId = d->id;
  a.carId = c->id;
  snprintf(a.driver, sizeof(a.driver), "%s", d->name);
  snprintf(a.car, sizeof(a.car), "%s", c->name);
  return a;
}

String pbFolder() {
  const Active a = active();
  char s[16];
  profiles::folderSuffix(a.driverId, a.carId, s, sizeof(s));
  return String(s);
}

void noteVin(const char* vin) {
  char v[profiles::VIN_LEN + 1];
  if (!profiles::cleanVin(vin, v) || !v[0]) return;  // not given, or not a VIN
  portENTER_CRITICAL(&lock);
  memcpy(seenVin, v, sizeof(seenVin));
  vinPending = true;
  portEXIT_CRITICAL(&lock);
}

void vinSeen(char* out, size_t n) {
  portENTER_CRITICAL(&lock);
  snprintf(out, n, "%s", seenVin);
  portEXIT_CRITICAL(&lock);
}

bool applyVin() {
  if (!vinPending || locked) return false;  // nothing new, or wait for the session to end
  char v[profiles::VIN_LEN + 1];
  portENTER_CRITICAL(&lock);
  memcpy(v, seenVin, sizeof(v));
  vinPending = false;
  portEXIT_CRITICAL(&lock);
  const Book b = get();
  const profiles::Entry* c = b.carByVin(v);
  if (!c || c->id == b.carId()) return false;
  const int id = c->id;
  const bool ok = select(Kind::Car, id) == Err::Ok;
  if (ok) DEBUG_PRINTF("Profiles: the adapter's car matches car %d - now the current car\n", id);
  return ok;
}

String apiJson() {
  const Book b = get();
  DynamicJsonDocument doc(1536);
  JsonArray d = doc.createNestedArray("drivers");
  for (int i = 0; i < b.drivers.count; i++) {
    JsonObject o = d.createNestedObject();
    o["id"] = b.drivers.items[i].id;
    o["name"] = b.drivers.items[i].name;
  }
  JsonArray c = doc.createNestedArray("cars");
  for (int i = 0; i < b.cars.count; i++) {
    JsonObject o = c.createNestedObject();
    o["id"] = b.cars.items[i].id;
    o["name"] = b.cars.items[i].name;
    o["vin"] = b.cars.items[i].vin;
  }
  doc["driver"] = b.driverId();
  doc["car"] = b.carId();
  char v[profiles::VIN_LEN + 1];
  vinSeen(v, sizeof(v));
  doc["vinSeen"] = v;
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace Profiles
