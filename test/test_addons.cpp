/**
 * Host-side tests for the add-on pin check (AddOns.h): an add-on whose pin the
 * GPS ended up on must stay off.
 *
 *   g++ -std=c++17 -Iinclude test/test_addons.cpp -o /tmp/ao && /tmp/ao
 */

#include <cstdio>
#include "AddOns.h"
#include "check.h"

int main() {
  CHECK(!addOnPinTaken(21, 44, 43), "UART connector leaves IO21 free");
  CHECK(!addOnPinTaken(14, 44, 43), "UART connector leaves IO14 free");
  CHECK(addOnPinTaken(21, 14, 21), "GPS on 14/21 takes IO21");
  CHECK(addOnPinTaken(21, 21, 14), "GPS on 21/14 takes IO21");
  CHECK(addOnPinTaken(14, 14, 21), "GPS on 14/21 takes IO14");
  CHECK(!addOnPinTaken(3, 14, 21), "GPS on 14/21 leaves IO3 free");
  CHECK(!addOnPinTaken(-1, 14, 21), "no pin is never taken");
  printf("  add-on pin collisions: ok\n");

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
