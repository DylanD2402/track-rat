/**
 * Host-side tests for the battery maths, the auto-sleep decision and the
 * firmware file check.
 *
 *   g++ -std=c++17 -Iinclude test/test_power.cpp -o /tmp/pw && /tmp/pw
 */

#include <cstdio>
#include <cstring>
#include <vector>
#include "Battery.h"
#include "FirmwareImage.h"
#include "SleepTimer.h"

static int failures = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

// Same numbers as include/Config.h
static battery::Limits limits() { return {2500, 4230, 3300, 10000, 30}; }

static void testVoltageToPercent() {
  using battery::percentFromMv;
  CHECK(percentFromMv(4200) == 100, "4.20 V = %d%%", percentFromMv(4200));
  CHECK(percentFromMv(4300) == 100, "above full = %d%%", percentFromMv(4300));
  CHECK(percentFromMv(3300) == 0, "3.30 V = %d%%", percentFromMv(3300));
  CHECK(percentFromMv(3000) == 0, "below empty = %d%%", percentFromMv(3000));
  CHECK(percentFromMv(3840) == 50, "3.84 V = %d%%", percentFromMv(3840));
  // Halfway between two points lands halfway between their percentages
  CHECK(percentFromMv(3825) >= 45 && percentFromMv(3825) <= 47, "3.825 V = %d%%", percentFromMv(3825));
  // Never goes down as the voltage goes up
  int prev = -1;
  bool monotonic = true;
  for (int mv = 3200; mv <= 4300; mv++) {
    int p = percentFromMv(mv);
    if (p < prev) monotonic = false;
    prev = p;
  }
  CHECK(monotonic, "percentage fell as voltage rose");
  printf("  voltage to percent: 4.20 V %d%%, 3.84 V %d%%, 3.30 V %d%%\n", percentFromMv(4200), percentFromMv(3840),
         percentFromMv(3300));

  // The board's divider: 1.9 V at the pin is a 3.8 V battery
  CHECK(battery::batteryMvFromPinMv(1900, 2.0f, 0) == 3800, "divider");
  CHECK(battery::batteryMvFromPinMv(1900, 2.0f, 25) == 3825, "offset");
}

static void testPresence() {
  battery::Monitor m(limits());
  CHECK(!m.present() && m.percent() == -1, "present before any sample");

  // No battery, pin floating near 0 V: nothing to show, never low
  for (int t = 0; t < 30; t++) m.addSample(t * 1000, 40);
  CHECK(!m.present() && m.percent() == -1, "present with the pin at 0 V");
  CHECK(!m.low(60000), "low with no battery");

  // No battery, charger holding the socket at its float voltage
  for (int t = 0; t < 30; t++) m.addSample(t * 1000, 4260);
  CHECK(!m.present(), "present at the charger's float voltage");

  // A battery is plugged in
  for (int t = 0; t < 10; t++) m.addSample(t * 1000, 3900);
  CHECK(m.present() && m.percent() > 50 && m.percent() < 70, "battery at 3.9 V reads %d%%", m.percent());

  // A full battery just under the limit still counts
  battery::Monitor full(limits());
  for (int t = 0; t < 10; t++) full.addSample(t * 1000, 4195);
  CHECK(full.present() && full.percent() >= 98, "full battery: %d%%", full.percent());

  // Hysteresis: after "no battery", a reading just inside the limit isn't enough
  battery::Monitor h(limits());
  h.addSample(0, 4260);
  h.addSample(1000, 4220);  // inside 4230 but not by 30 mV
  CHECK(!h.present(), "present at 4.22 V straight after none");
  h.addSample(2000, 4150);
  CHECK(h.present(), "not present at 4.15 V");

  // A battery pulled out while running
  battery::Monitor pulled(limits());
  for (int t = 0; t < 10; t++) pulled.addSample(t * 1000, 3700);
  CHECK(pulled.present(), "present");
  pulled.addSample(10000, 30);
  CHECK(!pulled.present() && pulled.percent() == -1, "still present after removal");
  printf("  no battery (0 V or float voltage) shows nothing; real battery shows %d%%\n", m.percent());
}

