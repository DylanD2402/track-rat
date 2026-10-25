/**
 * Track Rat - car profiles on the SD card (/obd/*.json)
 *
 * A car profile lists the car-specific requests to make (brake pressure,
 * steering angle...) in OBDb's own format: its "commands", each with "hdr",
 * "rax", "eax", "fcm1", "cmd", "freq" and "signals" (id, name and "fmt").
 * tools/obd_car.py makes one from a car's OBDb signal set, with only the
 * signals you choose, plus Track Rat's own fields:
 *
 *   "name": "BMW 128i"      shown on the Engine data screen and in the log
 *   "vin": "WBAUP"          the start of the VIN it's for ("" or none: any car)
 *   "year": 2010            the model year it was made for
 *
 * Profiles are CC BY-SA 4.0 (they're adapted from OBDb's data, which is);
 * the firmware has no OBDb data in it. See README "Car-specific data".
 *
 * Read once at boot (ObdBle::begin). A file with more than MAX_SIGNALS
 * signals (an OBDb file copied as it is) keeps the first ones.
 */

#ifndef CAR_PROFILES_H
#define CAR_PROFILES_H

#include "CarObd.h"

namespace CarProfiles {

constexpr int MAX_PROFILES = 4;
constexpr const char* DIR = "/obd";

// The profiles on the SD card, into out. Returns how many.
int load(carobd::Profile* out, int max);

}  // namespace CarProfiles

#endif  // CAR_PROFILES_H
