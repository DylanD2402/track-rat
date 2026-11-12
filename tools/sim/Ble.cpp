// Stand-ins for src/RaceBoxBle.cpp, src/ObdBle.cpp and src/CameraBle.cpp (tools/build_sim.py): a browser has no
// Bluetooth for the unit to use, so nothing is connected and a scan finds nothing (after its usual wait).
// Same headers (include/RaceBoxBle.h, ObdBle.h, CameraBle.h).
#include "CameraBle.h"
#include "Config.h"
#include "ObdBle.h"
#include "RaceBoxBle.h"

namespace RaceBoxBle {
void initRadio() {}
void begin() {}
void setEnabled(bool) {}
void setPaused(bool) {}
void sendFix(const GpsFix&, const GPSHandler&) {}
bool connected() { return false; }
void onSubscribed(uint16_t) {}
void resumeAdvertising() {}
bool radioTryLock(uint32_t) { return true; }
void radioUnlock() {}
}  // namespace RaceBoxBle

namespace ObdBle {
static uint32_t scanStart = 0;
static bool scanOn = false;
static obd::Values vals;
static carobd::Values carVals;

void begin() {}
void poll() {}
bool configured() { return false; }
bool connected() { return false; }
bool readingEngine() { return false; }
const obd::Values& values() { return vals; }
Status status() { return Status(); }
const carobd::Profile* carProfile() { return nullptr; }
const carobd::Values& carValues() { return carVals; }
void startScan() {
  scanOn = true;
  scanStart = millis();
}
bool scanning() {
  if (scanOn && millis() - scanStart >= OBD_SCAN_S * 1000UL) scanOn = false;
  return scanOn;
}
int found(Found*, int) { return 0; }
void choose(const Found&) {}
void forget() {}
void setPaused(bool) {}
}  // namespace ObdBle

namespace CameraBle {
static uint32_t scanStart = 0;
static bool scanOn = false;

void begin() {}
void poll() {}
void onFix(const GpsFix&, bool) {}
void setRecording(bool) {}
void toggleRecording() {}
bool configured() { return false; }
bool connected() { return false; }
bool takeStarted(uint32_t&) { return false; }
Status status() { return Status(); }
void startScan() {
  scanOn = true;
  scanStart = millis();
}
bool scanning() {
  if (scanOn && millis() - scanStart >= CAMERA_SCAN_S * 1000UL) scanOn = false;
  return scanOn;
}
int found(Found*, int) { return 0; }
void choose(const Found&) {}
void forget() {}
void setPaused(bool) {}
}  // namespace CameraBle
