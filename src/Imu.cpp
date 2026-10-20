/**
 * Track Rat - optional IMU implementation (see Imu.h). Untested on hardware.
 */

#include "Imu.h"
#include "Config.h"
#include "VboFormat.h"
#include <Arduino.h>
#include <Wire.h>

namespace Imu {

#if IMU_ENABLED

static uint8_t addr = 0;  // 0: none found
static imu::Average avg;
static imu::MountLearner mount;
static vbo::GForce gpsG;  // the GPS's own g, which shows the learner which way is forward
static bool haveCar = false, said = false;
static imu::CarG lastG = {0, 0, 1};
static imu::CarRates lastRates = {0, 0, 0};
static uint32_t lastPoll = 0;

static bool writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t a, uint8_t reg, uint8_t* out, uint8_t n) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(a, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) out[i] = Wire.read();
  return true;
}

void begin() {
  for (uint8_t a : {lsm6::ADDR_LOW, lsm6::ADDR_HIGH}) {
    uint8_t who = 0;
    if (!readRegs(a, lsm6::WHO_AM_I, &who, 1) || !lsm6::chipName(who)) continue;
    addr = a;
    const bool ok = writeReg(lsm6::CTRL3_C, lsm6::CTRL3_C_BDU_INC) && writeReg(lsm6::CTRL1_XL, lsm6::CTRL1_XL_104HZ_4G) &&
                    writeReg(lsm6::CTRL2_G, lsm6::CTRL2_G_104HZ_500DPS);
    if (!ok) {
      addr = 0;
      DEBUG_PRINTF("IMU: %s at 0x%02X didn't take its set-up - not used\n", lsm6::chipName(who), a);
      return;
    }
    DEBUG_PRINTF("IMU: %s at 0x%02X - real g once it has learned how it's mounted\n", lsm6::chipName(who), a);
    return;
  }
  DEBUG_PRINTLN("IMU: none (g-forces from the GPS)");
}

bool found() { return addr != 0; }

void poll() {
  if (!addr) return;
  const uint32_t now = millis();
  if (now - lastPoll < 4) return;  // 104 Hz: a new sample about every 10 ms
  lastPoll = now;
  uint8_t status = 0;
  if (!readRegs(addr, lsm6::STATUS_REG, &status, 1) || !(status & 0x01)) return;
  uint8_t raw[12];
  if (readRegs(addr, lsm6::OUTX_L_G, raw, sizeof(raw))) avg.add(lsm6::decode(raw));
}

void onFix(const GpsFix& fix) {
  if (!addr) return;
  gpsG.update(fix.gpsTimeMs, fix.speedKmh, fix.courseDeg, fix.courseValid);
  imu::Vec a, w;
  if (!avg.take(a, w)) {
    haveCar = false;  // no readings since the last fix: don't hand on old ones
    return;
  }
  if (!mount.ready()) mount.add(a, fix.speedKmh, gpsG.longitudinal(), gpsG.lateral());
  if (!mount.ready()) return;
  if (!said) {
    said = true;
    DEBUG_PRINTLN("IMU: mount learned - the log and RaceBox output now use its g");
  }
  lastG = mount.carG(a);
  lastRates = mount.carRates(w);
  haveCar = true;
}

bool carG(imu::CarG& g, imu::CarRates& rates) {
  if (!haveCar) return false;
  g = lastG;
  rates = lastRates;
  return true;
}

#else  // IMU_ENABLED 0: nothing

void begin() {}
bool found() { return false; }
void poll() {}
void onFix(const GpsFix&) {}
bool carG(imu::CarG&, imu::CarRates&) { return false; }

#endif

}  // namespace Imu
