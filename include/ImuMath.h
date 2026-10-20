/**
 * Track Rat - IMU maths (optional LSM6DSO-class IMU on the I2C header)
 *
 * The parts of the IMU support that don't touch the hardware:
 *
 * - lsm6: which chips answer (WHO_AM_I), the register set-up, and turning
 *   the raw 16-bit readings into g and degrees per second.
 * - MountLearner: the IMU can be mounted any way round, so it learns which
 *   way is up (the average reading while the car stands still: the
 *   accelerometer reads +1 g straight up) and which way is forward (the
 *   horizontal reading while the GPS says the car is speeding up or braking
 *   in a straight line). Until it knows both, nothing changes: the log and
 *   the RaceBox packet keep the g-forces worked out from the GPS.
 * - With both, carG() turns a reading into the car's longitudinal (+ =
 *   speeding up), lateral (+ = turning right, as the .vbo log has it) and
 *   vertical g, and carRates() the gyro into roll, pitch and yaw rates.
 *
 * Untested on hardware: no IMU has been fitted yet. Pure C++, tested on a PC
 * in test/test_imu.cpp.
 */

#ifndef IMU_MATH_H
#define IMU_MATH_H

#include <math.h>
#include <stdint.h>

namespace lsm6 {

// I2C addresses (SA0 low, high). The touch controller is at 0x38 on the same bus.
constexpr uint8_t ADDR_LOW = 0x6A, ADDR_HIGH = 0x6B;

// Registers
constexpr uint8_t WHO_AM_I = 0x0F;
constexpr uint8_t CTRL1_XL = 0x10, CTRL2_G = 0x11, CTRL3_C = 0x12;
constexpr uint8_t STATUS_REG = 0x1E;  // bit 0: new accelerometer data, bit 1: new gyro data
constexpr uint8_t OUTX_L_G = 0x22;    // gyro X, Y, Z then accelerometer X, Y, Z: 12 bytes, little-endian

// Set-up: 104 Hz, accelerometer +/-4 g, gyro +/-500 dps, block data update and
// register auto-increment (the same on the LSM6DSO, DSOX, DSL, DS3 and DS3TR-C).
constexpr uint8_t CTRL1_XL_104HZ_4G = 0x48;
constexpr uint8_t CTRL2_G_104HZ_500DPS = 0x44;
constexpr uint8_t CTRL3_C_BDU_INC = 0x44;
constexpr float G_PER_LSB = 0.000122f;    // +/-4 g
constexpr float DPS_PER_LSB = 0.0175f;    // +/-500 dps

// The chips this set-up suits, by their WHO_AM_I answer
inline const char* chipName(uint8_t whoAmI) {
  switch (whoAmI) {
    case 0x6C: return "LSM6DSO";       // also DSOX, DSR
    case 0x6A: return "LSM6DSL";       // also DS3TR-C
    case 0x69: return "LSM6DS3";
    default: return nullptr;
  }
}

struct Reading {
  float ax, ay, az;  // g, the chip's own axes
  float gx, gy, gz;  // degrees per second
};

// The 12 bytes from OUTX_L_G
inline Reading decode(const uint8_t* b) {
  auto s16 = [&](int i) { return (int16_t)(b[i] | (b[i + 1] << 8)); };
  Reading r;
  r.gx = s16(0) * DPS_PER_LSB;
  r.gy = s16(2) * DPS_PER_LSB;
  r.gz = s16(4) * DPS_PER_LSB;
  r.ax = s16(6) * G_PER_LSB;
  r.ay = s16(8) * G_PER_LSB;
  r.az = s16(10) * G_PER_LSB;
  return r;
}

}  // namespace lsm6

