// Host test for include/TrackOrder.h: the track list, nearest first.
//   g++ -std=c++17 -Iinclude test/test_trackorder.cpp -o /tmp/to && /tmp/to

#include <cstdio>
#include <vector>
#include "TrackOrder.h"

using trackorder::Item;

static int failures = 0;
static void expect(const std::vector<Item>& items, const std::vector<int>& want, const char* what) {
  std::vector<int> got = trackorder::byDistance(items);
  if (got != want) {
    printf("FAIL %s: got", what);
    for (int i : got) printf(" %d", i);
    printf(", want");
    for (int i : want) printf(" %d", i);
    printf("\n");
    failures++;
  }
}

int main() {
  printf("Track order tests\n");
  // 0: 50 km, 1: 3 km, 2: 400 km, 3: bench
  expect({{50000, -1, false}, {3000, -1, false}, {400000, -1, false}, {-1, -1, true}}, {1, 0, 2, 3},
         "nearest first, bench last");
  // No GPS fix: every distance unknown, file order kept, bench last
  expect({{-1, -1, false}, {-1, -1, false}, {-1, -1, true}, {-1, -1, false}}, {0, 1, 3, 2}, "no fix: file order");
  // A venue (group 7) with three layouts 10.2/10.0/10.1 km stays together in file order,
  // ahead of a track at 12 km and behind one at 2 km
  expect({{12000, -1, false}, {10200, 7, false}, {10000, 7, false}, {10100, 7, false}, {2000, -1, false}},
         {4, 1, 2, 3, 0}, "layouts stay together at the venue's distance");
  // Two venues at the same distance keep their tracks.json order
  expect({{5000, 1, false}, {5000, 2, false}, {5000, 1, false}, {5000, 2, false}}, {0, 2, 1, 3},
         "same distance: venue order, layouts together");
  // A new track (no start/finish yet, found by its centre) sorts like any other
  expect({{900, -1, false}, {800, -1, false}}, {1, 0}, "plain distance order");
  // Unknown distance for one track only (shouldn't happen, but goes after the known ones)
  expect({{-1, -1, false}, {700, -1, false}, {-1, -1, true}}, {1, 0, 2}, "unknown after known");
  expect({}, {}, "empty list");
  printf(failures ? "%d FAILED\n" : "ALL PASSED\n", failures);
  return failures ? 1 : 0;
}
