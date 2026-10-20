/**
 * Track Rat - VBO log format implementation
 */

#include "VboFormat.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace vbo {

// Channel names: the first seven are VBO standard channels (fixed units);
// the rest need a line each in [channel units].
static const char* const CHANNELS[] = {"satellites", "time",  "latitude",  "longitude",
                                       "velocity kmh", "heading", "height", "HDOP",
                                       "Lap",        "Lateral G", "Longitudinal G", "Lap tag"};
static const int STANDARD_CHANNELS = 7;
static const char* const UNITS[] = {"-", "-", "g", "g", "-"};
static_assert(sizeof(CHANNELS) / sizeof(CHANNELS[0]) - STANDARD_CHANNELS == sizeof(UNITS) / sizeof(UNITS[0]),
              "one [channel units] line per non-standard channel");
static const char* const COLUMN_NAMES ="sats time lat long velocity heading height HDOP Lap LateralG LongitudinalG LapTag";

static const double G0 = 9.80665;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void formatTime(uint32_t utcMs, char* out) {
  utcMs %= 86400000UL;
  snprintf(out, 11, "%02u%02u%02u.%03u", (unsigned)(utcMs / 3600000UL), (unsigned)(utcMs / 60000UL % 60),
           (unsigned)(utcMs / 1000UL % 60), (unsigned)(utcMs % 1000UL));
}

size_t formatLine(const Sample& s, char* out, size_t cap) {
  char t[11];
  formatTime(s.utcMs, t);
  float heading = fmodf(s.headingDeg, 360.0f);
  if (heading < 0) heading += 360.0f;
  if (heading >= 359.995f) heading = 0;  // would print as 360.00
  int n = snprintf(out, cap, "%03u %s %012.5f %012.5f %07.3f %06.2f %+09.2f %05.2f %03u %+06.3f %+06.3f %03u\r\n",
                   (unsigned)(s.sats > 99 ? 99 : s.sats), t, latToVbo(s.lat), lonToVbo(s.lon),
                   clampf(s.speedKmh, 0, 999.999f), heading, clampf(s.heightM, -99999.99f, 99999.99f),
                   clampf(s.hdop, 0, 99.99f), (unsigned)(s.lap > 999 ? 999 : s.lap),
                   clampf(s.latG, -GForce::MAX_G, GForce::MAX_G), clampf(s.longG, -GForce::MAX_G, GForce::MAX_G), (unsigned)s.tag);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

static void appendGate(std::string& s, const char* type, const Gate& g, const char* name) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s %+.6f %+.6f %+.6f %+.6f \xC2\xAC %s\r\n", type, lonToVbo(g.lon1), latToVbo(g.lat1),
           lonToVbo(g.lon2), latToVbo(g.lat2), name);
  s += buf;
}

std::string headerText(const Header& h) {
  std::string s;
  char buf[96];
  uint32_t t = h.utcMs % 86400000UL;
  snprintf(buf, sizeof(buf), "File created on %02u/%02u/%04u @ %02u:%02u:%02u\r\n\r\n", (unsigned)(h.day ? h.day : 1),
           (unsigned)(h.month ? h.month : 1), (unsigned)(h.year ? h.year : 2000), (unsigned)(t / 3600000UL),
           (unsigned)(t / 60000UL % 60), (unsigned)(t / 1000UL % 60));
  s += buf;

  s += "[header]\r\n";
  for (const char* c : CHANNELS) {
    s += c;
    s += "\r\n";
  }
  s += "\r\n[channel units]\r\n";
  for (const char* u : UNITS) {
    s += u;
    s += "\r\n";
  }
  s += "\r\n[comments]\r\n";
  for (const std::string& c : h.comments) {
    s += c;
    s += "\r\n";
  }
  if (h.hasStart) {
    s += "\r\n[laptiming]\r\n";
    appendGate(s, "Start  ", h.start, h.hasFinish ? "Start" : "StartFinish");
    for (size_t i = 0; i < h.splits.size(); i++) {
      snprintf(buf, sizeof(buf), "S%u", (unsigned)(i + 1));
      appendGate(s, "Split  ", h.splits[i], buf);
    }
    for (size_t i = 0; i < h.sectors.size(); i++) {  // the gate at the end of T1 is T1
      snprintf(buf, sizeof(buf), "T%u", (unsigned)(i + 1));
      appendGate(s, "Split  ", h.sectors[i], buf);
    }
    if (h.hasFinish) appendGate(s, "Finish ", h.finish, "Finish");
  }
  s += "\r\n[column names]\r\n";
  s += COLUMN_NAMES;
  s += "\r\n\r\n[data]\r\n";
  return s;
}

