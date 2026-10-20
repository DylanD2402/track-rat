/**
 * Track Rat - Session replay
 *
 * Times a session again from its .vbo log: the log is fed in as raw bytes
 * (any chunk size), each fix goes through the same LapTimer (or FreeRoam)
 * the live session used, set up from the lines recorded in the log's header.
 * Used at boot to rebuild the summary of a session cut short by a power cut,
 * and for re-timing (the Wi-Fi page's track editor): useLines() times it with
 * the track's lines as they are now instead of the ones in the log.
 *
 * No Arduino dependencies: tested in test/test_vbo.cpp.
 */

#ifndef SESSION_REPLAY_H
#define SESSION_REPLAY_H

#include <functional>
#include <string>
#include <vector>
#include "FreeRoam.h"
#include "LapTimer.h"
#include "VboFormat.h"

class SessionReplay {
public:
  // For a log whose header has no start/finish (the line was marked during
  // the session): fills in the track's current line. Optional.
  using LineLookup = std::function<bool(const std::string& trackId, TrackPoint& sf, std::vector<TrackPoint>& splits)>;
  explicit SessionReplay(LineLookup lookup = nullptr) : lookup(lookup) {}
  // For a point-to-point log whose header has no finish (it was set during
  // the session): the track's finish now. Optional.
  using FinishLookup = std::function<bool(const std::string& trackId, TrackPoint& finish)>;
  FinishLookup finishLookup;

  // The track the session's summary names, if any. Set before feeding. If
  // it's another track than the header's, the session was switched to that
  // layout mid-session and kept its laps (main.cpp switchLayout): it's timed
  // with that track's lines (from the lookup) and named after it, as long as
  // they share the start/finish the header's track was timed at.
  std::string summaryTrackId;

  // Re-timing: time every fix with these lines (the track as it is now), not
  // the ones recorded in the log's header. The session is named after
  // summaryTrackId if that's set. Call before feeding.
  void useLines(const TrackPoint& sf, const std::vector<TrackPoint>& splits, const std::vector<TrackPoint>& sectors) {
    overrideLines = true;
    lineSf = sf;
    lineSplits = splits;
    lineSectors = sectors;
  }
  // With useLines, for a point-to-point track: its finish now (hasFinish false: none yet).
  void useStage(const TrackPoint& finish, bool hasFinish) {
    overrideStage = true;
    lineFinish = finish;
    lineHasFinish = hasFinish;
  }

  void feed(const char* data, size_t len);
  void finish();  // after the last byte

  // Results
  bool freeRoam = false;
  bool timed = false;            // a start/finish was known, so laps were timed
  bool switchedLayout = false;   // timed as summaryTrackId: trackId is that, trackName is left empty
  uint32_t samples = 0;          // well-formed data lines
  uint32_t durationMs = 0;       // first fix to last, across midnight
  bool cutLine = false;          // the last line had no line ending (power cut) and was dropped
  LapTimer timer;
  FreeRoam drive;
  std::string trackId, trackName, stamp;
  int session = 0;               // "Session N" of the day at the track, 0 if the log doesn't say
  const vbo::Reader& reader() const { return rdr; }

private:
  static const size_t MAX_LINE = 255;
  LineLookup lookup;
  bool overrideLines = false;
  bool overrideStage = false, lineHasFinish = false;
  TrackPoint lineFinish = {0, 0, 0, 0, HeadingFrom::None};
  TrackPoint lineSf = {0, 0, 0, 0, HeadingFrom::None};
  std::vector<TrackPoint> lineSplits, lineSectors;
  vbo::Reader rdr;
  bool setUp = false;
  uint32_t lastUtc = 0;
  char line[MAX_LINE + 1];
  size_t len = 0;
  bool overlong = false;
  void endLine();
  void setUpFromHeader();
};

#endif  // SESSION_REPLAY_H
