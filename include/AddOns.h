/**
 * Track Rat - Add-on pin check
 *
 * The big button (IO21) and the CAN transceiver (IO3/IO14) share the expansion
 * header with the GPS, which is found at boot on either the UART connector or
 * IO14/IO21. An add-on whose pin the GPS ended up on must stay off.
 *
 * No Arduino dependencies: tested in test/test_addons.cpp.
 */

#ifndef ADD_ONS_H
#define ADD_ONS_H

// True if `pin` is one of the two pins the GPS is using (-1 = unknown / none).
inline bool addOnPinTaken(int pin, int gpsRxPin, int gpsTxPin) {
  return pin >= 0 && (pin == gpsRxPin || pin == gpsTxPin);
}

#endif  // ADD_ONS_H