std::string pointText(const TrackPoint& p) {
  char buf[80];
  int n = snprintf(buf, sizeof(buf), "%.7f %.7f %.1f", p.lat, p.lon, p.radius);
  if (p.hasHeading() && n > 0 && (size_t)n < sizeof(buf)) {
    snprintf(buf + n, sizeof(buf) - n, " %.1f %s", p.heading, p.headingFrom == HeadingFrom::Map ? "map" : "set");
  }
  return buf;
}

bool parsePoint(const std::string& s, TrackPoint& p) {
  TrackPoint q = {0, 0, 0, 0, HeadingFrom::None};
  char from[8] = "";
  int n = sscanf(s.c_str(), "%lf %lf %f %f %7s", &q.lat, &q.lon, &q.radius, &q.heading, from);
  if (n < 3) return false;
  if (n >= 4) q.headingFrom = strcmp(from, "map") == 0 ? HeadingFrom::Map : HeadingFrom::Set;
  p = q;
  return true;
}

// --- g from the GPS ----------------------------------------------------------

void GForce::update(uint32_t utcMs, float speedKmh, float headingDeg, bool headingValid) {
  if (havePrev) {
    int32_t dt = utcDiffMs(prevMs, utcMs);
    if (dt == 0) return;  // same fix again
    if (dt > 0 && dt <= MAX_GAP_MS) {
      float dts = dt / 1000.0f;
      float v0 = prevSpeed / 3.6f, v1 = speedKmh / 3.6f;
      float rawLong = (v1 - v0) / dts / (float)G0;
      float rawLat = 0;
      if (headingValid && prevHeadingValid) {
        float dh = headingDeg - prevHeading;
        while (dh > 180.0f) dh -= 360.0f;
        while (dh < -180.0f) dh += 360.0f;
        rawLat = 0.5f * (v0 + v1) * (dh * (float)geo::DEG2RAD) / dts / (float)G0;  // v * yaw rate
      }
      rawLong = clampf(rawLong, -MAX_G, MAX_G);
      rawLat = clampf(rawLat, -MAX_G, MAX_G);
      float a = dts / (TAU_S + dts);
      lon += a * (rawLong - lon);
      lat += a * (rawLat - lat);
    } else {  // a gap, or the clock went backwards: start over
      lat = lon = 0;
    }
  }
  havePrev = true;
  prevMs = utcMs;
  prevSpeed = speedKmh;
  prevHeading = headingDeg;
  prevHeadingValid = headingValid;
}

// --- timing lines from the track map -----------------------------------------