static void testSmoothing() {
  battery::Monitor m(limits());
  m.addSample(0, 3900);
  // One noisy reading barely moves it
  m.addSample(1000, 3700);
  CHECK(m.millivolts() > 3840 && m.millivolts() <= 3900, "after one dip: %d mV", m.millivolts());
  // It does settle on a lasting change
  for (int t = 2; t < 40; t++) m.addSample(t * 1000, 3700);
  CHECK(m.millivolts() < 3720, "settled at %d mV", m.millivolts());
}

static void testLow() {
  battery::Monitor m(limits());
  uint32_t t = 0;
  for (; t < 10000; t += 1000) m.addSample(t, 3800);
  CHECK(!m.low(t), "low at 3.8 V");

  // Voltage drops to 3.2 V: low only after it has stayed there
  for (int i = 0; i < 40; i++, t += 1000) m.addSample(t, 3200);
  CHECK(m.low(t), "not low after 40 s at 3.2 V");

  // A brief dip (a load spike) must not trigger it
  battery::Monitor s(limits());
  uint32_t u = 0;
  for (; u < 10000; u += 1000) s.addSample(u, 3800);
  for (int i = 0; i < 3; i++, u += 1000) s.addSample(u, 3100);
  for (int i = 0; i < 20; i++, u += 1000) s.addSample(u, 3800);
  CHECK(!s.low(u), "low after a 3 s dip");

  // Recovering (e.g. USB plugged in and charging) clears it
  for (int i = 0; i < 20; i++, u += 1000) s.addSample(u, 4000);
  CHECK(!s.low(u), "low while charged");

  // Battery pulled out while low: not low any more
  m.addSample(t, 20);
  CHECK(!m.low(t + 60000), "low with no battery");

  // The hold time is measured with unsigned arithmetic, so it survives millis() wrapping
  battery::Monitor w(limits());
  uint32_t base = 0xFFFFFFFFu - 5000;
  for (uint32_t i = 0; i < 20; i++) w.addSample(base + i * 1000, 3200);
  CHECK(w.low(base + 20 * 1000), "not low across the millis() wrap");
  printf("  low battery: needs %u s below 3.30 V; spikes, recovery and no battery ignored\n", limits().lowHoldMs / 1000);
}

