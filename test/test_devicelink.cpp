/**
 * Host-side tests for when a Bluetooth device counts as connected (include/DeviceLink.h):
 * the link must be up AND the device must have sent something recently, so a remembered
 * adapter or camera that is away, off or mid-reconnect shows nothing.
 *
 *   g++ -std=c++17 -Iinclude test/test_devicelink.cpp -o /tmp/dl && /tmp/dl
 */

#include <cstdio>
#include "DeviceLink.h"
#include "check.h"

using namespace devicelink;

static void testAlive() {
  CHECK(OBD_ALIVE_MS >= 4000 && OBD_ALIVE_MS <= 10000, "OBD window covers the car-off retry gap (3 s) and no more than 10 s");
  CHECK(CAMERA_ALIVE_MS >= 2000 && CAMERA_ALIVE_MS <= 5000, "camera window: a few of its half-second status pushes");
  CHECK(!alive(true, 0, 5000, 3000), "link up but the device has said nothing yet: not connected");
  CHECK(!alive(false, 4900, 5000, 3000), "just heard from it but the link is down: not connected");
  CHECK(alive(true, 4900, 5000, 3000), "link up and heard 100 ms ago: connected");
  CHECK(alive(true, 2000, 5000, 3000), "exactly at the window: still connected");
  CHECK(!alive(true, 1999, 5000, 3000), "1 ms past the window: gone");
  CHECK(alive(true, 5002, 5000, 3000), "stamped a moment after now (other core): connected");
  printf("  alive: needs the link, a first message, and a recent one\n");
}

static void testClockWrap() {
  const uint32_t last = 0xFFFFFF00u;
  CHECK(alive(true, last, 0xFFFFFF00u + 1000u, 3000), "1 s old across the wrap point: connected");
  CHECK(alive(true, last, 500u, 3000), "256 + 500 ms old after millis() wrapped: connected");
  CHECK(!alive(true, last, 5000u, 3000), "about 5 s old after the wrap: gone");
  printf("  alive: right across millis() wrapping\n");
}

static void testCameraSwitchedOff() {
  // Status pushes every 500 ms; then the camera is switched off. The link stays "up" until the
  // supervision timeout, but the tag must drop within CAMERA_ALIVE_MS of the last push.
  uint32_t lastStatus = 0;
  uint32_t droppedAt = 0;
  for (uint32_t t = 1000; t < 20000; t += 100) {
    if (t <= 6000 && t % 500 == 0) lastStatus = t;  // switched off after 6 s
    const bool up = cameraAlive(true, lastStatus, t);
    if (t == 1000) CHECK(up, "first status: connected");
    if (!up && !droppedAt) droppedAt = t;
  }
  CHECK(droppedAt > 6000 && droppedAt <= 6000 + CAMERA_ALIVE_MS + 100, "dropped %u ms after the last status", (unsigned)(droppedAt - 6000));
  printf("  camera switched off: tag gone within %d ms, link still up\n", CAMERA_ALIVE_MS);
}

static void testObdUnplugged() {
  // Replies keep coming (even "UNABLE TO CONNECT" with the ignition off, every ~3.5 s), then it is unplugged.
  uint32_t last = 0, droppedAt = 0;
  bool flickered = false;
  for (uint32_t t = 100; t < 30000; t += 50) {
    if (t <= 10000 && t % 3500 == 0) last = t;
    const bool up = obdAlive(true, last, t);
    if (t > 3600 && t <= 10000 && !up) flickered = true;
    if (!up && t > 10000 && !droppedAt) droppedAt = t;
  }
  CHECK(!flickered, "a car-off adapter answering every 3.5 s never blinks the tag");
  CHECK(droppedAt > 10000 && droppedAt <= 10000 + 3500 + OBD_ALIVE_MS + 100, "unplugged: gone at %u ms", (unsigned)droppedAt);
  printf("  OBD adapter: steady while it answers, gone soon after it stops\n");
}

int main() {
  printf("Device link tests\n");
  testAlive();
  testClockWrap();
  testCameraSwitchedOff();
  testObdUnplugged();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nAll device link tests passed\n", failures);
  return failures ? 1 : 0;
}