bool gateFromMap(const std::vector<std::vector<geo::XYf>>& map, double refLat, double refLon, const TrackPoint& p,
                 Gate& out, double maxOffsetM) {
  const geo::XY c = geo::toLocal(p.lat, p.lon, refLat, refLon);
  double bestD2 = maxOffsetM * maxOffsetM;
  int bestLine = -1;
  size_t bestSeg = 0;
  double bestU = 0, bestLen = 0;
  for (size_t li = 0; li < map.size(); li++) {
    const std::vector<geo::XYf>& l = map[li];
    for (size_t i = 0; i + 1 < l.size(); i++) {
      double dx = l[i + 1].x - l[i].x, dy = l[i + 1].y - l[i].y;
      double len2 = dx * dx + dy * dy;
      double u = len2 > 1e-9 ? ((c.x - l[i].x) * dx + (c.y - l[i].y) * dy) / len2 : 0;
      u = u < 0 ? 0 : (u > 1 ? 1 : u);
      double ex = l[i].x + u * dx - c.x, ey = l[i].y + u * dy - c.y;
      if (ex * ex + ey * ey <= bestD2) {
        bestD2 = ex * ex + ey * ey;
        bestLine = (int)li;
        bestSeg = i;
        bestU = u;
        bestLen = sqrt(len2);
      }
    }
  }
  if (bestLine < 0) return false;

  // The track's direction here: from a point ~10 m back along the line to
  // one ~10 m ahead, which evens out a coarse or wobbly map. A closed loop
  // (first point = last) is followed round past its ends.
  const std::vector<geo::XYf>& l = map[bestLine];
  const size_t n = l.size();
  const bool closed = n > 3 && hypot(l[0].x - l[n - 1].x, l[0].y - l[n - 1].y) < 1.0;
  const size_t loop = n - 1;  // distinct points on a closed loop
  const double REACH_M = 10.0;
  auto walk = [&](size_t from, int step, double done) {  // done: metres from the nearest point to `from`
    size_t i = from;
    for (size_t k = 0; k < n; k++) {
      size_t next;
      if (step > 0) {
        if (i + 1 < n) next = i + 1;
        else if (closed) next = 1;
        else break;
      } else {
        if (i > 0) next = i - 1;
        else if (closed) next = loop - 1;
        else break;
      }
      done += hypot(l[next].x - l[i].x, l[next].y - l[i].y);
      i = next;
      if (done >= REACH_M) break;
    }
    return l[i];
  };
  geo::XYf back = walk(bestSeg, -1, bestU * bestLen);
  geo::XYf ahead = walk(bestSeg + 1, +1, (1 - bestU) * bestLen);
  double dx = ahead.x - back.x, dy = ahead.y - back.y;
  double len = hypot(dx, dy);
  if (len < 1.0) {  // degenerate: fall back to the segment itself
    dx = l[bestSeg + 1].x - l[bestSeg].x;
    dy = l[bestSeg + 1].y - l[bestSeg].y;
    len = hypot(dx, dy);
    if (len < 1e-3) return false;
  }
  const double nx = -dy / len, ny = dx / len;  // square to the track
  const double r = p.radius > 0 ? p.radius : 15.0;
  geo::fromLocal(c.x + nx * r, c.y + ny * r, refLat, refLon, out.lat1, out.lon1);
  geo::fromLocal(c.x - nx * r, c.y - ny * r, refLat, refLon, out.lat2, out.lon2);
  return true;
}

bool gateAcross(const TrackPoint& p, Gate& out) {
  if (!p.hasHeading()) return false;
  const double h = p.heading * geo::DEG2RAD;
  const double nx = cos(h), ny = -sin(h);  // square to the direction (sin h, cos h)
  const double r = p.radius > 0 ? p.radius : 15.0;
  geo::fromLocal(nx * r, ny * r, p.lat, p.lon, out.lat1, out.lon1);
  geo::fromLocal(-nx * r, -ny * r, p.lat, p.lon, out.lat2, out.lon2);
  return true;
}

// --- reading -----------------------------------------------------------------

static std::string lower(std::string s) {
  for (char& ch : s) ch = (char)tolower((unsigned char)ch);
  return s;
}

static std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

void Reader::mapColumns() {
  columnsMapped = true;
  for (int i = 0; i < (int)chans.size(); i++) {
    std::string c = lower(chans[i]);
    if (c == "satellites") colSats = i;
    else if (c == "time") colTime = i;
    else if (c == "latitude") colLat = i;
    else if (c == "longitude") colLon = i;
    else if (c == "velocity kmh") colSpeed = i;
    else if (c == "heading") colHeading = i;
    else if (c == "height") colHeight = i;
    else if (c == "hdop") colHdop = i;
    else if (c == "lap") colLap = i;
    else if (c == "lateral g") colLatG = i;
    else if (c == "longitudinal g") colLongG = i;
    else if (c == "lap tag") colTag = i;
  }
}

