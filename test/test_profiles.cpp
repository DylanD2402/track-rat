/**
 * Host-side tests for drivers and cars (include/Profiles.h): the defaults,
 * names (trimmed, unique, 20 bytes), ids that are never used twice, the limit
 * of 8, removing (never the last, the current one hands over), choosing, a
 * car's VIN and finding a car by it, the label, and the folder a pair's best
 * laps are kept in (empty for driver 1 with car 1, so existing PBs stay).
 *
 *   g++ -std=c++17 -Iinclude test/test_profiles.cpp -o /tmp/pr && /tmp/pr
 */

#include <cstdio>
#include "Profiles.h"
#include "check.h"

using namespace profiles;

static const char* VIN_A = "1HGCM82633A004352";
static const char* VIN_B = "WBAUP7C58AVK12345";

static void testDefaults() {
  Book b;
  CHECK(b.drivers.count == 1 && b.cars.count == 1, "one of each");
  CHECK(b.driverId() == 1 && b.carId() == 1, "ids 1 and 1");
  CHECK(!strcmp(b.driverName(), "Driver 1") && !strcmp(b.carName(), "Car 1"), "names '%s' '%s'", b.driverName(), b.carName());
  CHECK(!b.showLabel(), "no label with one of each");
  CHECK(b.drivers.nextId == 2 && b.cars.nextId == 2, "counters start at 2");
  char buf[64];
  b.label(buf, sizeof(buf));
  CHECK(!strcmp(buf, "Driver 1 \xC2\xB7 Car 1"), "label '%s'", buf);
  printf("  defaults: Driver 1 and Car 1, no label\n");
}

static void testNames() {
  Book b;
  int id = 0;
  CHECK(b.add(Kind::Driver, "  Dylan  ", &id) == Err::Ok && id == 2, "trimmed name added with id 2 (%d)", id);
  CHECK(!strcmp(b.drivers.find(2)->name, "Dylan"), "stored trimmed: '%s'", b.drivers.find(2)->name);
  CHECK(b.add(Kind::Driver, "") == Err::BadName, "empty");
  CHECK(b.add(Kind::Driver, "   ") == Err::BadName, "blank");
  CHECK(b.add(Kind::Driver, nullptr) == Err::BadName, "null");
  CHECK(b.add(Kind::Driver, "dylan") == Err::Duplicate, "same name, other case");
  CHECK(b.add(Kind::Driver, "Driver 1") == Err::Duplicate, "the default's name");
  CHECK(b.add(Kind::Driver, "12345678901234567890") == Err::Ok, "20 bytes is fine");
  CHECK(b.add(Kind::Driver, "123456789012345678901") == Err::BadName, "21 bytes is too long");
  CHECK(b.add(Kind::Driver, "Bad\tname") == Err::BadName, "control character");
  CHECK(b.add(Kind::Car, "Dylan") == Err::Ok, "the same name in the other list is fine");
  CHECK(b.rename(Kind::Driver, 2, "Sam") == Err::Ok && !strcmp(b.drivers.find(2)->name, "Sam"), "rename");
  CHECK(b.rename(Kind::Driver, 2, "SAM") == Err::Ok, "rename to its own name in other case");
  CHECK(b.rename(Kind::Driver, 2, "Driver 1") == Err::Duplicate, "rename onto another");
  CHECK(b.rename(Kind::Driver, 2, " ") == Err::BadName, "rename to blank");
  CHECK(b.rename(Kind::Driver, 99, "X") == Err::NotFound, "rename unknown id");
  CHECK(!strcmp(b.drivers.find(2)->name, "SAM"), "failed renames changed nothing");
  printf("  names: trimmed, 1-20 bytes, unique per list\n");
}

