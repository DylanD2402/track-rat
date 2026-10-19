/**
 * Track Rat - track list order
 *
 * The track list shows the nearest tracks first. Layouts of one venue stay
 * together, in their tracks.json order, at the distance of the venue's
 * closest layout. Tracks with no known distance (no GPS fix) keep their
 * tracks.json order after the rest, and the Bench Test is always last.
 *
 * Pure C++ (no Arduino); tested in test/test_trackorder.cpp.
 */

#ifndef TRACK_ORDER_H
#define TRACK_ORDER_H

#include <algorithm>
#include <map>
#include <vector>

namespace trackorder {

struct Item {
  double distanceM;  // < 0: unknown
  int group;         // same number = layouts of one venue; -1 = on its own
  bool last;         // always at the end (the Bench Test)
};

// Indices into items, nearest first.
inline std::vector<int> byDistance(const std::vector<Item>& items) {
  const double UNKNOWN = 1e18;
  std::map<int, double> groupDist;  // closest layout of each venue
  for (const Item& it : items) {
    if (it.group < 0 || it.distanceM < 0) continue;
    auto g = groupDist.find(it.group);
    if (g == groupDist.end() || it.distanceM < g->second) groupDist[it.group] = it.distanceM;
  }
  std::map<int, int> groupFirst;  // a venue's place in tracks.json, to keep its layouts together
  for (size_t i = 0; i < items.size(); i++)
    if (items[i].group >= 0 && !groupFirst.count(items[i].group)) groupFirst[items[i].group] = (int)i;

  auto key = [&](int i) {
    const Item& it = items[i];
    double d = it.distanceM < 0 ? UNKNOWN : it.distanceM;
    if (it.group >= 0 && groupDist.count(it.group)) d = groupDist[it.group];
    return d;
  };
  auto anchor = [&](int i) { return items[i].group >= 0 ? groupFirst[items[i].group] : i; };

  std::vector<int> order(items.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    if (items[a].last != items[b].last) return !items[a].last;
    double ka = key(a), kb = key(b);
    if (ka != kb) return ka < kb;
    return anchor(a) < anchor(b);  // same distance: venue order, then layouts in file order (stable)
  });
  return order;
}

}  // namespace trackorder

#endif  // TRACK_ORDER_H