static void testSleepTimer() {
  const uint32_t IDLE = 20 * 60 * 1000, DEEP = 60 * 1000;
  SleepTimer s(IDLE, DEEP);
  s.begin(0);

  CHECK(s.update(IDLE - 1, false) == SleepAction::None, "slept early");
  CHECK(s.update(IDLE, false) == SleepAction::ScreenOff, "no screen-off at 20 min");
  CHECK(s.screenOff(), "screenOff flag");
  CHECK(s.update(IDLE + 1000, false) == SleepAction::None, "ScreenOff repeated");
  CHECK(s.update(IDLE + DEEP - 1, false) == SleepAction::None, "deep sleep early");
  CHECK(s.update(IDLE + DEEP, false) == SleepAction::DeepSleep, "no deep sleep after the extra minute");

  // A touch while the screen is off wakes it and restarts the clock
  SleepTimer a(IDLE, DEEP);
  a.begin(0);
  a.update(IDLE, false);
  CHECK(a.activity(IDLE + 5000) == true, "activity didn't report the wake");
  CHECK(!a.screenOff(), "still off after a touch");
  CHECK(a.update(IDLE + 5000 + IDLE - 1, false) == SleepAction::None, "slept again too soon");
  CHECK(a.update(IDLE + 5000 + IDLE, false) == SleepAction::ScreenOff, "didn't go off again");
  CHECK(a.activity(IDLE + 5000 + IDLE + 1) == true, "second wake");
  CHECK(a.activity(IDLE + 5000 + IDLE + 2) == false, "activity on a lit screen reported a wake");

  // Moving counts as activity: never sleeps while a session is moving
  SleepTimer m(IDLE, DEEP);
  m.begin(0);
  SleepAction last = SleepAction::None;
  for (uint32_t t = 0; t < 3 * IDLE; t += 100) {
    SleepAction r = m.update(t, true);
    if (r != SleepAction::None) last = r;
  }
  CHECK(last == SleepAction::None && !m.screenOff(), "went to sleep while moving");

  // ...and the clock starts from the last movement
  CHECK(m.update(3 * IDLE - 100 + IDLE - 1, false) == SleepAction::None, "slept before 20 min of parking");
  CHECK(m.update(3 * IDLE - 100 + IDLE, false) == SleepAction::ScreenOff, "parked for 20 min but screen stayed on");

  // Moving again while the screen is off (e.g. rolled off) wakes it, and it won't deep sleep
  m.update(3 * IDLE + 2 * DEEP - 100 + IDLE, true);
  CHECK(!m.screenOff(), "still off after moving");

  // Works across the millis() wrap (49.7 days)
  SleepTimer w(IDLE, DEEP);
  uint32_t start = 0xFFFFFFFFu - 1000;
  w.begin(start);
  CHECK(w.update(start + IDLE - 1, false) == SleepAction::None, "wrap: early");
  CHECK(w.update(start + IDLE, false) == SleepAction::ScreenOff, "wrap: screen-off missed");
  CHECK(w.update(start + IDLE + DEEP, false) == SleepAction::DeepSleep, "wrap: deep sleep missed");
  printf("  sleep: backlight off at %u min parked, deep sleep %u s later; movement and touches reset it\n",
         IDLE / 60000, DEEP / 1000);
}

// A minimal ESP32-S3 app image: header, one segment header, then the app descriptor.
static std::vector<uint8_t> makeImage(size_t size, uint16_t chip = 0x0009, bool appDesc = true) {
  std::vector<uint8_t> b(size, 0);
  b[0] = 0xE9;
  b[1] = 5;  // segments
  b[12] = chip & 0xFF;
  b[13] = chip >> 8;
  if (appDesc) {
    uint32_t magic = 0xABCD5432;
    memcpy(&b[32], &magic, 4);
  }
  return b;
}

static void testFirmwareImage() {
  using firmware::Check;
  const size_t SLOT = 6553600;

  auto good = makeImage(800000);
  CHECK(firmware::check(good.data(), good.size(), good.size(), SLOT) == Check::Ok, "good image refused");

  CHECK(firmware::check(good.data(), good.size(), SLOT + 1, SLOT) == Check::TooBig, "oversize accepted");
  CHECK(firmware::check(good.data(), good.size(), SLOT, SLOT) == Check::Ok, "exactly slot-sized refused");

  auto tiny = makeImage(64);
  CHECK(firmware::check(tiny.data(), tiny.size(), tiny.size(), SLOT) == Check::TooShort, "tiny accepted");

  auto text = makeImage(4096);
  memcpy(text.data(), "Hello, this is not firmware", 27);
  CHECK(firmware::check(text.data(), text.size(), text.size(), SLOT) == Check::BadMagic, "text file accepted");

  auto s3 = makeImage(4096, 0x0000);  // original ESP32
  CHECK(firmware::check(s3.data(), s3.size(), s3.size(), SLOT) == Check::WrongChip, "wrong chip accepted");

  auto boot = makeImage(4096, 0x0009, false);  // bootloader / merged image: no app descriptor
  CHECK(firmware::check(boot.data(), boot.size(), boot.size(), SLOT) == Check::NotAnApp, "bootloader accepted");

  // Short read of the header
  CHECK(firmware::check(good.data(), 50, good.size(), SLOT) == Check::TooShort, "short read accepted");

  printf("  firmware file: good image accepted; text, bootloader, wrong chip, too big, too small refused\n");
}

int main() {
  printf("Power tests\n");
  testVoltageToPercent();
  testPresence();
  testSmoothing();
  testLow();
  testSleepTimer();
  testFirmwareImage();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
