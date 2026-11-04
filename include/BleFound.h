/**
 * Track Rat - the list of Bluetooth devices a scan found (OBD adapters, cameras)
 *
 * ObdBle and CameraBle both show "what the scan found, strongest first" on a pick-one screen. The device record and
 * the sorted insert live here, with no Arduino or NimBLE code, so they can be tested on the host.
 */

#ifndef BLE_FOUND_H
#define BLE_FOUND_H

#include <stdint.h>

namespace blefound {

struct Found {
  char name[24];
  char address[18];
  uint8_t addressType;
  int rssi;
};
constexpr int MAX_FOUND = 6;

// Put f into list (n entries, strongest signal first), after any with the same rssi. When the list is full the
// weakest falls off the end (or f itself is dropped if it is the weakest). Returns the new count.
inline int insert(Found* list, int n, const Found& f) {
  int at = n;
  while (at > 0 && list[at - 1].rssi < f.rssi) at--;
  if (at >= MAX_FOUND) return n;
  for (int k = (n < MAX_FOUND ? n : MAX_FOUND - 1); k > at; k--) list[k] = list[k - 1];
  list[at] = f;
  return n < MAX_FOUND ? n + 1 : n;
}

}  // namespace blefound

#endif