namespace imu {

struct Vec {
  float x = 0, y = 0, z = 0;
};
inline Vec vec(float x, float y, float z) {
  Vec v;
  v.x = x;
  v.y = y;
  v.z = z;
  return v;
}
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec cross(Vec a, Vec b) { return vec(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline Vec scale(Vec a, float k) { return vec(a.x * k, a.y * k, a.z * k); }
inline Vec add(Vec a, Vec b) { return vec(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec sub(Vec a, Vec b) { return vec(a.x - b.x, a.y - b.y, a.z - b.z); }
inline float norm(Vec a) { return sqrtf(dot(a, a)); }
inline Vec unit(Vec a) {
  const float n = norm(a);
  return n > 1e-6f ? scale(a, 1.0f / n) : Vec();
}

// The car's g: longitudinal (+ = speeding up), lateral (+ = turning right), vertical (+1 at rest)
struct CarG {
  float lon, lat, vert;
};
// The car's rotation rates in degrees per second: roll (about forward, + = right side down),
// pitch (about the right-hand axis, + = nose up), yaw (about up, + = turning left)
struct CarRates {
  float roll, pitch, yaw;
};

class MountLearner {
public:
  static constexpr float STILL_KMH = 1.0f;          // slower than this: standing still
  static constexpr float STILL_MAX_DEV_G = 0.05f;   // a still reading this far from 1 g isn't still (engine shake, a door)
  static constexpr int STILL_SAMPLES = 40;          // readings to know up (2 s at 20 a second)
  static constexpr float LONG_MIN_G = 0.12f;        // GPS: speeding up or braking at least this hard...
  static constexpr float LAT_MAX_G = 0.08f;         // ...in a straight line
  static constexpr float FORWARD_WEIGHT = 8.0f;     // g of straight-line speeding up and braking, added up, to know forward
  static constexpr float MIN_AGREEMENT = 0.6f;      // the readings must point the same way this much (0..1)

  void reset() { *this = MountLearner(); }

  // One reading (the average since the last GPS fix) with what the GPS says
  // then: its speed and its own longitudinal and lateral g.
  void add(Vec a, float speedKmh, float gpsLongG, float gpsLatG) {
    if (speedKmh < STILL_KMH) {
      if (fabsf(norm(a) - 1.0f) < STILL_MAX_DEV_G) {
        upSum = imu::add(upSum, a);
        upCount++;
      }
      return;
    }
    if (!knowsUp() || fabsf(gpsLongG) < LONG_MIN_G || fabsf(gpsLatG) > LAT_MAX_G) return;
    const Vec u = up();
    const Vec h = sub(a, scale(u, dot(a, u)));  // the horizontal part
    fwdSum = imu::add(fwdSum, scale(h, gpsLongG > 0 ? 1.0f : -1.0f));  // speeding up: along forward; braking: against it
    fwdWeight += fabsf(gpsLongG);
    fwdLenSum += norm(h);
  }

  bool knowsUp() const { return upCount >= STILL_SAMPLES; }
  // Both up and forward, with the forward readings agreeing
  bool ready() const {
    return knowsUp() && fwdWeight >= FORWARD_WEIGHT && fwdLenSum > 0 && norm(fwdSum) / fwdLenSum >= MIN_AGREEMENT;
  }
  Vec up() const { return unit(upSum); }
  Vec forward() const {
    const Vec u = up();
    return unit(sub(fwdSum, scale(u, dot(fwdSum, u))));  // square to up
  }
  Vec right() const { return cross(forward(), up()); }

  CarG carG(Vec a) const {
    CarG g;
    g.lon = dot(a, forward());
    g.lat = dot(a, right());
    g.vert = dot(a, up());
    return g;
  }
  CarRates carRates(Vec w) const {
    CarRates r;
    r.roll = dot(w, forward());
    r.pitch = dot(w, right());  // nose up turns forward towards up about the right-hand axis
    r.yaw = dot(w, up());
    return r;
  }

private:
  Vec upSum, fwdSum;
  int upCount = 0;
  float fwdWeight = 0, fwdLenSum = 0;
};

// Averages the IMU's readings between GPS fixes (about five at 104 Hz and 20 fixes a second)
struct Average {
  Vec a, w;
  int n = 0;
  void add(const lsm6::Reading& r) {
    a = imu::add(a, vec(r.ax, r.ay, r.az));
    w = imu::add(w, vec(r.gx, r.gy, r.gz));
    n++;
  }
  // The mean so far, and starts again. False if there were no readings.
  bool take(Vec& accel, Vec& rate) {
    if (!n) return false;
    accel = scale(a, 1.0f / n);
    rate = scale(w, 1.0f / n);
    *this = Average();
    return true;
  }
};

}  // namespace imu

#endif  // IMU_MATH_H
