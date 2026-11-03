/**
 * Host-side tests for what the battery has been through: the drain of each way of running, time left, charge
 * cycles, health from the charger's step, the full voltage, full runs, and corrupt saved data.
 *
 *   g++ -std=c++17 -Iinclude test/test_battery_stats.cpp -o /tmp/bs && /tmp/bs
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include "Battery.h"
#include "BatteryStats.h"
#include "check.h"

using namespace battery;

static Defaults defaults() { return {3000, {180, 260, 90, 1}}; }
// Same numbers as include/Config.h
static Limits limits() { return {2500, 4230, 3300, 10000, 30, 5, 30, 100, 6, 15, 10, 3, 1, 5, 30000, 60000}; }

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// A fresh Stats that has got past the 5 minutes it ignores at the start, so the next minute sets the baseline
static void warmUp(Stats& s, Mode mode, int mv) {
  for (int i = 0; i < 6; i++) s.addMinute(mode, mv, false, 50);
}

static void testPercentX100() {
  CHECK(percentFromMvX100(4200) == 10000 && percentFromMvX100(4300) == 10000, "full");
  CHECK(percentFromMvX100(3300) == 0 && percentFromMvX100(3000) == 0, "empty");
  CHECK(percentFromMvX100(3840) == 5000, "3.84 V = %d", percentFromMvX100(3840));
  CHECK(percentFromMvX100(3830) == 4750, "3.83 V = %d", percentFromMvX100(3830));
  CHECK(percentFromMvX100(3838) == 4950 && percentFromMvX100(3802) == 4050, "3838 %d, 3802 %d", percentFromMvX100(3838),
        percentFromMvX100(3802));
  bool agrees = true, monotonic = true;
  int prev = -1;
  for (int mv = 3200; mv <= 4300; mv++) {
    const int p = percentFromMvX100(mv);
    if (p / 100 != percentFromMv(mv)) agrees = false;
    if (p < prev) monotonic = false;
    prev = p;
  }
  CHECK(agrees, "x100 and whole percent disagree");
  CHECK(monotonic, "x100 fell as voltage rose");
  printf("  percent x100: 3.838 V %d, 3.802 V %d\n", percentFromMvX100(3838), percentFromMvX100(3802));
}

static void testUsage() {
  // 0.4 mV a minute is 6 % an hour in the 3820-3840 mV and 3800-3820 mV stretches (25 hundredths of a percent a mV)
  Stats s(defaults());
  CHECK(!s.measured(Mode::ScreenOn) && !s.dirty(), "fresh");
  for (int t = 0; t <= 95; t++) s.addMinute(Mode::ScreenOn, 3840 - (t * 2) / 5, false, 80);
  // minutes 0-4 ignored (just started), 5 sets the baseline (3838), 6..95 are 90 minutes down to 3802
  const StatsData& d = s.data();
  CHECK(d.secs[0] == 5400 && d.dropX100[0] == 900, "ScreenOn: %u s, %d", (unsigned)d.secs[0], (int)d.dropX100[0]);
  CHECK(d.secs[1] == 0 && d.secs[2] == 0 && d.secs[3] == 0, "other modes untouched");
  CHECK(s.measured(Mode::ScreenOn) && s.dirty(), "measured after 90 minutes and 9 %%");
  Report r = s.report(Mode::ScreenOn, 80, false);
  CHECK(near(r.usage[0].pctPerHour, 6.0f, 0.01f), "6 %%/h: %.3f", r.usage[0].pctPerHour);
  CHECK(near(r.usage[0].milliamps, 180.0f, 0.5f), "180 mA: %.2f", r.usage[0].milliamps);
  CHECK(r.usage[0].measured && !r.usage[1].measured && !r.usage[2].measured && !r.usage[3].measured, "only ScreenOn measured");
  CHECK(near(r.usage[1].milliamps, 260, 0.01f) && near(r.usage[2].milliamps, 90, 0.01f) && near(r.usage[3].milliamps, 1, 0.01f),
        "the others show the defaults");
  CHECK(near(r.usage[2].pctPerHour, 3.0f, 0.001f), "90 mA = 3 %%/h: %.3f", r.usage[2].pctPerHour);
  CHECK(near(r.cycles, 0.09f, 0.0001f), "cycles %.4f", r.cycles);
  s.saved();
  CHECK(!s.dirty(), "saved");

  // Time left: percent / rate of the mode it is in now
  CHECK(r.haveTimeLeft && !r.timeLeftEstimate && near(r.hoursLeft, 80.0f / 6.0f, 0.01f), "time left %.2f h", r.hoursLeft);
  r = s.report(Mode::ScreenOff, 60, false);  // not measured: 90 mA = 3 %/h
  CHECK(r.haveTimeLeft && r.timeLeftEstimate && near(r.hoursLeft, 20.0f, 0.01f), "estimate %.2f h", r.hoursLeft);
  r = s.report(Mode::ScreenOn, 80, true);
  CHECK(!r.haveTimeLeft, "no time left while charging");
  r = s.report(Mode::ScreenOn, -1, false);
  CHECK(!r.haveTimeLeft, "no time left with no battery");
  printf("  usage: %.1f %%/h = %.0f mA, time left %.1f h\n", s.report(Mode::ScreenOn, 80, false).usage[0].pctPerHour,
         s.report(Mode::ScreenOn, 80, false).usage[0].milliamps, s.report(Mode::ScreenOn, 80, false).hoursLeft);

  // Not enough yet: 20 minutes of data is under the 30 minutes needed
  Stats few(defaults());
  for (int t = 0; t <= 24; t++) few.addMinute(Mode::ScreenOn, 3840 - (t * 2) / 5, false, 80);
  CHECK(few.data().secs[0] == 1140 && !few.measured(Mode::ScreenOn), "19 minutes: %u s, measured %d", (unsigned)few.data().secs[0],
        few.measured(Mode::ScreenOn));
  // Enough time but under 2 %: flat voltage for 2 hours
  Stats flatS(defaults());
  for (int t = 0; t < 130; t++) flatS.addMinute(Mode::ScreenOn, 3800, false, 50);
  CHECK(flatS.data().secs[0] == 124 * 60 && !flatS.measured(Mode::ScreenOn), "flat: not measured, %u s", (unsigned)flatS.data().secs[0]);
  CHECK(flatS.report(Mode::ScreenOn, 50, false).usage[0].milliamps == 180.0f, "flat shows the default");
}

static void testIgnored() {
  // After charging, the first 5 minutes are the cell relaxing, which looks like fast drain
  Stats s(defaults());
  for (int i = 0; i < 3; i++) s.addMinute(Mode::ScreenOn, 3900, true, 50);
  const int relaxing[] = {3890, 3880, 3870, 3860, 3850};
  for (int mv : relaxing) s.addMinute(Mode::ScreenOn, mv, false, 50);
  for (int i = 0; i < 11; i++) s.addMinute(Mode::ScreenOn, 3850, false, 50);  // the first sets the baseline
  CHECK(s.data().secs[0] == 600 && s.data().dropX100[0] == 0, "relaxing ignored: %u s, %d", (unsigned)s.data().secs[0],
        (int)s.data().dropX100[0]);

  // A minute that rose (a load dropped) is a recovery, not drain; the next minutes carry on from the new level
  Stats r(defaults());
  warmUp(r, Mode::ScreenOn, 3800);
  r.addMinute(Mode::ScreenOn, 3800, false, 50);  // +60 s, no drop
  r.addMinute(Mode::ScreenOn, 3820, false, 50);  // rose 20 mV: ignored
  r.addMinute(Mode::ScreenOn, 3820, false, 50);  // +60 s
  r.addMinute(Mode::ScreenOn, 3815, false, 50);  // 4500 - 4375 = 125, +60 s
  CHECK(r.data().secs[0] == 180 && r.data().dropX100[0] == 125, "recovery: %u s, %d", (unsigned)r.data().secs[0],
        (int)r.data().dropX100[0]);
  // A rise of 1-3 mV is noise and is counted, so the noise cancels instead of only the falls being counted
  Stats n(defaults());
  warmUp(n, Mode::ScreenOn, 3800);
  n.addMinute(Mode::ScreenOn, 3802, false, 50);
  n.addMinute(Mode::ScreenOn, 3800, false, 50);
  CHECK(n.data().secs[0] == 120 && n.data().dropX100[0] == 0, "noise cancels: %u s, %d", (unsigned)n.data().secs[0], (int)n.data().dropX100[0]);

  // The minute the mode changes carries the sag (or the recovery) of the change: ignored
  Stats m(defaults());
  warmUp(m, Mode::ScreenOn, 3800);
  m.addMinute(Mode::ScreenOnWifi, 3760, false, 50);  // Wi-Fi on: sags 40 mV
  m.addMinute(Mode::ScreenOnWifi, 3760, false, 50);
  m.addMinute(Mode::ScreenOnWifi, 3759, false, 50);  // 2750 - 2725
  CHECK(m.data().secs[0] == 0 && m.data().secs[1] == 120 && m.data().dropX100[1] == 25, "mode change: %u s, %u s, %d",
        (unsigned)m.data().secs[0], (unsigned)m.data().secs[1], (int)m.data().dropX100[1]);

  // A cell held up by a finished charger (4175 mV or more) is not drain
  Stats f(defaults());
  warmUp(f, Mode::ScreenOn, 3800);
  for (int i = 0; i < 20; i++) f.addMinute(Mode::ScreenOn, 4190, false, 100);
  CHECK(f.data().secs[0] == 0, "held up at full counts for nothing");

  // Glitches and sleeping are not a minute of use
  Stats g(defaults());
  g.addMinute(Mode::ScreenOn, 0, false, 50);
  g.addMinute(Mode::ScreenOn, 5000, false, 50);
  g.addMinute(Mode::Asleep, 3800, false, 50);
  CHECK(!g.dirty() && g.data().secs[0] == 0, "glitches ignored");
  // 5 % in a minute is not drain
  Stats big(defaults());
  warmUp(big, Mode::ScreenOn, 3900);
  big.addMinute(Mode::ScreenOn, 3700, false, 50);
  CHECK(big.data().secs[0] == 0, "a huge drop is ignored");
  printf("  ignored: relaxing, recoveries, mode changes, held-up full, glitches; noise cancels\n");
}

static void testMinuteMeter() {
  MinuteMeter m;
  bool done = false;
  for (int i = 0; i < 59; i++) done = done || m.add(3800, Mode::ScreenOn, false);
  CHECK(!done, "ready too early");
  CHECK(m.add(3800, Mode::ScreenOn, false) && m.meanMv() == 3800 && m.mode() == Mode::ScreenOn && !m.charging(), "a minute");
  // Half and half rounds up; the mode it mostly was in; any second with the charger in counts
  for (int i = 0; i < 59; i++) m.add(i % 2 ? 3800 : 3801, i < 40 ? Mode::ScreenOff : Mode::ScreenOn, i == 7);
  CHECK(m.add(3800, Mode::ScreenOff, false), "second minute");
  CHECK(m.mode() == Mode::ScreenOff && m.charging(), "mostly screen off, charger seen once");
  CHECK(m.meanMv() == 3800 || m.meanMv() == 3801, "mean %d", m.meanMv());
  // 30 x 3800 and 30 x 3801 = 3800.5 -> 3801
  MinuteMeter h;
  for (int i = 0; i < 30; i++) h.add(3800, Mode::ScreenOn, false);
  for (int i = 0; i < 29; i++) h.add(3801, Mode::ScreenOn, false);
  CHECK(h.add(3801, Mode::ScreenOn, false) && h.meanMv() == 3801, "rounding: %d", h.meanMv());
  // reset() drops a partly filled minute
  MinuteMeter r;
  for (int i = 0; i < 30; i++) r.add(3000, Mode::ScreenOn, true);
  r.reset();
  for (int i = 0; i < 59; i++) r.add(3800, Mode::ScreenOn, false);
  CHECK(r.add(3800, Mode::ScreenOn, false) && r.meanMv() == 3800 && !r.charging(), "reset");
}

static void testSleep() {
  Stats s(defaults());
  s.addSleep(8 * 3600, 3840, 3830);  // 5000 - 4750 = 250 over 8 h
  CHECK(s.data().secs[3] == 28800 && s.data().dropX100[3] == 250, "asleep: %u s, %d", (unsigned)s.data().secs[3], (int)s.data().dropX100[3]);
  CHECK(s.measured(Mode::Asleep), "8 h and 2.5 %% is enough");
  Report r = s.report(Mode::ScreenOn, 50, false);
  CHECK(near(r.usage[3].pctPerHour, 0.3125f, 0.001f) && near(r.usage[3].milliamps, 9.375f, 0.05f), "asleep %.4f %%/h, %.3f mA",
        r.usage[3].pctPerHour, r.usage[3].milliamps);
  CHECK(near(r.cycles, 0.025f, 0.0001f), "the sleep counts as discharge: %.4f", r.cycles);

  Stats shortS(defaults());  // under 30 minutes: not worth the voltage noise
  shortS.addSleep(1200, 3840, 3835);
  CHECK(shortS.data().secs[3] == 0 && shortS.data().dropX100[3] == 0, "20 minutes asleep not counted");

  Stats charged(defaults());  // the voltage jumped: it was charged meanwhile
  charged.addSleep(36000, 3800, 3900);
  CHECK(charged.data().secs[3] == 0 && charged.data().totalDropX100 == 0, "charged while asleep");

  Stats up(defaults());  // a few mV up is noise, and a negative drop is nothing
  up.addSleep(7200, 3840, 3845);
  CHECK(up.data().secs[3] == 0 && up.data().dropX100[3] == 0, "recovery asleep");

  Stats bad(defaults());
  bad.addSleep(0, 3800, 3790);
  bad.addSleep(7200, 0, 3790);
  bad.addSleep(7200, 3800, 9000);
  CHECK(!bad.dirty(), "nonsense voltages or times ignored");
  printf("  sleep: 8 h, 3.840 -> 3.830 V = %.2f %%/h, %.1f mA\n", r.usage[3].pctPerHour, r.usage[3].milliamps);
}

static void testCorrupt() {
  StatsData z;
  memset(&z, 0, sizeof(z));
  Stats s(defaults());
  s.load(z);
  Report r = s.report(Mode::ScreenOn, 70, false);
  CHECK(s.data().valid() && !r.usage[0].measured && r.cycles == 0 && r.health == Health::Learning && r.fullMv == 0,
        "zeroed data = nothing measured");

  uint32_t garbageOk = 0;
  testSeed(7);
  for (int round = 0; round < 200; round++) {
    StatsData g;
    uint8_t* p = (uint8_t*)&g;
    for (size_t i = 0; i < sizeof(g); i++) p[i] = (uint8_t)testRand();
    Stats t(defaults());
    t.load(g);
    const Report rr = t.report(Mode::ScreenOn, 70, false);
    if (t.data().valid() && !rr.usage[0].measured && rr.cycles == 0) garbageOk++;
  }
  CHECK(garbageOk == 200, "garbage gave nonsense: %u of 200 clean", (unsigned)garbageOk);

  // Real data with one bit wrong is thrown away
  Stats used(defaults());
  for (int t = 0; t <= 95; t++) used.addMinute(Mode::ScreenOn, 3840 - (t * 2) / 5, false, 80);
  StatsData d = used.data();
  Stats back(defaults());
  back.load(d);
  CHECK(back.measured(Mode::ScreenOn) && back.data().dropX100[0] == 900, "saved data loads back");
  d.secs[0] ^= 0x100;
  back.load(d);
  CHECK(!back.measured(Mode::ScreenOn) && back.data().secs[0] == 0, "one flipped bit and it is discarded");

  // Well-formed (checksum right) but impossible
  StatsData e;
  e.reset();
  e.fullMv = 5000;
  e.seal();
  CHECK(!e.valid(), "full voltage out of range");
  e.reset();
  e.dropX100[0] = 2000000000;
  e.seal();
  CHECK(!e.valid(), "drop with no time");
  e.reset();
  e.secs[0] = 3600;
  e.dropX100[0] = 400000;  // 40 % a minute
  e.seal();
  CHECK(!e.valid(), "drop faster than possible");
  e.reset();
  e.stepsTotal = 3;
  e.stepFirstCount = 3;
  e.stepFirst[0] = e.stepFirst[1] = 80;
  e.stepFirst[2] = 5000;
  e.seal();
  CHECK(!e.valid(), "a step of 5 V");
  e.reset();
  e.stepsTotal = 9;
  e.stepFirstCount = 2;
  e.seal();
  CHECK(!e.valid(), "step count disagrees");
  e.reset();
  e.lastMix[1] = 200;
  e.seal();
  CHECK(!e.valid(), "mix over 100 %%");
  e.reset();
  e.seal();
  CHECK(e.valid() && sizeof(StatsData) == 104, "a clean one is fine (%u bytes)", (unsigned)sizeof(StatsData));
  printf("  corrupt saved data: zeros, 200 random blocks, a flipped bit and 6 impossible ones all become 'not measured yet'\n");
}

static void testHealth() {
  Stats s(defaults());
  for (int i = 0; i < 4; i++) s.addStep(80, 3800);
  CHECK(s.report(Mode::ScreenOn, 50, false).health == Health::Learning && s.report(Mode::ScreenOn, 50, false).steps == 4, "4 steps: learning");
  s.addStep(80, 3800);
  Report r = s.report(Mode::ScreenOn, 50, false);
  CHECK(r.health == Health::Good && r.stepFirstMv == 80 && r.stepNowMv == 80, "5 steps: good, %d vs %d", r.stepNowMv, r.stepFirstMv);

  // Steps that don't count: near full (constant voltage phase), or out of range
  s.addStep(80, 4100);
  s.addStep(80, 4150);
  s.addStep(10, 3800);
  s.addStep(400, 3800);
  s.addStep(80, 2000);
  CHECK(s.report(Mode::ScreenOn, 50, false).steps == 5, "ignored steps: %d", s.report(Mode::ScreenOn, 50, false).steps);

  // The median ignores one odd step: the baseline of {70, 90, 80, 200, 60} is 80; the latest {100 x4, 300} is 100
  Stats m(defaults());
  const int first[] = {70, 90, 80, 200, 60};
  for (int v : first) m.addStep(v, 3700);
  const int later[] = {100, 100, 100, 100, 300};
  for (int v : later) m.addStep(v, 3700);
  r = m.report(Mode::ScreenOn, 50, false);
  CHECK(r.stepFirstMv == 80 && r.stepNowMv == 100 && r.health == Health::Good && r.steps == 10, "medians %d, %d, health %d",
        r.stepFirstMv, r.stepNowMv, (int)r.health);

  // Rise = latest / baseline: 110 / 80 = 1.37 is Fair, 140 / 80 = 1.75 is Replace
  Stats f(defaults());
  for (int i = 0; i < 5; i++) f.addStep(80, 3700);
  for (int i = 0; i < 5; i++) f.addStep(110, 3700);
  CHECK(f.report(Mode::ScreenOn, 50, false).health == Health::Fair, "rise 1.37 = fair");
  for (int i = 0; i < 5; i++) f.addStep(140, 3700);
  CHECK(f.report(Mode::ScreenOn, 50, false).health == Health::Replace, "rise 1.75 = replace");
  Stats g(defaults());
  for (int i = 0; i < 5; i++) g.addStep(100, 3700);
  for (int i = 0; i < 5; i++) g.addStep(129, 3700);
  CHECK(g.report(Mode::ScreenOn, 50, false).health == Health::Good, "rise 1.29 = still good");
  for (int i = 0; i < 5; i++) g.addStep(130, 3700);
  CHECK(g.report(Mode::ScreenOn, 50, false).health == Health::Fair, "rise 1.30 = fair");

  // Cycles count too: a clean step but 350 cycles is fair, 510 is replace
  StatsData d;
  d.reset();
  for (int i = 0; i < 5; i++) d.stepFirst[i] = d.stepRecent[i] = 80;
  d.stepFirstCount = 5;
  d.stepsTotal = 5;
  d.totalDropX100 = 3500000;
  d.seal();
  Stats c(defaults());
  c.load(d);
  r = c.report(Mode::ScreenOn, 50, false);
  CHECK(near(r.cycles, 350.0f, 0.01f) && r.health == Health::Fair, "350 cycles = fair (%d)", (int)r.health);
  d.totalDropX100 = 5100000;
  d.seal();
  c.load(d);
  CHECK(c.report(Mode::ScreenOn, 50, false).health == Health::Replace, "510 cycles = replace");
  d.totalDropX100 = 2990000;
  d.seal();
  c.load(d);
  CHECK(c.report(Mode::ScreenOn, 50, false).health == Health::Good, "299 cycles = good");
  printf("  health: learning until 5 steps, medians, rise 1.30 is fair, 1.60 and 500 cycles replace\n");
}

static void testFullVoltage() {
  // The charger holds it flat at 4195 for 10 minutes: 5 minutes later that is the full voltage
  Stats s(defaults());
  for (int i = 0; i < 14; i++) s.addMinute(Mode::ScreenOn, 4195, true, 99);
  CHECK(s.fullMv() == 0 && !s.takeFull(), "not yet at minute 14");
  s.addMinute(Mode::ScreenOn, 4195, true, 99);
  CHECK(s.fullMv() == 4195 && s.takeFull() && !s.takeFull(), "learned at minute 15: %d", s.fullMv());
  for (int i = 0; i < 30; i++) s.addMinute(Mode::ScreenOn, 4195, true, 99);
  CHECK(s.fullMv() == 4195 && !s.takeFull(), "once per charge");

  // The next charge is smoothed in: (4195 x 3 + 4185) / 4 = 4193
  s.addMinute(Mode::ScreenOn, 3900, false, 80);  // unplugged and used (this releases the latch)
  for (int i = 0; i < 15; i++) s.addMinute(Mode::ScreenOn, 4185, true, 99);
  CHECK(s.fullMv() == 4193 && s.takeFull(), "smoothed: %d", s.fullMv());

  // Still rising: no full voltage
  Stats rise(defaults());
  for (int i = 0; i < 30; i++) rise.addMinute(Mode::ScreenOn, 4100 + 4 * i, true, 90);
  CHECK(rise.fullMv() == 0, "still rising");
  // Flat but below 4.10 V (a pause, not the end)
  Stats low(defaults());
  for (int i = 0; i < 30; i++) low.addMinute(Mode::ScreenOn, 4050, true, 90);
  CHECK(low.fullMv() == 0, "flat at 4.05 V is not full");
  // Flat for only 9 minutes
  Stats nine(defaults());
  for (int i = 0; i < 9; i++) nine.addMinute(Mode::ScreenOn, 4195, true, 99);
  for (int i = 0; i < 3; i++) nine.addMinute(Mode::ScreenOn, 4195 + 10 * (i + 1), true, 99);
  CHECK(nine.fullMv() == 0, "9 minutes is not 10");

  // Charging cleared by a slow fall: the minute means 4190, 4188, ... settle at 4185 five minutes later
  Stats slow(defaults());
  for (int i = 0; i < 3; i++) slow.addMinute(Mode::ScreenOn, 4195, true, 99);
  const int fall[] = {4190, 4188, 4187, 4186, 4185, 4185};
  for (int mv : fall) {
    CHECK(slow.fullMv() == 0, "learned too soon");
    slow.addMinute(Mode::ScreenOn, mv, false, 99);
  }
  CHECK(slow.fullMv() == 4185 && slow.takeFull(), "slow fall: %d", slow.fullMv());

  // A real unplug (a 60 mV step down) is not the charger finishing
  Stats unplug(defaults());
  for (int i = 0; i < 3; i++) unplug.addMinute(Mode::ScreenOn, 4190, true, 99);
  for (int i = 0; i < 8; i++) unplug.addMinute(Mode::ScreenOn, 4130, false, 99);
  CHECK(unplug.fullMv() == 0, "unplugging is not full");

  // Still moving 5 minutes later: wait a few more minutes, then give up
  Stats moving(defaults());
  for (int i = 0; i < 10; i++) moving.addMinute(Mode::ScreenOn, 4195, true, 99);
  for (int mv = 4185; mv >= 4115; mv -= 10) moving.addMinute(Mode::ScreenOn, mv, true, 99);
  CHECK(moving.fullMv() == 0, "never settled");

  // Out of the believable range: dropped
  Stats away(defaults());
  for (int i = 0; i < 10; i++) away.addMinute(Mode::ScreenOn, 4195, true, 99);
  for (int i = 0; i < 8; i++) away.addMinute(Mode::ScreenOn, 4250, true, 99);
  CHECK(away.fullMv() == 0, "4.25 V is not a battery");
  printf("  full voltage: flat 10 min then +5 min, slow fall, smoothed, not from a step down or a rise\n");
}

static void testFullMapping() {
  CHECK(fullCorrectedMv(4170, 4170) == 4200, "full reads 4200: %d", fullCorrectedMv(4170, 4170));
  CHECK(fullCorrectedMv(3800, 4170) == 3800 && fullCorrectedMv(3600, 4170) == 3600, "low end untouched");
  CHECK(fullCorrectedMv(3900, 4170) == 3908, "3900 -> %d", fullCorrectedMv(3900, 4170));
  CHECK(fullCorrectedMv(4100, 4170) == 4124, "4100 -> %d", fullCorrectedMv(4100, 4170));
  CHECK(fullCorrectedMv(3900, 0) == 3900 && fullCorrectedMv(4170, 0) == 4170, "not learned: unchanged");
  CHECK(fullCorrectedMv(3900, 5000) == 3900 && fullCorrectedMv(3900, 100) == 3900, "nonsense full: unchanged");
  CHECK(fullCorrectedMv(4200, 4200) == 4200, "4200 full: no shift");
  CHECK(fullCorrectedMv(4220, 4220) == 4200, "a cell that rests at 4220 reads 100%%");
  for (int full : {4100, 4170, 4200, 4220}) {
    bool monotonic = true;
    int prev = 0;
    for (int mv = 3200; mv <= 4300; mv++) {
      const int c = fullCorrectedMv(mv, full);
      if (c < prev) monotonic = false;
      prev = c;
    }
    CHECK(monotonic, "corrected voltage fell as voltage rose (full %d)", full);
  }

  // In the Monitor: 4170 is 97 % on the plain curve and 100 % once that is known to be this cell's full
  Monitor plain(limits()), learned(limits());
  learned.setFullMv(4170);
  plain.addSample(0, 4170);
  learned.addSample(0, 4170);
  CHECK(plain.percent() == 97 && learned.percent() == 100, "4170: %d vs %d", plain.percent(), learned.percent());
  Monitor mid(limits());
  mid.setFullMv(4170);
  mid.addSample(0, 3900);
  CHECK(mid.percent() == 64, "3900 with a learned full: %d", mid.percent());
  Monitor bad(limits());
  bad.setFullMv(9999);
  bad.addSample(0, 4170);
  CHECK(bad.percent() == 97, "nonsense full voltage ignored");

  // The low-battery cutoff stays on the raw voltage
  Monitor lowM(limits());
  lowM.setFullMv(4100);
  for (int t = 0; t <= 11; t++) lowM.addSample(t * 1000, 3290);
  CHECK(lowM.low(11000) && lowM.percent() == 0, "low() unchanged");
  Monitor notLow(limits());
  notLow.setFullMv(4100);
  for (int t = 0; t <= 11; t++) notLow.addSample(t * 1000, 3320);
  CHECK(!notLow.low(11000), "3320 mV is not low");

  // The charger has finished: straight to 100
  Monitor m(limits());
  m.addSample(0, 4100);
  CHECK(m.percent() == 88, "4100 = %d", m.percent());
  m.markFull();
  CHECK(m.percent() == 100, "markFull");
  Monitor none(limits());
  none.markFull();
  CHECK(none.percent() == -1, "markFull with no battery changes nothing");
  printf("  full mapping: 4.170 V full -> 4.200 V, fades out by 3.800 V, low() raw\n");
}

static void testStepFromMonitor() {
  Monitor m(limits());
  for (int t = 0; t < 20; t++) m.addSample(t * 1000, 3900);
  int step = 0, from = 0;
  CHECK(!m.takeStep(step, from), "no step yet");
  m.addSample(20000, 3990);
  CHECK(!m.takeStep(step, from), "one reading is not a step");
  m.addSample(21000, 3990);
  CHECK(m.charging() && m.takeStep(step, from) && step == 90 && from == 3900, "step %d from %d", step, from);
  CHECK(!m.takeStep(step, from), "once");
  // Found by a USB host or the slow trend: no step to learn from
  Monitor u(limits());
  for (int t = 0; t < 5; t++) u.addSample(t * 1000, 3900);
  u.setUsbHost(true);
  for (int t = 5; t < 10; t++) u.addSample(t * 1000, 3900);
  CHECK(u.charging() && !u.takeStep(step, from), "a USB host has no step");
  printf("  plug-in step: +%d mV from %d mV\n", step, from);
}

static void startRun(Stats& s, int minutesOnBattery, int shown) {
  for (int i = 0; i < 3; i++) s.addMinute(Mode::ScreenOn, 4190, true, 100);
  s.addMinute(Mode::ScreenOn, 4140, false, shown);  // unplugged
  for (int i = 0; i < minutesOnBattery; i++) s.addMinute(Mode::ScreenOn, 3800, false, 90);
}

static void testFullRuns() {
  Stats s(defaults());
  startRun(s, 600, 100);
  s.addSleep(7200, 3800, 3795);
  s.lowReached();
  // 60 s (the unplug minute) + 600 minutes on, 2 h asleep: 43260 s = 721 minutes
  CHECK(s.data().lastRunMin == 721 && s.data().bestRunMin == 721, "run: %u min", (unsigned)s.data().lastRunMin);
  CHECK(s.data().lastMix[0] == 83 && s.data().lastMix[3] == 17 && s.data().lastMix[1] == 0 && s.data().lastMix[2] == 0, "mix %d %d %d %d",
        s.data().lastMix[0], s.data().lastMix[1], s.data().lastMix[2], s.data().lastMix[3]);
  Report r = s.report(Mode::ScreenOn, 50, false);
  CHECK(!r.runEstimate && near(r.runHours, 721 / 60.0f, 0.001f) && near(r.lastRunHours, r.runHours, 0.0001f), "run hours %.3f", r.runHours);
  CHECK(near(r.bestRunHours, 721 / 60.0f, 0.001f), "best run");

  // A shorter run: last changes, best stays
  startRun(s, 100, 100);
  s.lowReached();
  CHECK(s.data().lastRunMin == 101 && s.data().bestRunMin == 721 && s.data().bestMix[0] == 83, "second run: last %u, best %u",
        (unsigned)s.data().lastRunMin, (unsigned)s.data().bestRunMin);
  // A longer one takes the best
  startRun(s, 900, 96);
  s.lowReached();
  CHECK(s.data().lastRunMin == 901 && s.data().bestRunMin == 901 && s.data().bestMix[0] == 100, "third run: best %u mix %d",
        (unsigned)s.data().bestRunMin, s.data().bestMix[0]);

  // Charged part way: not a full run
  Stats c(defaults());
  startRun(c, 300, 100);
  c.addMinute(Mode::ScreenOn, 4000, true, 70);
  for (int i = 0; i < 3; i++) c.addMinute(Mode::ScreenOn, 3900, false, 70);
  c.lowReached();
  CHECK(c.data().lastRunMin == 0, "a charge in the middle cancels the run");
  // ... and so does a charge while asleep
  Stats z(defaults());
  startRun(z, 300, 100);
  z.addSleep(36000, 3800, 3950);
  z.lowReached();
  CHECK(z.data().lastRunMin == 0, "charged asleep cancels the run");

  // Unplugged below 95 %: no run
  Stats l(defaults());
  startRun(l, 700, 94);
  l.lowReached();
  CHECK(l.data().lastRunMin == 0, "unplugged at 94%% is not a full run");
  // Under an hour is not a run
  Stats sh(defaults());
  startRun(sh, 30, 100);
  sh.lowReached();
  CHECK(sh.data().lastRunMin == 0, "31 minutes is not a run");
  // No run, no record
  Stats n(defaults());
  n.lowReached();
  CHECK(!n.dirty() && n.report(Mode::ScreenOn, 50, false).runEstimate, "no run: capacity / ScreenOn draw");
  CHECK(near(n.report(Mode::ScreenOn, 50, false).runHours, 3000.0f / 180.0f, 0.01f), "estimate %.2f h",
        n.report(Mode::ScreenOn, 50, false).runHours);
  // A run in progress survives a restart (it is in the saved data)
  Stats p(defaults());
  startRun(p, 400, 100);
  Stats back(defaults());
  back.load(p.data());
  for (int i = 0; i < 100; i++) back.addMinute(Mode::ScreenOn, 3800, false, 60);
  back.lowReached();
  CHECK(back.data().lastRunMin == 501, "run carried over a restart: %u min", (unsigned)back.data().lastRunMin);
  printf("  full runs: 12.0 h with a 83/17 on/asleep mix, best kept, charge or <95%% or <1 h = no run\n");
}

static void testDirty() {
  Stats s(defaults());
  CHECK(!s.dirty(), "clean");
  s.addStep(80, 3800);
  CHECK(s.dirty(), "a step changes it");
  s.saved();
  s.addMinute(Mode::ScreenOn, 3800, false, 50);  // ignored (just started): nothing changes
  CHECK(!s.dirty(), "an ignored minute changes nothing");
  s.load(s.data());
  CHECK(!s.dirty(), "loading is not a change");
}

int main() {
  printf("Battery stats tests\n");
  testPercentX100();
  testUsage();
  testIgnored();
  testMinuteMeter();
  testSleep();
  testCorrupt();
  testHealth();
  testFullVoltage();
  testFullMapping();
  testStepFromMonitor();
  testFullRuns();
  testDirty();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
