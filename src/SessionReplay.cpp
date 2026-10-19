/**
 * Track Rat - Session replay implementation
 */

#include "SessionReplay.h"
#include <stdlib.h>

void SessionReplay::setUpFromHeader() {
  setUp = true;
  stamp = rdr.comment("Started");
  freeRoam = rdr.comment("Type") == "freeroam";
  if (freeRoam) {
    drive.start(0);
    return;
  }
  trackId = rdr.comment("Track");
  trackName = rdr.comment("Track name");

  TrackPoint sf = {0, 0, 0, 0, HeadingFrom::None};
  std::vector<TrackPoint> splits;
  if (vbo::parsePoint(rdr.comment("Start/finish"), sf)) {
    for (int i = 1;; i++) {
      TrackPoint sp;
      if (!vbo::parsePoint(rdr.comment("Split " + std::to_string(i)), sp)) break;
      splits.push_back(sp);
    }
    timed = true;
  } else if (lookup && lookup(trackId, sf, splits)) {
    timed = true;
  }
  long minLap = atol(rdr.comment("Min lap ms").c_str());
  if (timed) timer.begin(sf, splits, minLap > 0 ? (uint32_t)minLap : (uint32_t)MIN_LAP_TIME_MS);
}

void SessionReplay::endLine() {
  line[len] = 0;
  vbo::Sample s;
  if (!overlong && rdr.feed(line, s)) {
    if (!setUp) setUpFromHeader();
    if (samples) {
      int32_t dt = vbo::utcDiffMs(lastUtc, s.utcMs);
      if (dt > 0) durationMs += dt;
    }
    lastUtc = s.utcMs;
    samples++;
    // rxMillis only drives the on-screen clock; elapsed log time stands in for it
    if (freeRoam) drive.onFix(s.lat, s.lon, s.speedKmh);
    else if (timed) timer.onFix(s.lat, s.lon, s.speedKmh, s.utcMs, durationMs);
  }
  len = 0;
  overlong = false;
}

void SessionReplay::feed(const char* data, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c = data[i];
    if (c == '\n') endLine();
    else if (len < MAX_LINE) line[len++] = c;
    else overlong = true;
  }
}

void SessionReplay::finish() {
  // Every line is written whole with its CRLF, so a last line without one
  // was cut short by a power cut: drop it (a cut inside the last number
  // would still parse, wrongly).
  if (len || overlong) cutLine = true;
  len = 0;
  overlong = false;
  if (!setUp) setUpFromHeader();  // no fixes at all: still say which session it was
}
