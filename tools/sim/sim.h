// What the simulator's stand-ins share (tools/sim). Not used by the firmware.
#pragma once
#include <stdint.h>

namespace sim {

// Things that never return on the device. The wrapper (site/try/host.js) throws from the call, so the tick
// unwinds; it then reports the unit as off or restarting.
enum class Halt : int { Off = 1, Restart = 2 };
[[noreturn]] void halt(Halt why);

int backlightPercent();  // 0 (off) .. 100

// The GPS: a fix from JavaScript (lat/lon degrees, speed km/h, heading degrees, accuracy metres, the fix's
// date and time as milliseconds since 1970, satellites), or none
void gpsFix(double lat, double lon, double speedKmh, double headingDeg, double accuracyM, double utcMs, int sats);
void gpsOff();

}  // namespace sim
