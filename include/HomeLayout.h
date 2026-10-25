/**
 * Track Rat - where things are on the home screen (9.1)
 *
 * Seven places to tap, big for gloved fingers: the Next up card across the
 * top, four tiles in a row, then Wi-Fi and Settings side by side. The
 * top bar (above TOP) opens the GPS status. BOOT goes through them in
 * the order of Item: press for the next, hold to choose.
 *
 * The drawing (DisplayHome.cpp) and the taps (main.cpp) both use these, and
 * a tap anywhere below the top bar lands on one of them. Pure C++ (no
 * Arduino); tested in test/test_nextup.cpp.
 */

#ifndef HOME_LAYOUT_H
#define HOME_LAYOUT_H

namespace home {

enum Item { NextUp = 0, TrackDay, Autocross, Drag, Drive, Wifi, Settings, ITEM_COUNT };

constexpr int WIDTH = 320;
constexpr int TOP = 30;                      // below the top bar (a tap above is the GPS status)
constexpr int CARD_Y = 32, CARD_H = 72;      // the Next up card
constexpr int TILE_Y = 110, TILE_H = 68;     // the four tiles, a quarter of the width each
constexpr int ROW_Y = 184, ROW_H = 40;       // Wi-Fi on the left, Settings on the right
constexpr int TILE_SPLIT = 105, ROW_SPLIT = 181;  // where a tap changes from one band to the next

// What a tap at x, y lands on, or -1 for the top bar
inline int hit(int x, int y) {
  if (y < TOP) return -1;
  if (y < TILE_SPLIT) return NextUp;
  if (y < ROW_SPLIT) {
    const int q = x * 4 / WIDTH;
    return TrackDay + (q < 0 ? 0 : q > 3 ? 3 : q);
  }
  return x < WIDTH / 2 ? Wifi : Settings;
}

// BOOT press: the next place
inline int next(int item) { return (item + 1) % ITEM_COUNT; }

}  // namespace home

#endif  // HOME_LAYOUT_H
