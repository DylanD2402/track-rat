/**
 * Track Rat - what the Adjust start / Adjust finish screen shows (9.3b)
 *
 * Plain data, no Arduino code: main.cpp fills it from the model (AdjustLine.h) and
 * DisplayAdjust.cpp lays it out. Text is already made (std::string), so the PC renders
 * (tools/render_screens.py) can build a view by hand.
 */

#ifndef ADJUST_VIEW_H
#define ADJUST_VIEW_H

#include <string>
#include <vector>

namespace adjust {

// A point on a run's path in the view's frame, metres: along = in the direction of travel from where the
// line was before it was moved (negative: behind it), across = to the right of travel.
struct PathPt {
  float along, across;
};

struct ViewRow {
  int number = 0;           // Run 3
  std::string oldText;      // "53.40"
  std::string newText;      // "53.46", "-" if the run can't be timed with the line there
  std::string diffText;     // "+0.06", "" if it didn't change
  bool best = false;        // the quickest run with the line where it is now
  bool changed = false;
};

enum class Phase : unsigned char {
  Reading,    // the runs' logs are being read
  Ready,      // nudging
  NoLogs,     // there are no session logs for the day's runs (or no SD card)
  Saving,     // the line is saved, the day's logs are being timed again
};

// The buttons, left to right, for the BOOT button's highlight (-1: none)
enum Button : int { BTN_EARLIER = 0, BTN_RESET = 1, BTN_LATER = 2, BTN_CANCEL = 3, BTN_SAVE = 4, BTN_COUNT = 5 };

struct View {
  bool finish = true;  // Adjust finish (else Adjust start)
  Phase phase = Phase::Reading;
  int done = 0, total = 0;  // Reading: logs read of the day's; Saving: sessions timed again of all
  std::string message;      // Saving, NoLogs: a line saying more

  // Ready
  float offsetM = 0;       // how far the line has moved: + later
  float halfWidthM = 10;   // half the line's length
  bool canMove = true;     // false: no way to tell which way is forward
  bool atEarliest = false, atLatest = false;
  std::string offsetText;  // "Moved 1.5 m later"
  std::string summary;     // "Best 52.31 > 52.37"
  std::string note;        // standing starts: how they are timed
  std::vector<std::vector<PathPt>> paths;  // one per row, in step with rows
  std::vector<ViewRow> rows;
};

}  // namespace adjust

#endif  // ADJUST_VIEW_H
