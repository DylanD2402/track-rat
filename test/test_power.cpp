/**
 * Host-side tests for the battery maths, the auto-sleep decision, the BOOT
 * press (and its hold to switch off) and the firmware file check.
 *
 *   g++ -std=c++17 -Iinclude test/test_power.cpp -o /tmp/pw && /tmp/pw
 */

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>
#include "Battery.h"
#include "ButtonPress.h"
#include "FirmwareImage.h"
#include "SleepTimer.h"
#include "check.h"

// Same numbers as include/Config.h
static battery::Limits limits() { return {2500, 4230, 3300, 10000, 30, 5, 50, 100, 4150, 6, 15, 30000, 60000}; }

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

  // A battery pulled out while running: gone after a few seconds, not on the first odd reading
  battery::Monitor pulled(limits());
  for (int t = 0; t < 10; t++) pulled.addSample(t * 1000, 3700);
  CHECK(pulled.present(), "present");
  pulled.addSample(10000, 30);
  CHECK(pulled.present(), "one zero reading hid the gauge");
  for (int t = 11; t < 15; t++) pulled.addSample(t * 1000, 30);
  CHECK(!pulled.present() && pulled.percent() == -1, "still present after removal");

  // Isolated ADC glitches (a spike up, a dip to zero) never hide the gauge or restart the low timer
  battery::Monitor glitchy(limits());
  uint32_t gt = 0;
  for (int i = 0; i < 400; i++, gt += 1000) {
    int mv = 3250;  // low the whole time
    if (i % 17 == 5) mv = 4300;
    if (i % 23 == 7) mv = 0;
    glitchy.addSample(gt, mv);
  }
  CHECK(glitchy.present() && glitchy.low(gt), "isolated glitches reset presence or the low timer");
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
  for (int i = 0; i < 5; i++) m.addSample(t + i * 1000, 20);
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
static std::vector<uint8_t> makeImage(size_t size, uint16_t chip = 0x0009, bool appDesc = true,
                                      const char* project = "trackrat") {
  std::vector<uint8_t> b(size, 0);
  b[0] = 0xE9;
  b[1] = 5;  // segments
  b[12] = chip & 0xFF;
  b[13] = chip >> 8;
  if (appDesc) {
    uint32_t magic = 0xABCD5432;
    memcpy(&b[32], &magic, 4);
    if (size >= 112) memcpy(&b[80], project, strlen(project));  // esp_app_desc_t.project_name
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

  // Another program for the same chip: the project name must be exactly "trackrat"
  for (const char* name : {"arduino-lib-builder", "trackrat2", "trackra", "TrackRat", ""}) {
    auto other = makeImage(4096, 0x0009, true, name);
    CHECK(firmware::check(other.data(), other.size(), other.size(), SLOT) == Check::NotTrackRat,
          "project \"%s\" accepted", name);
  }

  // Short read of the header
  CHECK(firmware::check(good.data(), 50, good.size(), SLOT) == Check::TooShort, "short read accepted");

  printf("  firmware file: good image accepted; text, bootloader, wrong chip, too big, too small refused\n");
}

// Wi-Fi upload: the file comes in chunks of any size; nothing may reach flash until the first 128 bytes
// have passed the check, and everything after that must arrive in order, none lost or repeated.
struct GateRun {
  firmware::Check check;
  bool decided;
  std::vector<uint8_t> written;  // what would have gone to flash
};

static GateRun runGate(const std::vector<uint8_t>& file, size_t chunk, size_t slot) {
  firmware::UploadGate gate;
  gate.reset(slot);
  GateRun r = {};
  size_t pos = 0;
  do {
    size_t n = std::min(chunk, file.size() - pos);
    bool final = pos + n == file.size();
    const uint8_t* data = file.data() + pos;
    size_t len = n;
    if (!gate.decided()) {
      size_t used = gate.feed(data, len, final);
      data += used;
      len -= used;
      if (gate.ok()) r.written.insert(r.written.end(), gate.header(), gate.header() + gate.headerLen());
      CHECK(gate.decided() || len == 0, "gate left bytes behind before deciding");
    }
    if (gate.ok()) r.written.insert(r.written.end(), data, data + len);
    CHECK(gate.ok() || r.written.empty(), "bytes written before the check passed");
    pos += n;
  } while (pos < file.size());
  r.check = gate.outcome();
  r.decided = gate.decided();
  return r;
}

static void testUploadGate() {
  using firmware::Check;
  const size_t SLOT = 6553600;
  const size_t sizes[] = {1, 7, 127, 128, 129, 4096};

  auto good = makeImage(20000);
  for (size_t i = 112; i < good.size(); i++) good[i] = (uint8_t)(i * 31 + i / 256);  // a lost or repeated byte shows
  auto s3 = makeImage(20000, 0x0000);
  auto boot = makeImage(20000, 0x0009, false);
  auto text = makeImage(20000);
  memcpy(text.data(), "Hello, this is not firmware", 27);

  for (size_t c : sizes) {
    GateRun r = runGate(good, c, SLOT);
    CHECK(r.decided && r.check == Check::Ok, "good image refused with chunks of %u", (unsigned)c);
    CHECK(r.written == good, "good image changed on the way with chunks of %u (%u of %u bytes)", (unsigned)c,
          (unsigned)r.written.size(), (unsigned)good.size());

    r = runGate(boot, c, SLOT);
    CHECK(r.check == Check::NotAnApp && r.written.empty(), "bootloader let through with chunks of %u", (unsigned)c);
    r = runGate(s3, c, SLOT);
    CHECK(r.check == Check::WrongChip && r.written.empty(), "wrong chip let through with chunks of %u", (unsigned)c);
    r = runGate(text, c, SLOT);
    CHECK(r.check == Check::BadMagic && r.written.empty(), "text file let through with chunks of %u", (unsigned)c);
  }

  // Ends before 128 bytes: too short, nothing written, even when the last chunk is empty
  for (size_t c : sizes) {
    std::vector<uint8_t> shortFile(good.begin(), good.begin() + 100);
    GateRun r = runGate(shortFile, c, SLOT);
    CHECK(r.decided && r.check == Check::TooShort && r.written.empty(), "100-byte file with chunks of %u", (unsigned)c);
  }
  {
    firmware::UploadGate gate;
    gate.reset(SLOT);
    CHECK(gate.feed(good.data(), 60, false) == 60 && !gate.decided(), "60 bytes should be held");
    CHECK(gate.feed(good.data() + 60, 0, true) == 0 && gate.decided() && gate.outcome() == Check::TooShort,
          "empty last chunk should end the upload as too short");
  }

  // Exactly 128 bytes is enough to decide; the header is all there is to write
  {
    std::vector<uint8_t> tiny(good.begin(), good.begin() + 128);
    GateRun r = runGate(tiny, 7, SLOT);
    CHECK(r.check == Check::Ok && r.written == tiny, "128-byte image");
  }

  // No app slot (size 0): refused rather than started
  CHECK(runGate(good, 4096, 0).check == Check::TooBig, "upload accepted with no app slot");

  // A new upload starts clean
  firmware::UploadGate gate;
  gate.reset(SLOT);
  gate.feed(boot.data(), boot.size(), true);
  CHECK(gate.decided() && !gate.ok(), "bootloader should leave the gate refused");
  gate.reset(SLOT);
  CHECK(!gate.decided() && !gate.ok() && gate.headerLen() == 0, "reset should clear the gate");

  printf("  wi-fi upload: header held across chunks of 1..4096 bytes; bootloader, wrong chip, short file refused\n");
}

// ---------------------------------------------------------------------------
// Chunked copy: the same loop the SD installer runs, with a fake file and fake flash
// ---------------------------------------------------------------------------
struct FakeFlash {
  std::vector<uint8_t> data;
  size_t failAfter = (size_t)-1;  // write fails once this many bytes are in
};

static void testCopyImage() {
  using firmware::Copy;
  std::vector<uint8_t> src(10000);
  for (size_t i = 0; i < src.size(); i++) src[i] = (uint8_t)(i * 31 + 7);
  uint8_t buf[4096];

  auto run = [&](size_t total, FakeFlash& fl, size_t readLimit, std::vector<size_t>* marks) {
    size_t pos = 0;
    return firmware::copyImage(
        total, buf, sizeof(buf),
        [&](uint8_t* b, size_t want) -> size_t {
          size_t n = want;
          if (pos + n > readLimit) n = readLimit > pos ? readLimit - pos : 0;  // the card ends early / is pulled
          memcpy(b, src.data() + pos, n);
          pos += n;
          return n;
        },
        [&](const uint8_t* b, size_t n) {
          if (fl.data.size() + n > fl.failAfter) return false;
          fl.data.insert(fl.data.end(), b, b + n);
          return true;
        },
        [&](size_t done, size_t) {
          if (marks) marks->push_back(done);
        });
  };

  FakeFlash ok;
  std::vector<size_t> marks;
  CHECK(run(src.size(), ok, src.size(), &marks) == Copy::Ok, "copy failed");
  CHECK(ok.data == src, "copied bytes differ");
  CHECK(marks.front() == 0 && marks.back() == src.size(), "progress didn't run 0 to total");
  bool rising = true;
  for (size_t i = 1; i < marks.size(); i++)
    if (marks[i] <= marks[i - 1]) rising = false;
  CHECK(rising, "progress not strictly rising");
  CHECK(marks.size() == 4, "progress calls %zu (want 0, 4096, 8192, 10000)", marks.size());

  FakeFlash exact;  // exact multiple of the buffer
  CHECK(run(8192, exact, 8192, nullptr) == Copy::Ok && exact.data.size() == 8192, "exact multiple");

  FakeFlash none;  // nothing to copy
  CHECK(run(0, none, 0, nullptr) == Copy::Ok && none.data.empty(), "empty copy");

  FakeFlash cut;  // card pulled half way
  CHECK(run(src.size(), cut, 5000, nullptr) == Copy::ReadError, "short file not reported");

  FakeFlash bad;  // flash write fails in the second chunk
  bad.failAfter = 4096;
  CHECK(run(src.size(), bad, src.size(), nullptr) == Copy::WriteError, "write failure not reported");

  // A read that claims more than was asked for is an error, not a buffer overrun
  FakeFlash over;
  Copy r = firmware::copyImage(
      100, buf, sizeof(buf), [&](uint8_t*, size_t want) -> size_t { return want + 1; },
      [&](const uint8_t*, size_t) { return true; }, [&](size_t, size_t) {});
  CHECK(r == Copy::ReadError, "over-long read accepted");
  printf("  installer copy loop: complete, empty, card pulled, flash error and over-read all handled\n");
}

// ---------------------------------------------------------------------------
// End to end: the same calls main.cpp makes, driven by a synthetic day
// ---------------------------------------------------------------------------

// Noise generator so the tests don't depend on <random>
static uint32_t rngState = 12345;
static int noise(int amplitude) {
  rngState = rngState * 1664525u + 1013904223u;
  return (int)((rngState >> 8) % (2 * amplitude + 1)) - amplitude;
}

static void testDrivingDay() {
  const uint32_t IDLE = 20 * 60 * 1000, DEEP = 60 * 1000;
  SleepTimer sleep(IDLE, DEEP);
  battery::Monitor bat(limits());
  sleep.begin(0);

  uint32_t drivingUntil = 30 * 60 * 1000;  // 30 min session, then parked
  uint32_t touchAt = drivingUntil + 5 * 60 * 1000;  // 5 min after parking the driver pokes the screen
  uint32_t screenOffAt = 0, deepAt = 0;
  bool touchedScreenWasOff = true;
  int lowAt = -1, minPercent = 101;
  int prevPercent = 101;
  bool percentOnlyFell = true;
  bool slept = false;

  uint32_t t = 0;
  for (; t < 3 * 60 * 60 * 1000 && !slept; t += 100) {
    bool movingFix = t < drivingUntil && (t % 100 == 0);  // 10 Hz fixes at 95 km/h while driving
    if (movingFix) sleep.activity(t);
    if (t == touchAt) touchedScreenWasOff = sleep.activity(t);  // a touch

    // Battery: 4.15 V falling to 3.25 V over the three hours, with ADC noise; one reading a second
    if (t % 1000 == 0) {
      int mv = 4150 - (int)((900ull * t) / (3ull * 60 * 60 * 1000)) + noise(25);
      bat.addSample(t, mv);
      if (bat.present()) {
        int p = bat.percent();
        if (p > prevPercent + 3) percentOnlyFell = false;  // the curve is steep in places: allow a little jitter
        prevPercent = p;
        minPercent = p < minPercent ? p : minPercent;
      }
      if (bat.low(t) && lowAt < 0) lowAt = (int)(t / 60000);
    }

    SleepAction a = sleep.update(t, false);
    if (a == SleepAction::ScreenOff && !screenOffAt) screenOffAt = t;
    if (a == SleepAction::DeepSleep) {
      deepAt = t;
      slept = true;
    }
  }

  // Parked at 30 min, touched at 35 min: the touch restarts the 20 minutes
  CHECK(!touchedScreenWasOff, "screen was off 5 min after parking");
  CHECK(screenOffAt == touchAt + IDLE, "screen off at %u s, want %u s", screenOffAt / 1000, (touchAt + IDLE) / 1000);
  CHECK(deepAt == touchAt + IDLE + DEEP, "deep sleep at %u s, want %u s", deepAt / 1000,
        (touchAt + IDLE + DEEP) / 1000);
  CHECK(screenOffAt > drivingUntil + IDLE, "slept before 20 min of parking");
  // 55 min in the battery is still around 4.0 V: no low-battery sleep, and the gauge only fell
  CHECK(lowAt < 0, "low battery fired at %d min with a 4.0 V battery", lowAt);
  CHECK(percentOnlyFell, "percentage rose while the battery drained");
  CHECK(minPercent > 50, "percentage fell to %d%% by the time it slept", minPercent);
  printf("  synthetic day: 30 min drive, touch at 35 min, backlight off at %u min, deep sleep at %u min 1 s\n",
         screenOffAt / 60000, deepAt / 60000);
}

static void testDrainToLow() {
  battery::Monitor bat(limits());
  int lowAtMv = 0, lowAtMin = -1;
  int prev = 101;
  bool monotonic = true;
  // 4.2 V to 3.0 V over 4 hours, noisy; the unit should call low once it has been at or under 3.30 V for 10 s
  const uint32_t total = 4 * 60 * 60 * 1000;
  for (uint32_t t = 0; t < total; t += 1000) {
    int trueMv = 4200 - (int)((1200ull * t) / total);
    bat.addSample(t, trueMv + noise(20));
    if (bat.present()) {
      if (bat.percent() > prev + 3) monotonic = false;
      prev = bat.percent();
    }
    if (bat.low(t)) {
      lowAtMv = trueMv;
      lowAtMin = (int)(t / 60000);
      break;
    }
  }
  CHECK(lowAtMin > 0, "never reported low");
  CHECK(lowAtMv <= 3320 && lowAtMv >= 3250, "low reported at true %d mV", lowAtMv);
  CHECK(monotonic, "percentage rose during discharge");
  printf("  discharge: low reported at about %d mV, %d min in\n", lowAtMv, lowAtMin);
}

static void testNoisyAndFloating() {
  // A healthy battery with +/-40 mV of ADC noise: always present, never low, no flicker
  battery::Monitor h(limits());
  int transitions = 0;
  bool was = false;
  bool everLow = false;
  for (uint32_t t = 0; t < 600000; t += 1000) {
    h.addSample(t, 3850 + noise(40));
    if (h.present() != was) transitions++;
    was = h.present();
    if (h.low(t)) everLow = true;
  }
  CHECK(transitions == 1 && !everLow, "healthy battery: %d presence changes, low=%d", transitions, everLow);

  // Empty socket on USB: the charger's float voltage wobbling either side of the upper limit must not
  // make the gauge flicker on and off
  battery::Monitor f(limits());
  transitions = 0;
  was = false;
  for (uint32_t t = 0; t < 600000; t += 1000) {
    f.addSample(t, 4240 + noise(35));  // 4.205 to 4.275 V
    if (f.present() != was) transitions++;
    was = f.present();
  }
  CHECK(transitions <= 2, "floating charger: gauge changed %d times in 10 min", transitions);

  // Pin shorted / floating at the ADC floor, and ADC reading garbage high
  battery::Monitor z(limits());
  for (uint32_t t = 0; t < 60000; t += 1000) z.addSample(t, 0);
  CHECK(!z.present() && !z.low(60000), "zero reading");
  battery::Monitor g(limits());
  for (uint32_t t = 0; t < 60000; t += 1000) g.addSample(t, 9000);
  CHECK(!g.present() && !g.low(60000), "huge reading");

  // USB plugged in while the battery is low and the unit hasn't slept yet: charging pulls the reading up
  battery::Monitor c(limits());
  uint32_t t = 0;
  for (; t < 8000; t += 1000) c.addSample(t, 3250);  // low for 8 s
  for (; t < 30000; t += 1000) c.addSample(t, 3600);  // USB in, charging
  CHECK(!c.low(t), "low still reported after charging started");
  printf("  noisy readings, floating charger, garbage readings and charging all behave\n");
}

// ---------------------------------------------------------------------------
// Charging: found from the voltage alone, and a shown percentage that moves like a phone's
// ---------------------------------------------------------------------------

// Feeds `seconds` readings 1 s apart at `mv` (+/- noise); t advances.
static void feed(battery::Monitor& m, uint32_t& t, int seconds, int mv, int noiseMv = 0) {
  for (int i = 0; i < seconds; i++, t += 1000) m.addSample(t, mv + (noiseMv ? noise(noiseMv) : 0));
}

static void testChargingSteps() {
  battery::Monitor m(limits());
  uint32_t t = 0;
  feed(m, t, 60, 3800, 10);
  CHECK(!m.charging(), "charging with no charger");
  const int before = m.percent();
  CHECK(before >= 30 && before <= 45, "shown %d%% at 3.8 V", before);

  // USB plugged in: the charger lifts the reading by 150 mV
  feed(m, t, 1, 3950);
  CHECK(!m.charging(), "one high reading is not a plug-in");
  feed(m, t, 1, 3950);
  CHECK(m.charging(), "two readings 150 mV up did not mean charging");
  CHECK(m.millivolts() >= 3945, "average did not snap to the new level: %d mV", m.millivolts());
  CHECK(m.percent() == before, "shown %d%% jumped from %d%% on plug-in", m.percent(), before);
  // The lift is taken off: the shown figure doesn't climb to the 3.95 V value (70%)
  feed(m, t, 600, 3950, 8);
  CHECK(m.charging(), "charging dropped under noise");
  CHECK(m.percent() >= before && m.percent() <= before + 10, "shown %d%% after 10 min (from %d%%)", m.percent(), before);

  // Unplugged: the reading falls back 150 mV
  const int charged = m.percent();
  feed(m, t, 1, 3800);
  CHECK(m.charging(), "one low reading is not an unplug");
  feed(m, t, 1, 3800);
  CHECK(!m.charging(), "two readings 150 mV down did not clear charging");
  CHECK(m.millivolts() <= 3810, "average did not snap down: %d mV", m.millivolts());
  CHECK(m.percent() == charged, "shown %d%% dropped from %d%% on unplug", m.percent(), charged);
  feed(m, t, 600, 3800, 8);
  CHECK(!m.charging(), "charging came back with no charger");
  CHECK(m.percent() <= charged && m.percent() >= charged - 10, "shown %d%% after unplug (from %d%%)", m.percent(), charged);

  // A load step while not charging (a 70 mV dip) does not snap the average or change the state
  battery::Monitor d(limits());
  t = 0;
  feed(d, t, 60, 3800);
  feed(d, t, 2, 3730);
  CHECK(!d.charging() && d.millivolts() > 3780, "a load dip changed things: %d mV", d.millivolts());
  printf("  charging: plug-in and unplug steps found; shown %d%% -> %d%% -> %d%% without a jump\n", before, charged,
         m.percent());
}

static void testChargingNoise() {
  // +/-15 mV of ADC noise (and bigger bursts, one at a time) for an hour: never flips the state
  for (int level : {3700, 3850, 4100}) {
    battery::Monitor m(limits());
    uint32_t t = 0;
    int flips = 0;
    bool was = false;
    for (int i = 0; i < 3600; i++, t += 1000) {
      int mv = level + noise(15);
      if (i % 50 == 7) mv += 45;  // lone spikes, never two in a row
      m.addSample(t, mv);
      if (m.charging() != was) flips++;
      was = m.charging();
    }
    CHECK(flips == 0, "noise at %d mV flipped charging %d times", level, flips);
  }
  // Charging state also survives noise
  battery::Monitor c(limits());
  uint32_t t = 0;
  feed(c, t, 30, 3800, 12);
  feed(c, t, 3, 3950, 12);
  CHECK(c.charging(), "plug-in missed under noise");
  int flips = 0;
  for (int i = 0; i < 3600; i++, t += 1000) {
    c.addSample(t, 3950 + noise(15));
    if (!c.charging()) flips++;
  }
  CHECK(flips == 0, "charging cleared %d times by noise", flips);
  printf("  charging: an hour of +/-15 mV noise flips nothing\n");
}

static void testChargingTrend() {
  // USB already in at boot: no step, just a slow rise as the charger fills the cell
  battery::Monitor m(limits());
  uint32_t t = 0;
  feed(m, t, 5, 3800);
  const int start = m.percent();
  CHECK(!m.charging(), "charging with nothing to go on");
  for (int i = 0; i < 400 && !m.charging(); i++, t += 1000) m.addSample(t, 3800 + i / 4 + noise(10));  // +15 mV/min
  CHECK(m.charging(), "a 15 mV/min rise never counted as charging");
  CHECK(t < 400000, "took too long: %u s", t / 1000);
  CHECK(m.percent() >= start, "shown fell on a trend detection");
  // A slow fall clears it again
  for (int i = 0; i < 600 && m.charging(); i++, t += 1000) m.addSample(t, 3900 - i / 4 + noise(10));
  CHECK(!m.charging(), "a slow fall never cleared charging");

  // USB in at boot with the cell nearly full: the charger lifts it only about 2 mV a minute
  battery::Monitor n(limits());
  t = 0;
  for (int i = 0; i < 900 && !n.charging(); i++, t += 1000) n.addSample(t, 4085 + i / 30 + noise(5));
  CHECK(n.charging(), "a 2 mV/min rise near full never counted as charging");
  CHECK(t <= 420000, "near full took too long: %u s", t / 1000);

  // The very slow drain of a normal discharge never counts as a rise or fall
  battery::Monitor d(limits());
  t = 0;
  for (int i = 0; i < 7200; i++, t += 1000) d.addSample(t, 3900 - i / 20 + noise(15));
  CHECK(!d.charging(), "a normal discharge looked like charging");

  // Near full the charger may have finished: a slow fall there does not clear charging
  battery::Monitor f(limits());
  t = 0;
  feed(f, t, 30, 4000, 5);
  feed(f, t, 3, 4180, 5);
  CHECK(f.charging(), "plug-in near full missed");
  for (int i = 0; i < 1200; i++, t += 1000) f.addSample(t, 4190 - i / 40);
  CHECK(f.charging(), "charging cleared by a slow fall at %d mV", f.millivolts());
  CHECK(f.percent() >= 90, "full charger shows %d%%", f.percent());
  printf("  charging: a slow rise sets it, a slow fall clears it, normal discharge and noise do not\n");
}

static void testShownPercent() {
  // First reading shows straight away, then it moves at most 1 % per 60 s while discharging
  battery::Monitor m(limits());
  uint32_t t = 0;
  m.addSample(t, 3950);
  CHECK(m.percent() == battery::percentFromMv(3950), "first reading %d%%", m.percent());
  t += 1000;
  // Voltage falls 3950 -> 3750 in 10 s (a heavy load): the shown figure only creeps
  int prev = m.percent();
  int maxDrop = 0;
  uint32_t lastChange = 0;
  bool slowEnough = true, onlyDown = true;
  for (int i = 0; i < 600; i++, t += 1000) {
    m.addSample(t, i < 10 ? 3950 - i * 20 : 3750);
    int p = m.percent();
    if (p > prev) onlyDown = false;
    if (p < prev) {
      if (prev - p > 1 || (lastChange && t - lastChange < 60000)) slowEnough = false;
      lastChange = t;
      maxDrop += prev - p;
    }
    prev = p;
  }
  CHECK(onlyDown, "shown percentage rose while discharging");
  CHECK(slowEnough, "shown percentage fell faster than 1%% per minute");
  CHECK(maxDrop >= 8 && maxDrop <= 10, "fell %d%% in 10 min", maxDrop);

  // Charging: only up, at most 1 % per 30 s, and it keeps climbing towards the resting value
  battery::Monitor c(limits());
  t = 0;
  feed(c, t, 30, 3700);
  feed(c, t, 2, 3850);  // plug-in, 150 mV lift: resting ~3700 = ~15 %
  CHECK(c.charging(), "no charging");
  prev = c.percent();
  lastChange = 0;
  bool up = true, rate = true;
  for (int i = 0; i < 3600; i++, t += 1000) {
    c.addSample(t, 3850 + i / 12);  // 5 mV/min lift of the real cell
    int p = c.percent();
    if (p < prev) up = false;
    if (p > prev) {
      if (p - prev > 1 || (lastChange && t - lastChange < 30000)) rate = false;
      lastChange = t;
    }
    prev = p;
  }
  CHECK(up, "shown percentage fell while charging");
  CHECK(rate, "shown percentage rose faster than 1%% per 30 s");
  CHECK(c.percent() > 30, "shown only %d%% after an hour of charging", c.percent());
  printf("  shown percent: 1%%/60 s down, 1%%/30 s up, never the wrong way\n");
}

static void testChargingLowAndAbsent() {
  // low() follows the real voltage: a shown percent held up by the rate limit doesn't hide a flat battery
  battery::Monitor m(limits());
  uint32_t t = 0;
  feed(m, t, 30, 3800);
  feed(m, t, 40, 3200);
  CHECK(m.low(t), "low() not set at 3.2 V (shown %d%%)", m.percent());
  CHECK(m.percent() > 20, "shown %d%% fell with the voltage instead of lagging", m.percent());

  // Charging plugged in at 3.2 V: the snap up clears low as before
  feed(m, t, 3, 3450);
  CHECK(m.charging() && !m.low(t), "plugging in did not clear low");

  // Battery pulled: everything starts again
  battery::Monitor a(limits());
  t = 0;
  feed(a, t, 30, 3700);
  feed(a, t, 3, 3850);
  CHECK(a.charging(), "setup");
  feed(a, t, 10, 20);
  CHECK(!a.present() && !a.charging() && a.percent() == -1, "absent battery still charging/present");
  feed(a, t, 20, 3900);
  CHECK(a.present() && !a.charging(), "charging survived a battery swap");
  CHECK(a.percent() == battery::percentFromMv(3900), "shown %d%% did not restart from the voltage", a.percent());
  printf("  charging: low() follows the real voltage; a pulled battery resets everything\n");
}

// Same numbers as include/Config.h: LONG_PRESS_MS, POWER_OFF_BAR_MS, POWER_OFF_HOLD_MS
static const uint32_t LONG_MS = 1000, BAR_MS = 2000, OFF_MS = 3000;

// Holds the button from `from` to `to` (polled every 20 ms like InputManager), counting what it gives
struct PressLog {
  int shorts = 0, longs = 0;
  uint32_t longAt = 0, offAt = 0;  // when Long came, and when the hold reached OFF_MS (0: never)
};
static PressLog press(ButtonPress& b, uint32_t from, uint32_t to, uint32_t until) {
  PressLog log;
  for (uint32_t t = from; t <= until; t += 20) {
    const ButtonPress::Event e = b.update(t >= from && t < to, t);
    if (e == ButtonPress::Event::Short) log.shorts++;
    if (e == ButtonPress::Event::Long) log.longs++, log.longAt = t;
    if (!log.offAt && b.heldMs(t) >= OFF_MS) log.offAt = t;
  }
  return log;
}

static void testButtonPress() {
  // BOOT: its long press comes when it's let go, between 1 s and the power-off bar
  {  // a tap: Short when it's let go, and never long enough to switch off
    ButtonPress b(LONG_MS, BAR_MS);
    b.begin(false, 0);
    const PressLog p = press(b, 100, 300, 2000);
    CHECK(p.shorts == 1 && p.longs == 0 && !p.offAt, "tap: %d short %d long", p.shorts, p.longs);
    CHECK(b.heldMs(2000) == 0, "nothing held after it");
  }
  {  // contact bounce: under 30 ms gives nothing
    ButtonPress b(LONG_MS, BAR_MS);
    b.begin(false, 0);
    CHECK(b.update(true, 100) == ButtonPress::Event::None && b.update(false, 120) == ButtonPress::Event::None, "a 20 ms blip");
  }
  {  // held 1.5 s: nothing while held, Long when it's let go
    ButtonPress b(LONG_MS, BAR_MS);
    b.begin(false, 0);
    const PressLog p = press(b, 100, 1600, 4000);
    CHECK(p.longs == 1 && p.shorts == 0 && p.longAt == 1600 && !p.offAt, "1.5 s hold: long at %u ms, when let go", (unsigned)p.longAt);
  }
  {  // either side of the bar: let go at 1.9 s is a Long, at 2.1 s (the bar showing) nothing at all
    ButtonPress b(LONG_MS, BAR_MS);
    b.begin(false, 0);
    PressLog p = press(b, 100, 2000, 3000);
    CHECK(p.longs == 1 && p.shorts == 0, "let go at 1.9 s: %d long", p.longs);
    p = press(b, 3100, 5200, 6000);
    CHECK(p.longs == 0 && p.shorts == 0 && !p.offAt, "let go at 2.1 s: nothing (%d long %d short)", p.longs, p.shorts);
  }
  {  // held on to switch off: no Long on the way (on the menu it would start Free Roam), and none after
    ButtonPress b(LONG_MS, BAR_MS);
    b.begin(false, 0);
    const PressLog p = press(b, 100, 5000, 6000);
    CHECK(p.longs == 0 && p.shorts == 0 && p.offAt == 3100, "3 s hold: off at %u, %d long", (unsigned)p.offAt, p.longs);
    CHECK(b.heldMs(6000) == 0, "nothing held once let go");
    b.update(true, 7000);
    b.update(true, 9000);
    CHECK(b.heldMs(9500) == 2500, "held 2.5 s: %u", (unsigned)b.heldMs(9500));
  }
  {  // held through start-up (the press that woke the board): ignored until let go, then normal again
    ButtonPress b(LONG_MS, BAR_MS);
    b.begin(true, 0);
    PressLog p = press(b, 0, 4000, 5000);
    CHECK(p.shorts == 0 && p.longs == 0 && !p.offAt, "the waking press: %d short %d long, off at %u", p.shorts, p.longs, (unsigned)p.offAt);
    p = press(b, 5100, 5300, 6000);
    CHECK(p.shorts == 1, "the next press counts");
    b.begin(true, 7000);
    p = press(b, 7000, 8500, 9000);
    CHECK(p.shorts == 0 && p.longs == 0, "a waking press let go after 1.5 s: still nothing");
  }
  // The big button: its long press fires at 1 s, still held
  {
    ButtonPress b(LONG_MS);
    b.begin(false, 0);
    PressLog p = press(b, 100, 300, 1000);
    CHECK(p.shorts == 1 && p.longs == 0, "big button tap");
    p = press(b, 1100, 2600, 4000);
    CHECK(p.longs == 1 && p.shorts == 0 && p.longAt == 2100, "big button 1.5 s hold: long at %u ms, still held", (unsigned)p.longAt);
    p = press(b, 4100, 9000, 9500);
    CHECK(p.longs == 1 && p.longAt == 5100, "held on: one long, at 1 s");
  }
  {  // millis() wrapping during a hold
    ButtonPress b(LONG_MS);
    const uint32_t t0 = 0xFFFFFFFFu - 500;
    b.begin(false, t0);
    b.update(true, t0);
    CHECK(b.update(true, t0 + 1000) == ButtonPress::Event::Long && b.heldMs(t0 + 3000) == 3000, "a hold across the wrap");
    ButtonPress boot(LONG_MS, BAR_MS);
    boot.begin(false, t0);
    boot.update(true, t0);
    CHECK(boot.update(false, t0 + 1200) == ButtonPress::Event::Long, "a BOOT hold let go across the wrap");
  }
  printf("  BOOT press: tap, a hold acts when let go before 2 s, held on to switch off does nothing else; big button at 1 s\n");
}

int main() {
  printf("Power tests\n");
  testVoltageToPercent();
  testPresence();
  testSmoothing();
  testLow();
  testSleepTimer();
  testButtonPress();
  testFirmwareImage();
  testUploadGate();
  testCopyImage();
  testDrivingDay();
  testDrainToLow();
  testNoisyAndFloating();
  testChargingSteps();
  testChargingNoise();
  testChargingTrend();
  testShownPercent();
  testChargingLowAndAbsent();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