static void testIdsAndLimit() {
  Book b;
  int id = 0;
  for (int i = 0; i < 7; i++) {
    char n[16];
    snprintf(n, sizeof(n), "D%d", i);
    CHECK(b.add(Kind::Driver, n, &id) == Err::Ok && id == i + 2, "id %d for D%d", id, i);
  }
  CHECK(b.drivers.count == 8, "eight drivers");
  CHECK(b.add(Kind::Driver, "Ninth") == Err::Full, "the ninth is refused");
  // Ids are never reused: remove one, the next added gets a new id
  CHECK(b.remove(Kind::Driver, 5) == Err::Ok && b.drivers.count == 7, "removed id 5");
  CHECK(b.add(Kind::Driver, "Again", &id) == Err::Ok && id == 9, "next id is 9, not 5 (%d)", id);
  CHECK(b.drivers.find(5) == nullptr, "id 5 is gone for good");
  CHECK(b.cars.nextId == 2, "the car counter is separate");
  // The counter stops at 255
  Book c;
  c.drivers.nextId = 255;
  CHECK(c.add(Kind::Driver, "Last id", &id) == Err::Ok && id == 255, "id 255 is allowed");
  CHECK(c.remove(Kind::Driver, 255) == Err::Ok, "remove it");
  CHECK(c.add(Kind::Driver, "No ids left") == Err::Full, "no id past 255 is handed out");
  printf("  ids: 2, 3, 4... never reused, 8 at most, stop at 255\n");
}

static void testRemoveSelect() {
  Book b;
  int d2 = 0, d3 = 0;
  b.add(Kind::Driver, "Two", &d2);
  b.add(Kind::Driver, "Three", &d3);
  CHECK(b.select(Kind::Driver, d3) == Err::Ok && b.driverId() == d3 && !strcmp(b.driverName(), "Three"), "select");
  CHECK(b.select(Kind::Driver, 77) == Err::NotFound && b.driverId() == d3, "unknown id: nothing changes");
  CHECK(b.remove(Kind::Driver, d2) == Err::Ok && b.driverId() == d3, "removing another keeps the current");
  CHECK(b.remove(Kind::Driver, d3) == Err::Ok && b.driverId() == 1, "removing the current makes the first current (%d)", b.driverId());
  CHECK(b.remove(Kind::Driver, 1) == Err::Last, "never the last one");
  CHECK(b.drivers.count == 1, "still one");
  CHECK(b.remove(Kind::Driver, 42) == Err::NotFound, "unknown id");
  // Removing the first leaves the next first as the current
  int d4 = 0;
  b.add(Kind::Driver, "Four", &d4);
  CHECK(b.remove(Kind::Driver, 1) == Err::Ok && b.driverId() == d4, "the first one went: the next is current");
  // Cars the same way
  int c2 = 0;
  b.add(Kind::Car, "Kart", &c2);
  b.select(Kind::Car, c2);
  CHECK(b.carId() == c2 && !strcmp(b.carName(), "Kart"), "car selected");
  CHECK(b.remove(Kind::Car, c2) == Err::Ok && b.carId() == 1, "car removed, first car current");
  printf("  remove and select: the last stays, the current hands over\n");
}

static void testVin() {
  Book b;
  int c2 = 0;
  b.add(Kind::Car, "Civic", &c2);
  CHECK(b.setVin(1, VIN_A) == Err::Ok && !strcmp(b.cars.find(1)->vin, VIN_A), "VIN set");
  CHECK(b.setVin(c2, "wbaup7c58avk12345") == Err::Ok && !strcmp(b.cars.find(c2)->vin, VIN_B), "lower case is made capitals");
  CHECK(b.setVin(c2, VIN_A) == Err::Duplicate, "two cars can't share a VIN");
  CHECK(b.setVin(c2, "SHORT") == Err::BadVin, "too short");
  CHECK(b.setVin(c2, "1HGCM82633A00435O") == Err::BadVin, "an O is not in a VIN");
  CHECK(b.setVin(c2, "1HGCM82633A00435!") == Err::BadVin, "punctuation");
  CHECK(b.setVin(9, VIN_A) == Err::NotFound, "unknown car");
  CHECK(!strcmp(b.cars.find(c2)->vin, VIN_B), "a refused VIN changed nothing");
  const Entry* e = b.carByVin(VIN_B);
  CHECK(e && e->id == c2, "found by VIN");
  e = b.carByVin("1hgcm82633a004352");
  CHECK(e && e->id == 1, "found ignoring case");
  CHECK(b.carByVin("JH4KA8260MC000000") == nullptr, "an unknown VIN finds none");
  CHECK(b.carByVin("") == nullptr && b.carByVin(nullptr) == nullptr, "no VIN finds none (even with a car with none)");
  CHECK(b.setVin(1, "") == Err::Ok && b.cars.find(1)->vin[0] == 0, "cleared");
  CHECK(b.carByVin(VIN_A) == nullptr, "no longer found");
  printf("  VIN: 17 letters and digits, one car each, found by VIN\n");
}

