/**
 * Track Rat - every track's map, kept compact
 *
 * tracks.json is 270 KB; parsing it takes 100-250 ms, which used to happen at every session start (loadMap) and
 * every layout check (readMaps). The map points are copied out once, when the file is loaded or saved, and read
 * from here instead. Points are kept as the doubles the file holds, so the metres come out exactly as when
 * parsing the file. Pure code, no Arduino dependencies (the firmware passes an allocator that uses PSRAM), so it
 * can be unit-tested on a PC (test/test_mapstore.cpp).
 */

#ifndef MAP_STORE_H
#define MAP_STORE_H

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "LayoutMatch.h"

namespace mapstore {

template <class Alloc>
using Vec = std::vector<double, typename std::allocator_traits<Alloc>::template rebind_alloc<double>>;
template <class Alloc>
using VecU32 = std::vector<uint32_t, typename std::allocator_traits<Alloc>::template rebind_alloc<uint32_t>>;

template <class Alloc = std::allocator<double>>
class MapStore {
public:
  void clear() {
    slots_.clear();
    latLon_.clear();
    lineEnds_.clear();
    open_ = false;
  }
  void shrink() {
    slots_.shrink_to_fit();
    latLon_.shrink_to_fit();
    lineEnds_.shrink_to_fit();
  }

  // Adding a map: addMap(), then addLine() and addPoint() for each of its lines, in file order.
  // The first map for a venue and layout is the one kept, as the file's search finds the first.
  void addMap(const std::string& venueId, int layoutIndex) {
    for (const Slot& s : slots_)
      if (s.layoutIndex == layoutIndex && s.venueId == venueId) {
        open_ = false;  // a repeat: ignored
        return;
      }
    Slot s;
    s.venueId = venueId;
    s.layoutIndex = layoutIndex;
    s.firstLine = (uint32_t)lineEnds_.size();
    s.lineCount = 0;
    slots_.push_back(s);
    open_ = true;
  }
  void addLine() {
    if (!open_) return;
    lineEnds_.push_back((uint32_t)latLon_.size() / 2);
    slots_.back().lineCount++;
  }
  void addPoint(double lat, double lon) {
    if (!open_ || slots_.back().lineCount == 0) return;
    latLon_.push_back(lat);
    latLon_.push_back(lon);
    lineEnds_.back() = (uint32_t)latLon_.size() / 2;
  }

  // The map's lines in metres from the reference point, as parsing the file gives them: a line under two
  // points is left out. False if there's no map stored for the venue and layout (out is then empty).
  bool read(const std::string& venueId, int layoutIndex, double refLat, double refLon, layout::Lines& out) const {
    out.clear();
    for (const Slot& s : slots_) {
      if (s.layoutIndex != layoutIndex || s.venueId != venueId) continue;
      for (uint32_t l = 0; l < s.lineCount; l++) {
        const uint32_t begin = l == 0 ? firstPoint(s) : lineEnds_[s.firstLine + l - 1];
        const uint32_t end = lineEnds_[s.firstLine + l];
        if (end - begin < 2) continue;
        std::vector<geo::XYf> pts;
        pts.reserve(end - begin);
        for (uint32_t i = begin; i < end; i++) {
          const geo::XY p = geo::toLocal(latLon_[2 * i], latLon_[2 * i + 1], refLat, refLon);
          pts.push_back({(float)p.x, (float)p.y});
        }
        out.push_back(std::move(pts));
      }
      return true;
    }
    return false;
  }

  size_t points() const { return latLon_.size() / 2; }
  size_t maps() const { return slots_.size(); }

private:
  struct Slot {
    std::string venueId;
    int layoutIndex = -1;
    uint32_t firstLine = 0, lineCount = 0;
  };
  // Where a map's first line starts: where the line before the map ended
  uint32_t firstPoint(const Slot& s) const { return s.firstLine == 0 ? 0 : lineEnds_[s.firstLine - 1]; }

  std::vector<Slot> slots_;
  Vec<Alloc> latLon_;
  VecU32<Alloc> lineEnds_;  // for each line, the point index it ends at (points run on from line to line)
  bool open_ = false;
};

}  // namespace mapstore

#endif  // MAP_STORE_H
