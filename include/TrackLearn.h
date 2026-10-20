/**
 * Track Rat - tracks that learn themselves
 *
 * Away from known tracks, Free Roam watches the path for a loop driven twice:
 * back past a point passed at speed (LEARN_MIN_KMH), heading the same way,
 * within LEARN_MATCH_M, at least LEARN_MIN_LOOP_MS and LEARN_MIN_LOOP_M
 * later, and then past it once more after a second loop of about the same
 * length (LEARN_SAME_LOOP). Needing two loops keeps a drive round the block
 * from becoming a track. main.cpp then creates the track with a provisional
 * start/finish where the first loop began, and times both loops from the
 * fixes it kept (FixRing).
 *
 * The path is kept as points LoopFinder::SPACING_M apart, for the last
 * LEARN_WINDOW_MS only, in metres from the first fix. Time is any clock that
 * only goes forward (main.cpp uses each fix's millis()).
 *
 * No Arduino dependencies: tested in test/test_tracklearn.cpp.
 */

#ifndef TRACK_LEARN_H
#define TRACK_LEARN_H

#include <math.h>
#include <stdint.h>
#include <vector>
#include "Config.h"
#include "GeoMath.h"

namespace learn {

// A loop driven twice: the provisional start/finish (where the first loop
// began, and the way the car was heading there) and when it was passed.
struct Loop {
  double lat = 0, lon = 0;
  float headingDeg = 0;
  uint32_t t0 = 0, t1 = 0, t2 = 0;  // the three passes, in the clock given to onFix
  float lap1M = 0, lap2M = 0;       // the two loops' lengths
};

// Directions of travel: the smaller angle between them, 0..180
inline float headingDiff(float a, float b) {
  float d = fmodf(fabsf(a - b), 360.0f);
  return d > 180.0f ? 360.0f - d : d;
}

class LoopFinder {
public:
  static constexpr float SPACING_M = 5.0f;     // path resolution
  static constexpr double MAX_JUMP_M = 200.0;  // a position jump (GPS glitch) isn't distance
  static constexpr size_t MAX_POINTS = 20000;  // 100 km of path: plenty for LEARN_WINDOW_MS

  void reset() { *this = LoopFinder(); }

  // One fix. True when this pass completes the second loop: *out says where and when.
  bool onFix(double lat, double lon, float speedKmh, float courseDeg, bool courseValid, uint32_t ms, Loop* out) {
    if (!haveRef) {
      refLat = lat;
      refLon = lon;
      haveRef = true;
      pts.reserve(4096);  // large: allocated in PSRAM on the ESP32
    }
    const geo::XY p = geo::toLocal(lat, lon, refLat, refLon);
    if (haveLast) {
      const double d = hypot(p.x - lastX, p.y - lastY);
      if (d < MAX_JUMP_M) distM += d;
    }
    lastX = p.x;
    lastY = p.y;
    haveLast = true;

    dropOld(ms);
    if (count() && hypot(p.x - pts.back().x, p.y - pts.back().y) < SPACING_M) return false;

    Pt now;
    now.x = (float)p.x;
    now.y = (float)p.y;
    now.t = ms;
    now.dist = (float)distM;
    // The GPS course when it has one; else the way the path went since the last point
    if (courseValid) now.heading = courseDeg;
    else if (count()) now.heading = geo::bearingDeg(p.x - pts.back().x, p.y - pts.back().y);
    else now.heading = -1;
    now.fast = speedKmh >= LEARN_MIN_KMH && now.heading >= 0;

    const bool found = now.fast && match(now, out);
    if (count() >= MAX_POINTS) first++;  // drop the oldest: never more than MAX_POINTS
    pts.push_back(now);
    compact();
    return found;
  }

  size_t count() const { return pts.size() - first; }

private:
  struct Pt {
    float x, y, heading, dist;
    uint32_t t;
    bool fast;
  };
  struct Pass {
    size_t i;  // its closest point to the one being matched
    float d;
  };

  std::vector<Pt> pts;  // pts[first..] is the path in the window
  size_t first = 0;
  bool haveRef = false, haveLast = false;
  double refLat = 0, refLon = 0, lastX = 0, lastY = 0, distM = 0;

  void dropOld(uint32_t ms) {
    while (count() && ms - pts[first].t > LEARN_WINDOW_MS) first++;
  }
  void compact() {
    if (first > 1024 && first * 2 > pts.size()) {
      pts.erase(pts.begin(), pts.begin() + first);
      first = 0;
    }
  }