static void testLabel() {
  Book b;
  char buf[64];
  CHECK(!b.showLabel(), "one of each: no label");
  int id = 0;
  b.add(Kind::Car, "Civic", &id);
  CHECK(b.showLabel(), "two cars: label");
  b.select(Kind::Car, id);
  b.label(buf, sizeof(buf));
  CHECK(!strcmp(buf, "Driver 1 \xC2\xB7 Civic"), "label '%s'", buf);
  b.remove(Kind::Car, id);
  CHECK(!b.showLabel(), "back to one: no label");
  b.add(Kind::Driver, "Sam", &id);
  CHECK(b.showLabel(), "two drivers: label");
  b.label(buf, 8);
  CHECK(strlen(buf) == 7, "label fits a small buffer");
  printf("  label: shown once there is a choice\n");
}

static void testFolder() {
  char s[16];
  folderSuffix(1, 1, s, sizeof(s));
  CHECK(s[0] == 0, "driver 1 with car 1: no folder (today's files stay theirs)");
  folderSuffix(3, 2, s, sizeof(s));
  CHECK(!strcmp(s, "d3-c2"), "d3-c2 (%s)", s);
  folderSuffix(1, 2, s, sizeof(s));
  CHECK(!strcmp(s, "d1-c2"), "driver 1 with another car has its own: %s", s);
  folderSuffix(2, 1, s, sizeof(s));
  CHECK(!strcmp(s, "d2-c1"), "another driver with car 1: %s", s);
  folderSuffix(255, 255, s, sizeof(s));
  CHECK(!strcmp(s, "d255-c255"), "the longest: %s", s);
  // If driver 1 is removed, nobody inherits the legacy folder: ids aren't reused
  Book b;
  int d2 = 0;
  b.add(Kind::Driver, "Two", &d2);
  b.remove(Kind::Driver, 1);
  int d3 = 0;
  b.add(Kind::Driver, "Three", &d3);
  CHECK(d3 == 3 && b.drivers.find(1) == nullptr, "id 1 stays gone");
  printf("  folder: empty for 1 and 1, else d<driver>-c<car>\n");
}

static void testSanitize() {
  Book b;
  // Garbage as it might come back from flash
  b.drivers.count = 4;
  b.drivers.items[0].id = 3;  snprintf(b.drivers.items[0].name, sizeof(b.drivers.items[0].name), "  Sam ");
  b.drivers.items[1].id = 3;  snprintf(b.drivers.items[1].name, sizeof(b.drivers.items[1].name), "Dup id");
  b.drivers.items[2].id = 0;  snprintf(b.drivers.items[2].name, sizeof(b.drivers.items[2].name), "Zero id");
  b.drivers.items[3].id = 9;  snprintf(b.drivers.items[3].name, sizeof(b.drivers.items[3].name), "sam");
  b.drivers.nextId = 1;
  b.drivers.current = 77;
  b.cars.count = 0;
  b.sanitize();
  CHECK(b.drivers.count == 1 && b.drivers.items[0].id == 3 && !strcmp(b.drivers.items[0].name, "Sam"), "bad entries dropped, name trimmed (%d)", b.drivers.count);
  CHECK(b.drivers.nextId == 4, "counter past the biggest id (%d)", b.drivers.nextId);
  CHECK(b.driverId() == 3, "current falls back to the first");
  CHECK(b.cars.count == 1 && b.carId() == 1 && !strcmp(b.carName(), "Car 1"), "an empty list gets its default");
  Book ok;
  int id;
  ok.add(Kind::Car, "Civic", &id);
  ok.setVin(id, VIN_A);
  ok.select(Kind::Car, id);
  Book copy = ok;
  copy.sanitize();
  CHECK(copy == ok, "a good book is left as it is");
  printf("  sanitize: bad entries dropped, counters and current repaired\n");
}

int main() {
  printf("Drivers and cars tests\n");
  testDefaults();
  testNames();
  testIdsAndLimit();
  testRemoveSelect();
  testVin();
  testLabel();
  testFolder();
  testSanitize();
  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All passed\n");
  return 0;
}
