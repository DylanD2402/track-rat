/**
 * Host-side tests for the IMU maths (ImuMath.h): reading the chip's raw
 * bytes, which chips answer, and learning how the IMU is mounted (up from
 * standing still, forward from speeding up and braking in a straight line)
 * for mounts at any angle, then the car's g and rotation rates from a
 * reading. The hardware itself is untested: no IMU has been fitted yet.
 *
 *   g++ -std=c++17 -Iinclude test/test_imu.cpp -o /tmp/imu && /tmp/imu
 */

#include <cmath>
#include <cstdio>
#include "ImuMath.h"
#include "check.h"

using namespace imu;

// A mount: the chip's x, y, z axes in the car's frame (forward, left, up: right-handed, like the chip's)
struct Mount {
  Vec dx, dy, dz;
  // What the chip reads for a vector given as forward, right and up components
  Vec read(float fwd, float right, float up) const {
    const Vec v = vec(fwd, -right, up);
    return vec(dot(v, dx), dot(v, dy), dot(v, dz));
  }
};

// Rotations about up (yaw), left (pitch) and forward (roll), in degrees
static Mount mountAt(float yawDeg, float pitchDeg, float rollDeg) {
  const float y = yawDeg * (float)M_PI / 180, p = pitchDeg * (float)M_PI / 180, r = rollDeg * (float)M_PI / 180;
  auto rot = [&](Vec v) {
    // roll about forward (x), then pitch about left (y), then yaw about up (z)
    Vec a = vec(v.x, v.y * cosf(r) - v.z * sinf(r), v.y * sinf(r) + v.z * cosf(r));
    Vec b = vec(a.x * cosf(p) + a.z * sinf(p), a.y, -a.x * sinf(p) + a.z * cosf(p));
    return vec(b.x * cosf(y) - b.y * sinf(y), b.x * sinf(y) + b.y * cosf(y), b.z);
  };
  Mount m;
  m.dx = rot(vec(1, 0, 0));
  m.dy = rot(vec(0, 1, 0));
  m.dz = rot(vec(0, 0, 1));
  return m;
}

static float noise(float amp) { return amp * ((testRand() / 32767.0f) * 2 - 1); }

// Standing still, then straight-line speeding up and braking: the learner should know the mount
static void learn(MountLearner& l, const Mount& m) {
  for (int i = 0; i < 50; i++) l.add(m.read(noise(0.01f), noise(0.01f), 1 + noise(0.01f)), 0, 0, 0);
  for (int i = 0; i < 20; i++) l.add(m.read(0.3f + noise(0.02f), noise(0.03f), 1), 40, 0.3f, 0.01f);
  for (int i = 0; i < 10; i++) l.add(m.read(-0.8f + noise(0.02f), noise(0.03f), 1), 90, -0.8f, -0.02f);
}

