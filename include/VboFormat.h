/**
 * Track Rat - VBO log format
 *
 * Racelogic's VBO text format, which the free Circuit Tools opens directly:
 * a header naming the channels, then one line per GPS fix. See
 * https://developers.racelogic.co.uk/Portal/Tutorials/tutorialVBOfilestructure.html
 *
 * - Lines end in CRLF; sections are separated by blank lines.
 * - Latitude and longitude are in minutes (degrees x 60), and longitude's
 *   sign is reversed: west is positive.
 * - time is UTC hhmmss.sss. It wraps at midnight, like a VBOX's.
 *
 * Also reads the files back (boot recovery, re-timing). No Arduino
 * dependencies: unit-tested on a PC in test/test_vbo.cpp.
 */

#ifndef VBO_FORMAT_H
#define VBO_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "GeoMath.h"

namespace vbo {

// One line of the log.
struct Sample {
  uint32_t utcMs = 0;  // UTC milliseconds since midnight
  double lat = 0, lon = 0;
  float speedKmh = 0;
  float headingDeg = 0;
  float heightM = 0;
  uint8_t sats = 0;
  float hdop = 0;
  uint16_t lap = 0;    // lap number, 0 before timing starts (out lap)
  float latG = 0;      // + = turning right
  float longG = 0;     // + = speeding up
};

// A timing line for [laptiming], by its two ends.
struct Gate {
  double lat1, lon1, lat2, lon2;
};

struct Header {
  uint16_t year = 0;   // UTC date and time of the first fix ("File created on")
  uint8_t month = 0, day = 0;
  uint32_t utcMs = 0;
  std::vector<std::string> comments;  // [comments] lines, "Key: value" where they're read back
  bool hasStart = false;              // [laptiming] is written only with a start/finish
  Gate start = {0, 0, 0, 0};
  std::vector<Gate> splits;
};

// Everything before the first data line, ending with "[data]".
std::string headerText(const Header& h);

// One data line, with its CRLF. Returns its length, or 0 if `cap` is too small.
// Every line is the same length for the same file layout.
size_t formatLine(const Sample& s, char* out, size_t cap);

// "hhmmss.sss"; out needs 11 bytes.
void formatTime(uint32_t utcMs, char* out);

// Positions as written in the file
inline double latToVbo(double deg) { return deg * 60.0; }
inline double lonToVbo(double deg) { return -deg * 60.0; }

// Milliseconds from a to b on the UTC clock, across midnight if needed.
inline int32_t utcDiffMs(uint32_t a, uint32_t b) {
  int32_t d = (int32_t)b - (int32_t)a;
  if (d < -43200000) d += 86400000;
  if (d > 43200000) d -= 86400000;
  return d;
}

// Lateral and longitudinal g from the GPS alone: speed change and heading
// change between fixes, lightly smoothed (about 0.2 s).
class GForce {
public:
  static constexpr float TAU_S = 0.2f;
  static constexpr float MAX_G = 5.0f;          // anything beyond is a glitch
  static constexpr int32_t MAX_GAP_MS = 1000;   // longer gaps start over

  void reset() { *this = GForce(); }
  void update(uint32_t utcMs, float speedKmh, float headingDeg, bool headingValid);
  float lateral() const { return lat; }
  float longitudinal() const { return lon; }

private:
  bool havePrev = false;
  uint32_t prevMs = 0;
  float prevSpeed = 0, prevHeading = 0;
  bool prevHeadingValid = false;
  float lat = 0, lon = 0;
};

// A start/finish or split line across the track at `p`, `p.radius` metres
// either side, square to the track's direction there. The direction comes
// from the nearest map line within maxOffsetM of `p` (map in metres from
// refLat/refLon, as Track::map). False if no map line passes that close.
bool gateFromMap(const std::vector<std::vector<geo::XYf>>& map, double refLat, double refLon, const TrackPoint& p,
                 Gate& out, double maxOffsetM = 30.0);

// Reads a VBO a line at a time.
class Reader {
public:
  // `line` without its line ending (a trailing CR is ignored). Returns true
  // for a well-formed data line, decoded into `out`.
  bool feed(const char* line, Sample& out);

  const std::vector<std::string>& channels() const { return chans; }
  const std::vector<std::string>& comments() const { return comms; }
  // The value of a "Key: value" comment line, or "" if there isn't one.
  std::string comment(const std::string& key) const;
  uint32_t badLines() const { return bad; }

private:
  static const size_t MAX_COLUMNS = 64;  // a file with more channels isn't read
  enum class Section { None, Header, Units, Comments, Columns, Data, Other };
  Section section = Section::None;
  std::vector<std::string> chans;
  std::vector<std::string> comms;
  bool columnsMapped = false;
  int colSats = -1, colTime = -1, colLat = -1, colLon = -1, colSpeed = -1, colHeading = -1, colHeight = -1,
      colHdop = -1, colLap = -1, colLatG = -1, colLongG = -1;
  uint32_t bad = 0;
  void mapColumns();
};

}  // namespace vbo

#endif  // VBO_FORMAT_H