  // Is `now` a third pass of one point, after two loops of about the same length?
  bool match(const Pt& now, Loop* out) const {
    // Earlier passes close to here, the same way, newest first (not this
    // pass itself). Points of one pass are seconds apart; its closest point
    // stands for it.
    Pass passes[2];
    size_t np = 0;
    const float r2 = LEARN_MATCH_M * LEARN_MATCH_M;
    for (size_t i = pts.size(); i-- > first;) {
      const Pt& q = pts[i];
      if (now.dist - q.dist <= 3 * LEARN_MATCH_M) continue;
      const float dx = q.x - now.x, dy = q.y - now.y;
      if (fabsf(dx) > LEARN_MATCH_M || fabsf(dy) > LEARN_MATCH_M) continue;
      const float d2 = dx * dx + dy * dy;
      if (d2 > r2 || !q.fast || headingDiff(q.heading, now.heading) > LEARN_HEADING_DEG) continue;
      if (np && pts[passes[np - 1].i].t - q.t < PASS_GAP_MS) {
        if (d2 < passes[np - 1].d) passes[np - 1] = {i, d2};
      } else {
        if (np == 2) break;  // only the last two are needed
        passes[np++] = {i, d2};
      }
    }
    if (np < 2) return false;
    // The last two passes: each a whole loop before the next, the loops alike.
    // A pass in between (a loop under LEARN_MIN_LOOP_M driven over and over)
    // makes them the wrong two.
    const Pt& B = pts[passes[0].i];
    const Pt& A = pts[passes[1].i];
    if (!longEnough(B, now) || !longEnough(A, B)) return false;
    const float lap1 = B.dist - A.dist, lap2 = now.dist - B.dist;
    if (fabsf(lap1 - lap2) > LEARN_SAME_LOOP * (lap1 > lap2 ? lap1 : lap2)) return false;  // different loops
    if (retraced(passes[1].i, passes[0].i)) return false;
    if (out) {
      geo::fromLocal(A.x, A.y, refLat, refLon, out->lat, out->lon);
      out->headingDeg = A.heading;
      out->t0 = A.t;
      out->t1 = B.t;
      out->t2 = now.t;
      out->lap1M = lap1;
      out->lap2M = lap2;
    }
    return true;
  }

  static bool longEnough(const Pt& from, const Pt& to) {
    return to.t - from.t >= LEARN_MIN_LOOP_MS && to.dist - from.dist >= LEARN_MIN_LOOP_M;
  }

  // Out along a road and back the same way (a hill climb and the drive down,
  // runs up and down a strip) passes each point twice, but it isn't a track:
  // most of the loop runs alongside itself the other way.
  bool retraced(size_t from, size_t to) const {
    const size_t n = to - from;
    size_t back = 0;
    for (size_t i = from; i < to; i++) {
      const Pt& p = pts[i];
      for (size_t j = from; j < to; j++) {
        const Pt& q = pts[j];
        const float dx = q.x - p.x, dy = q.y - p.y;
        if (fabsf(dx) > RETRACE_M || fabsf(dy) > RETRACE_M || dx * dx + dy * dy > RETRACE_M * RETRACE_M) continue;
        if (headingDiff(p.heading, q.heading) > 150.0f) {
          back++;
          break;
        }
      }
      if (back * 3 > n * 2) return true;                 // most of it: retraced
      if ((back + (to - i - 1)) * 3 <= n * 2) return false;  // can't be any more
    }
    return false;
  }

  static constexpr uint32_t PASS_GAP_MS = 5000;  // closer than this: the same pass
  static constexpr float RETRACE_M = 8.0f;       // the way back runs this close (a kart track's straights are further apart)
};

// The last fixes, oldest first, to time a learned track's loops from. A fixed
// size, set once (large: PSRAM on the ESP32).
template <class T>
class FixRing {
public:
  void begin(size_t capacity) {
    buf.assign(capacity, T());
    head = n = 0;
  }
  void release() {
    std::vector<T>().swap(buf);
    head = n = 0;
  }
  // True if the oldest was dropped to make room (what was [i] is now [i - 1])
  bool push(const T& v) {
    if (buf.empty()) return false;
    buf[(head + n) % buf.size()] = v;
    if (n < buf.size()) {
      n++;
      return false;
    }
    head = (head + 1) % buf.size();
    return true;
  }
  size_t size() const { return n; }
  const T& operator[](size_t i) const { return buf[(head + i) % buf.size()]; }  // 0 = oldest
  void clear() { head = n = 0; }

private:
  std::vector<T> buf;
  size_t head = 0, n = 0;
};

}  // namespace learn

#endif  // TRACK_LEARN_H
