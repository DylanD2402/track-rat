/**
 * Host-side tests for best-ever and best-of-the-day laps (no ESP32 needed):
 * the stored file format, which stored lap the Compare with setting picks,
 * when a lap is a PB, and the live delta from a stored lap in the real
 * LapTimer.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_bestlap.cpp src/LapTimer.cpp -o /tmp/bl && /tmp/bl
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "BestLap.h"
#include "LapTimer.h"
#include "check.h"

using bestlap::Record;

static const double SF_LAT = 45.0, SF_LON = -76.0, R = 150.0;
static const double M_PER_DEG_LAT = 111194.93;
static const double TWO_PI = 2 * M_PI;

static void circlePos(double angle, double& lat, double& lon) {
  lat = SF_LAT + R * sin(angle) / M_PER_DEG_LAT;
  lon = SF_LON + R * (cos(angle) - 1.0) / (M_PER_DEG_LAT * cos(SF_LAT * geo::DEG2RAD));
}

static Record sampleRecord(uint32_t lapMs, uint32_t date, size_t points) {
  Record r;
  r.lapMs = lapMs;
  r.date = date;
  r.sfLat = SF_LAT;
  r.sfLon = SF_LON;
  for (size_t i = 0; i < points; i++)
    r.path.push_back({(float)i * 1.5f, -(float)i * 0.25f, (int32_t)(i * 50), 100.0f + i % 7});
  return r;
}

static void testFormat() {
  Record r = sampleRecord(92345, 20261020, 1800);
  std::vector<uint8_t> b = bestlap::encode(r);
  CHECK(b.size() == bestlap::HEADER_BYTES + 1800 * bestlap::POINT_BYTES, "size %zu", b.size());
  CHECK(b.size() < 30 * 1024, "a 90 s lap at 20 Hz is under 30 KB: %zu", b.size());
  Record back;
  CHECK(bestlap::decode(b.data(), b.size(), back), "decodes");
  CHECK(back.lapMs == 92345 && back.date == 20261020 && back.sfLat == SF_LAT && back.sfLon == SF_LON, "header");
  bool same = back.path.size() == r.path.size();
  for (size_t i = 0; same && i < r.path.size(); i++)
    same = back.path[i].x == r.path[i].x && back.path[i].y == r.path[i].y && back.path[i].t == r.path[i].t &&
           back.path[i].speedKmh == r.path[i].speedKmh;
  CHECK(same, "path round trip");

  Record head;
  CHECK(bestlap::decode(b.data(), bestlap::HEADER_BYTES, head, false) && head.lapMs == 92345 && head.path.empty(),
        "header only");
  CHECK(!bestlap::decode(b.data(), bestlap::HEADER_BYTES, head, true), "header only, but the path is wanted");
  CHECK(!bestlap::decode(b.data(), b.size() - 5, head), "cut short (power cut mid-write)");
  std::vector<uint8_t> bad = b;
  bad[0] ^= 0xFF;
  CHECK(!bestlap::decode(bad.data(), bad.size(), head), "wrong magic");
  bad = b;
  bad[28] = bad[29] = bad[30] = bad[31] = 0xFF;
  CHECK(!bestlap::decode(bad.data(), bad.size(), head), "absurd point count");
  Record none;
  std::vector<uint8_t> z = bestlap::encode(none);
  CHECK(!bestlap::decode(z.data(), z.size(), head, false), "no lap time: not a record");
  printf("  file: %zu bytes for 1800 points, round trip, damaged files refused\n", b.size());
}

static void testRules() {
  CHECK(bestlap::dateFromStamp("20261020_091500") == 20261020, "date from stamp");
  CHECK(bestlap::dateFromStamp("") == 0 && bestlap::dateFromStamp("SIM_42") == 0 &&
            bestlap::dateFromStamp("2026102X_091500") == 0,
        "no date");
  CHECK(bestlap::dateText(20261019) == "19 Oct 2026" && bestlap::dateText(0).empty(), "date text");

  Record ever = sampleRecord(90000, 20260601, 10), day = sampleRecord(93000, 20261020, 10), none;
  CHECK(bestlap::sameLine(ever, SF_LAT, SF_LON), "same line");
  CHECK(bestlap::sameLine(ever, SF_LAT + 4.0 / M_PER_DEG_LAT, SF_LON), "4 m: same line");
  CHECK(!bestlap::sameLine(ever, SF_LAT + 6.0 / M_PER_DEG_LAT, SF_LON), "6 m: the line moved");

  using bestlap::Ref;
  CHECK(bestlap::pickReference(CompareTo::Session, ever, day, 20261020) == Ref::None, "this session: no stored lap");
  CHECK(bestlap::pickReference(CompareTo::Ever, ever, day, 20261020) == Ref::Ever, "best ever");
  CHECK(bestlap::pickReference(CompareTo::Ever, none, day, 20261020) == Ref::None, "best ever, none yet");
  CHECK(bestlap::pickReference(CompareTo::Day, ever, day, 20261020) == Ref::Day, "best of the day");
  CHECK(bestlap::pickReference(CompareTo::Day, ever, day, 20261021) == Ref::Ever, "next day: best ever until a lap today");
  CHECK(bestlap::pickReference(CompareTo::Day, ever, day, 0) == Ref::Ever, "date unknown: best ever");
  CHECK(bestlap::pickReference(CompareTo::Day, none, none, 20261020) == Ref::None, "nothing stored");

  CHECK(bestlap::beats(89999, 0, ever) && !bestlap::beats(90000, 0, ever), "a PB must be quicker");
  CHECK(bestlap::beats(120000, 0, none), "the first clean lap is a PB");
  CHECK(!bestlap::beats(80000, laptags::GPS_GAP, ever) && !bestlap::beats(80000, laptags::PIT, none),
        "tagged laps never count");
  CHECK(bestlap::beatsDay(92000, 0, day, 20261020) && !bestlap::beatsDay(94000, 0, day, 20261020),
        "best of the day");
  CHECK(bestlap::beatsDay(99000, 0, day, 20261021), "yesterday's best doesn't count today");
  CHECK(!bestlap::beatsDay(80000, laptags::ODD_DISTANCE, day, 20261021), "tagged");
  printf("  rules: Compare with picks the stored lap, PBs and day bests need a quicker clean lap\n");
}

// Drives laps round the circle at v m/s through a lap timer at 10 Hz.
struct Car {
  LapTimer& lt;
  uint32_t t = 10 * 3600000u;
  double angle = -0.3;
  void to(double target, double v) {
    while (angle < target) {
      double lat, lon;
      circlePos(angle, lat, lon);
      lt.onFix(lat, lon, (float)(v * 3.6), t, t);
      t += 100;
      angle += v * 0.1 / R;
    }
  }
};

static std::vector<LapTimer::TracePt> pathOf(const std::vector<LapTimer::TracePt>& p) { return p; }

static void testStoredReference() {
  // Yesterday: a 30 m/s lap is stored as the PB
  LapTimer old;
  old.begin({SF_LAT, SF_LON, 15, 0, HeadingFrom::Set}, {});
  Car a{old};
  a.to(TWO_PI + 0.05, 30);
  CHECK(old.lapCount() == 1, "one lap yesterday");
  const uint32_t pbMs = old.lap(0).timeMs;
  std::vector<LapTimer::TracePt> pbPath = pathOf(old.bestPath());
  CHECK(pbPath.size() > 300 && pbPath.back().t == (int32_t)pbMs, "path ends at the line at the lap time");
  CHECK(pbPath[pbPath.size() / 2].speedKmh > 100, "speed kept with the path");

  // Today, from the first lap: compared with yesterday's lap
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15, 0, HeadingFrom::Set}, {});
  lt.setReference(pbPath, pbMs);
  Car b{lt};
  b.to(0.05, 25);
  b.to(M_PI, 27);  // lap 1, slower than the PB
  CHECK(lt.usingStoredReference() && lt.referenceLapMs() == pbMs, "lap 1 against the PB");
  CHECK(lt.liveDeltaMs() != NO_DELTA && lt.liveDeltaMs() > 1000, "behind the PB half a lap in: %d", lt.liveDeltaMs());
  b.to(TWO_PI + 0.05, 27);
  CHECK(lt.lapCount() == 1 && lt.usingStoredReference(), "slower than the PB: still against it");
  b.to(TWO_PI + M_PI, 32);  // lap 2: quicker than the PB
  CHECK(lt.liveDeltaMs() < -500, "ahead of the PB: %d", lt.liveDeltaMs());
  b.to(2 * TWO_PI + 0.05, 32);
  CHECK(lt.lapCount() == 2 && lt.bestLapIndex() == 1 && lt.lap(1).timeMs < pbMs, "lap 2 beats the PB");
  CHECK(!lt.usingStoredReference() && lt.referenceLapMs() == lt.lap(1).timeMs, "now against lap 2");
  b.to(2 * TWO_PI + M_PI, 32);
  CHECK(std::abs(lt.liveDeltaMs()) < 200, "same pace as lap 2: %d", lt.liveDeltaMs());

  // Best of the day: the stored lap is dropped once there's a lap today
  LapTimer d;
  d.begin({SF_LAT, SF_LON, 15, 0, HeadingFrom::Set}, {});
  d.setReference(pbPath, pbMs);
  d.clearReference();
  CHECK(!d.usingStoredReference() && d.referenceLapMs() == 0, "cleared");
  d.setReference(std::vector<LapTimer::TracePt>(1), 1000);
  CHECK(d.referenceLapMs() == 0, "a path of one point isn't a reference");
  d.setReference(pbPath, pbMs);
  d.begin({SF_LAT, SF_LON, 15, 0, HeadingFrom::Set}, {});
  CHECK(d.referenceLapMs() == 0, "begin() clears it");
  printf("  stored lap: live delta from lap 1, a quicker lap takes over\n");
}

int main() {
  printf("Best-ever and best-of-the-day laps\n");
  testFormat();
  testRules();
  testStoredReference();
  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nALL PASSED\n");
  return 0;
}
