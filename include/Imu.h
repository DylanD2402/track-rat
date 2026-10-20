/**
 * Track Rat - optional IMU (LSM6DSO-class, on the I2C header)
 *
 * Found at boot on the touch controller's I2C bus (address 0x6A or 0x6B; the
 * touch is at 0x38). With none, nothing changes: the g-forces in the log and
 * the RaceBox packet come from the GPS as before.
 *
 * With one, poll() reads it at 104 Hz from loop(), and onFix() (once per GPS
 * fix) averages the readings since the last fix and learns how it's mounted
 * (ImuMath.h: up from standing still, forward from speeding up and braking in
 * a straight line). Once it knows, carG() is the car's real g for the log and
 * the RaceBox packet. It learns again at every boot, as the unit may have
 * been moved.
 *
 * Untested on hardware: no IMU has been fitted yet. IMU_ENABLED in Config.h.
 */

#ifndef IMU_H
#define IMU_H

#include "GpsFix.h"
#include "ImuMath.h"

namespace Imu {

void begin();  // after the touch controller has started the I2C bus
bool found();
void poll();   // from loop(): reads a new sample if there is one
void onFix(const GpsFix& fix);  // once per GPS fix

// The car's g and rotation rates at the last fix. False until the mount is learned (or with no IMU).
bool carG(imu::CarG& g, imu::CarRates& rates);

}  // namespace Imu

#endif  // IMU_H
