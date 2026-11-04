/**
 * Host-side tests for the scan's found list (include/BleFound.h): strongest signal first, ties in the order found,
 * the weakest dropped when the list is full.
 *
 *   g++ -std=c++17 -Iinclude test/test_blefound.cpp -o /tmp/blef && /tmp/blef
 */

#include <cstdio>
#include <cstring>
#include "BleFound.h"
#include "check.h"

using namespace blefound;

static Found make(const char* name, int rssi) {
  Found f = {};
  strncpy(f.name, name, sizeof(f.name) - 1);
  f.rssi = rssi;
  return f;
}

static void testEmptyAndOrder() {
  Found list[MAX_FOUND] = {};
  int n = insert(list, 0, make("a", -70));
  CHECK(n == 1 && list[0].rssi == -70, "first device goes in an empty list");
  n = insert(list, n, make("b", -50));
  n = insert(list, n, make("c", -90));
  n = insert(list, n, make("d", -60));
  CHECK(n == 4, "four devices, count %d", n);
  CHECK(list[0].rssi == -50 && list[1].rssi == -60 && list[2].rssi == -70 && list[3].rssi == -90, "strongest first");
  CHECK(!strcmp(list[0].name, "b") && !strcmp(list[3].name, "c"), "records move with their rssi");
  printf("  order: strongest first\n");
}

static void testTies() {
  Found list[MAX_FOUND] = {};
  int n = 0;
  n = insert(list, n, make("first", -60));
  n = insert(list, n, make("second", -60));
  n = insert(list, n, make("strong", -40));
  n = insert(list, n, make("third", -60));
  CHECK(n == 4, "count %d", n);
  CHECK(!strcmp(list[0].name, "strong"), "the strong one leads");
  CHECK(!strcmp(list[1].name, "first") && !strcmp(list[2].name, "second") && !strcmp(list[3].name, "third"),
        "equal signals stay in the order found");
  printf("  ties: first found stays first\n");
}

static void testFull() {
  Found list[MAX_FOUND] = {};
  int n = 0;
  for (int i = 0; i < MAX_FOUND; i++) n = insert(list, n, make("x", -50 - i * 10));  // -50 ... -100
  CHECK(n == MAX_FOUND && list[MAX_FOUND - 1].rssi == -100, "full list, weakest -100");

  n = insert(list, n, make("mid", -75));
  CHECK(n == MAX_FOUND, "still %d when full", MAX_FOUND);
  CHECK(list[MAX_FOUND - 1].rssi == -90, "the weakest (-100) was dropped, now -90 last");
  bool found = false;
  for (int i = 0; i < n; i++) found |= !strcmp(list[i].name, "mid");
  CHECK(found, "the new, stronger device is in");
  for (int i = 1; i < n; i++) CHECK(list[i - 1].rssi >= list[i].rssi, "still sorted at %d", i);

  Found before[MAX_FOUND];
  memcpy(before, list, sizeof(before));
  n = insert(list, n, make("weak", -110));
  CHECK(n == MAX_FOUND && !memcmp(list, before, sizeof(before)), "weaker than all: not added, list unchanged");
  n = insert(list, n, make("equalweakest", list[MAX_FOUND - 1].rssi));
  CHECK(n == MAX_FOUND && !memcmp(list, before, sizeof(before)), "as weak as the weakest: not added");
  printf("  full list: drops the weakest, ignores a weaker newcomer\n");
}

int main() {
  printf("Found list tests\n");
  testEmptyAndOrder();
  testTies();
  testFull();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nAll found list tests passed\n", failures);
  return failures ? 1 : 0;
}