// "hhmmss.sss" (any number of decimals) -> ms since midnight; [s, e) is the token
static bool parseTime(const char* s, const char* e, uint32_t& ms) {
  int digits = 0;
  uint32_t hms = 0;
  while (s < e && isdigit((unsigned char)*s)) {
    hms = hms * 10 + (*s++ - '0');
    if (++digits > 6) return false;
  }
  if (digits < 5) return false;
  uint32_t frac = 0, scale = 1000;
  if (s < e && *s == '.') {
    s++;
    while (s < e && isdigit((unsigned char)*s)) {
      if (scale > 1) {
        scale /= 10;
        frac += (*s - '0') * scale;
      }
      s++;
    }
  }
  if (s != e) return false;
  uint32_t h = hms / 10000, m = hms / 100 % 100, sec = hms % 100;
  if (h > 23 || m > 59 || sec > 59) return false;
  ms = h * 3600000UL + m * 60000UL + sec * 1000UL + frac;
  return true;
}

bool Reader::feed(const char* line, Sample& out) {
  size_t len = strlen(line);
  while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;

  if (section != Section::Data || (len && line[0] == '[')) {
    std::string l(line, len);
    if (!l.empty() && l[0] == '[') {
      std::string name = lower(trim(l));
      if (name == "[header]") section = Section::Header;
      else if (name == "[channel units]") section = Section::Units;
      else if (name == "[comments]") section = Section::Comments;
      else if (name == "[column names]") section = Section::Columns;
      else if (name == "[data]") section = Section::Data;
      else section = Section::Other;
      return false;
    }
    if (trim(l).empty()) return false;
    if (section == Section::Header) chans.push_back(trim(l));
    else if (section == Section::Comments) comms.push_back(l);
    return false;
  }

  // A data line: parsed in place (recovery reads tens of thousands of them).
  const char* p = line;
  const char* const end = line + len;
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  if (p == end) return false;  // blank
  if (!columnsMapped) mapColumns();
  if (colTime < 0 || colLat < 0 || colLon < 0 || chans.size() > MAX_COLUMNS) {
    bad++;
    return false;
  }

  // Exactly one value per channel; anything else (such as a line cut short
  // by a power cut) is rejected.
  double v[MAX_COLUMNS] = {};
  uint32_t utc = 0;
  size_t col = 0;
  while (p < end) {
    const char* start = p;
    while (p < end && *p != ' ' && *p != '\t') p++;
    if (col >= chans.size()) {
      bad++;
      return false;
    }
    if ((int)col == colTime) {
      if (!parseTime(start, p, utc)) {
        bad++;
        return false;
      }
    } else {
      char* stop = nullptr;
      v[col] = strtod(start, &stop);
      if (stop != p) {
        bad++;
        return false;
      }
    }
    col++;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
  }
  if (col != chans.size()) {
    bad++;
    return false;
  }

  out = Sample();
  out.utcMs = utc;
  out.lat = v[colLat] / 60.0;
  out.lon = -v[colLon] / 60.0;
  if (colSpeed >= 0) out.speedKmh = (float)v[colSpeed];
  if (colHeading >= 0) out.headingDeg = (float)v[colHeading];
  if (colHeight >= 0) out.heightM = (float)v[colHeight];
  if (colSats >= 0) out.sats = (uint8_t)v[colSats];
  if (colHdop >= 0) out.hdop = (float)v[colHdop];
  if (colLap >= 0) out.lap = (uint16_t)v[colLap];
  if (colLatG >= 0) out.latG = (float)v[colLatG];
  if (colLongG >= 0) out.longG = (float)v[colLongG];
  if (colTag >= 0) out.tag = (uint8_t)v[colTag];
  return true;
}

std::string Reader::comment(const std::string& key) const {
  for (const std::string& c : comms) {
    size_t colon = c.find(':');
    if (colon == std::string::npos) continue;
    if (trim(c.substr(0, colon)) == key) return trim(c.substr(colon + 1));
  }
  return std::string();
}

}  // namespace vbo
