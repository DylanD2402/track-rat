/**
 * Track Rat - re-timing rules
 *
 * After the track's lines change (the Wi-Fi page's track editor), a session
 * is timed again from its .vbo log with the lines as they are now
 * (SessionReplay::useLines), its summary rewritten, and the best laps put
 * right. The task that does it is in SessionLog.cpp; what it decides is here:
 *
 * - Which logs: one by name, or every log whose summary names the track.
 * - The PB: the best clean lap of the re-timed sessions replaces the stored
 *   one if there is none, if the stored one was timed at another line (its
 *   time can't be compared any more), or if it's quicker.
 * - The best of the day: the same, for the latest day among the re-timed
 *   sessions, and only over a stored lap from that day or before.
 *
 * No Arduino dependencies: tested in test/test_retime.cpp.
 */

#ifndef RETIME_H
#define RETIME_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace retime {

// One re-timed session's best clean lap (lapMs 0: none).
struct Found {
  uint32_t lapMs = 0;
  uint32_t date = 0;  // YYYYMMDD
  size_t session = 0; // which of the re-timed sessions
};

// Is this log one to re-time? file: one log by name ("" = all at the track);
// summaryTrack: the track its summary names.
inline bool wanted(const std::string& logName, const std::string& summaryTrack, const std::string& file,
                   const std::string& trackId) {
  if (!file.empty()) return logName == file;
  return !trackId.empty() && summaryTrack == trackId;
}

// The quickest of them (lapMs 0 if none has a clean lap).
inline Found best(const std::vector<Found>& all) {
  Found b;
  for (const Found& f : all)
    if (f.lapMs && (!b.lapMs || f.lapMs < b.lapMs)) b = f;
  return b;
}

// The quickest on the latest day that has a clean lap.
inline Found bestOfLatestDay(const std::vector<Found>& all) {
  uint32_t day = 0;
  for (const Found& f : all)
    if (f.lapMs && f.date > day) day = f.date;
  Found b;
  for (const Found& f : all)
    if (f.lapMs && f.date == day && (!b.lapMs || f.lapMs < b.lapMs)) b = f;
  return b;
}

// Does the re-timed best replace the stored PB (storedMs 0: none)?
// storedSameLine: the stored lap was timed at the line as it is now.
inline bool replacesPb(const Found& found, uint32_t storedMs, bool storedSameLine) {
  if (!found.lapMs) return false;
  return !storedMs || !storedSameLine || found.lapMs < storedMs;
}

// Does the latest day's re-timed best replace the stored best of the day?
inline bool replacesDay(const Found& found, uint32_t storedMs, uint32_t storedDate, bool storedSameLine) {
  if (!found.lapMs) return false;
  if (!storedMs || found.date > storedDate) return true;
  if (found.date < storedDate) return false;  // the stored one is from a later day: it stays
  return !storedSameLine || found.lapMs < storedMs;
}

}  // namespace retime

#endif  // RETIME_H