int main() {
  printf("IMU maths tests\n");
  testSeed(3);

  // Raw bytes: gyro x = +1000 LSB, accelerometer z = +8197 LSB (about 1 g)
  {
    uint8_t b[12] = {0xE8, 0x03, 0, 0, 0x18, 0xFC, 0, 0, 0, 0, 0x05, 0x20};
    lsm6::Reading r = lsm6::decode(b);
    CHECK(fabsf(r.gx - 17.5f) < 0.01f && fabsf(r.gz + 17.5f) < 0.01f && r.gy == 0, "gyro dps: %.2f %.2f", r.gx, r.gz);
    CHECK(fabsf(r.az - 1.0f) < 0.001f && r.ax == 0 && r.ay == 0, "accel g: %.4f", r.az);
    CHECK(lsm6::chipName(0x6C) && lsm6::chipName(0x6A) && lsm6::chipName(0x69) && !lsm6::chipName(0x00) &&
              !lsm6::chipName(0xFF),
          "WHO_AM_I answers");
  }

  // Mounts at all sorts of angles: flat, on its side, upside down, tilted, back to front
  const float mounts[][3] = {{0, 0, 0}, {90, 0, 0}, {180, 0, 0}, {0, 0, 90}, {0, 0, 180}, {30, 20, -15}, {-120, 75, 40}, {200, -40, 170}};
  for (const auto& a : mounts) {
    const Mount m = mountAt(a[0], a[1], a[2]);
    MountLearner l;
    CHECK(!l.knowsUp() && !l.ready(), "knows nothing at first");
    learn(l, m);
    CHECK(l.ready(), "mount %.0f/%.0f/%.0f learned", a[0], a[1], a[2]);
    // Hard right corner while braking a little, on a bump
    const CarG g = l.carG(m.read(-0.2f, 0.9f, 1.1f));
    CHECK(fabsf(g.lon + 0.2f) < 0.05f && fabsf(g.lat - 0.9f) < 0.05f && fabsf(g.vert - 1.1f) < 0.05f,
          "mount %.0f/%.0f/%.0f: lon %.2f lat %.2f vert %.2f", a[0], a[1], a[2], g.lon, g.lat, g.vert);
    // Turning left (yaw +) with the nose dipping (pitch -) and leaning right (roll +)
    const CarRates r = l.carRates(m.read(5, -3, 20));
    CHECK(fabsf(r.roll - 5) < 1 && fabsf(r.pitch + 3) < 1 && fabsf(r.yaw - 20) < 1, "rates %.1f %.1f %.1f", r.roll, r.pitch,
          r.yaw);
  }

  // The car frame itself: right is forward x up, and the signs are the log's
  {
    const Mount flat = mountAt(0, 0, 0);
    MountLearner l;
    learn(l, flat);
    // Flat and facing forward: the chip's x is forward, y left, z up
    CHECK(fabsf(dot(l.forward(), vec(1, 0, 0)) - 1) < 0.01f && fabsf(dot(l.up(), vec(0, 0, 1)) - 1) < 0.01f, "flat mount axes");
    CHECK(fabsf(dot(l.right(), vec(0, -1, 0)) - 1) < 0.01f, "right = forward x up");
  }

  // Not enough: cornering only, or too little speeding up, never says it knows forward
  {
    const Mount m = mountAt(45, 10, 5);
    MountLearner l;
    for (int i = 0; i < 50; i++) l.add(m.read(0, 0, 1), 0, 0, 0);
    CHECK(l.knowsUp() && !l.ready(), "up but not forward yet");
    for (int i = 0; i < 200; i++) l.add(m.read(0.05f, 0.8f, 1), 80, 0.05f, 0.8f);
    CHECK(!l.ready(), "corners teach nothing about forward");
    for (int i = 0; i < 20; i++) l.add(m.read(0.3f, 0, 1), 40, 0.3f, 0);
    CHECK(!l.ready(), "6 g of speeding up isn't enough yet");
    for (int i = 0; i < 10; i++) l.add(m.read(0.3f, 0, 1), 40, 0.3f, 0);
    CHECK(l.ready(), "9 g is");
  }

  // Readings that disagree (the GPS's long g doesn't match the IMU at all) don't make a mount
  {
    const Mount m = mountAt(0, 0, 0);
    MountLearner l;
    for (int i = 0; i < 50; i++) l.add(m.read(0, 0, 1), 0, 0, 0);
    for (int i = 0; i < 100; i++) {
      const float ang = (float)i * 2.4f;
      l.add(m.read(0.4f * cosf(ang), 0.4f * sinf(ang), 1), 50, (i & 1) ? 0.3f : -0.3f, 0);
    }
    CHECK(!l.ready(), "random horizontal readings: no forward");
  }

  // Engine shake while parked (readings far from 1 g) isn't counted for up
  {
    MountLearner l;
    for (int i = 0; i < 100; i++) l.add(vec(0.3f, 0, 1.2f), 0, 0, 0);
    CHECK(!l.knowsUp(), "shaking readings ignored");
    l.reset();
    for (int i = 0; i < MountLearner::STILL_SAMPLES; i++) l.add(vec(0, 0, 1), 0.5f, 0, 0);
    CHECK(l.knowsUp(), "still readings counted");
  }

  // Averaging between fixes
  {
    Average avg;
    Vec a, w;
    CHECK(!avg.take(a, w), "nothing yet");
    lsm6::Reading r1 = {0, 0, 1, 10, 0, 0}, r2 = {0.2f, 0, 1, 20, 0, 0};
    avg.add(r1);
    avg.add(r2);
    CHECK(avg.take(a, w) && fabsf(a.x - 0.1f) < 1e-6f && fabsf(a.z - 1) < 1e-6f && fabsf(w.x - 15) < 1e-5f, "mean");
    CHECK(!avg.take(a, w), "and starts again");
  }

  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All IMU maths tests passed\n");
  return 0;
}
