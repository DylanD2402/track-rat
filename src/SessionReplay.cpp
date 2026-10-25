/**
 * Track Rat - Session replay implementation
 */

#include "SessionReplay.h"
#include <stdlib.h>
#include "LayoutMatch.h"

void SessionReplay::setUpFromHeader() {
  setUp = true;
  stamp = rdr.comment("Started");
  session = atoi(rdr.comment("Session").c_str());
  driver = rdr.comment("Driver");
  car = rdr.comment("Car");
  driverId = atoi(rdr.comment("Driver id").c_str());
  carId = atoi(rdr.comment("Car id").c_str());
  freeRoam = rdr.comment("Type") == "freeroam";
  if (freeRoam) {
    drive.start(0);
    return;
  }
  trackId = rdr.comment("Track");
  trackName = rdr.comment("Track name");
  long minLap = atol(rdr.comment("Min lap ms").c_str());
  const uint32_t minLapMs = minLap > 0 ? (uint32_t)minLap : (uint32_t)MIN_LAP_TIME_MS;

  if (overrideLines) {  // re-timing: the track's lines now, whatever the log recorded
    if (summaryTrackId.size() && summaryTrackId != trackId) {
      trackId = summaryTrackId;
      trackName.clear();
    }
    timed = true;
    timer.begin(lineSf, lineSplits, minLapMs, lineSectors);
    if (overrideStage) timer.setStage(lineFinish, lineHasFinish);
    return;
  }

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
  // Switched to another layout at the same line: every lap is timed with its
  // splits, as the layout check found the driver on it from the first lap.
  TrackPoint toSf = {0, 0, 0, 0, HeadingFrom::None};
  std::vector<TrackPoint> toSplits;
  if (timed && summaryTrackId.size() && summaryTrackId != trackId && lookup &&
      lookup(summaryTrackId, toSf, toSplits) && layout::sharesLine(sf, toSf)) {
    sf = toSf;
    splits = toSplits;
    trackId = summaryTrackId;
    trackName.clear();  // the header's is the old layout's; the track list has this one's
    switchedLayout = true;
  }
  std::vector<TrackPoint> sectorGates;  // corner sectors (only with the lines in the header)
  for (int i = 1; !switchedLayout; i++) {
    TrackPoint g;
    if (!vbo::parsePoint(rdr.comment("Sector " + std::to_string(i)), g)) break;
    sectorGates.push_back(g);
  }
  if (!timed) return;
  timer.begin(sf, splits, minLapMs, sectorGates);
  if (rdr.comment("Type") == "stage") {  // point to point: runs end at the finish
    TrackPoint fin = {0, 0, 0, 0, HeadingFrom::None};
    const bool hasFinish = vbo::parsePoint(rdr.comment("Finish"), fin) || (finishLookup && finishLookup(trackId, fin));
    timer.setStage(fin, hasFinish);
  }
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
    // The log has no accuracy figures, only the mark on each fix that had a poor one
    else if (timed) {
      // The engine's temperatures for the laps' hottest (6.5): the log has 0 for "not known"
      timer.noteTemps(rdr.hasCoolant() && s.coolantC != 0, s.coolantC, rdr.hasOil() && s.oilC != 0, s.oilC);
      timer.onFix(s.lat, s.lon, s.speedKmh, s.utcMs, durationMs, (s.tag & laptags::POOR_ACCURACY) ? 99.0f : -1.0f);
    }
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
