/**
 * Track Rat - where the Autocross hub's buttons are
 *
 * One place for the rectangles, so the drawing (DisplayAutocross.cpp) and the touch (main.cpp) cannot drift
 * apart: they once differed by 2 px. A button's hit zone is its drawn rectangle grown to the midpoint of
 * the gaps around it, so a tap between two buttons still goes to the nearer one and the zones never overlap.
 *
 * Pure C++ (no Arduino); tested on the PC in test/test_autocross_layout.cpp.
 */

#ifndef AUTOCROSS_LAYOUT_H
#define AUTOCROSS_LAYOUT_H

namespace autocross_layout {

constexpr int SCREEN_W = 320, SCREEN_H = 240;
constexpr int THIRD = SCREEN_W / 3;  // 106

// Hub buttons, in the order main.cpp's choice numbers them
enum Button : int { START_RUNS = 0, NEW_COURSE = 1, ADJUST_START = 2, ADJUST_FINISH = 3, BACK = 4, COUNT = 5 };

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

constexpr int ROW1_Y = 152, ROW1_H = 36;  // Start runs, New course
constexpr int ROW2_Y = 194, ROW2_H = 40;  // Adjust start, Adjust finish, Back

// The buttons as drawn
constexpr Rect button(int b) {
  return b == START_RUNS      ? Rect{5, ROW1_Y, 2 * THIRD - 10, ROW1_H}
         : b == NEW_COURSE    ? Rect{2 * THIRD + 5, ROW1_Y, THIRD - 10, ROW1_H}
         : b == ADJUST_START  ? Rect{5, ROW2_Y, THIRD - 10, ROW2_H}
         : b == ADJUST_FINISH ? Rect{THIRD + 5, ROW2_Y, THIRD - 10, ROW2_H}
                              : Rect{2 * THIRD + 5, ROW2_Y, THIRD - 10, ROW2_H};
}

// The touch: halfway across the gaps between rows (y 191) and columns (x 106 and 212); the top row reaches
// up 2 px, the bottom row down to the screen's edge
constexpr int ZONE_TOP = ROW1_Y - 2, ZONE_SPLIT_Y = 191, ZONE_COL2 = 2 * THIRD;
constexpr Rect zone(int b) {
  return b == START_RUNS      ? Rect{0, ZONE_TOP, ZONE_COL2, ZONE_SPLIT_Y - ZONE_TOP}
         : b == NEW_COURSE    ? Rect{ZONE_COL2, ZONE_TOP, SCREEN_W - ZONE_COL2, ZONE_SPLIT_Y - ZONE_TOP}
         : b == ADJUST_START  ? Rect{0, ZONE_SPLIT_Y, THIRD, SCREEN_H - ZONE_SPLIT_Y}
         : b == ADJUST_FINISH ? Rect{THIRD, ZONE_SPLIT_Y, THIRD, SCREEN_H - ZONE_SPLIT_Y}
                              : Rect{ZONE_COL2, ZONE_SPLIT_Y, SCREEN_W - ZONE_COL2, SCREEN_H - ZONE_SPLIT_Y};
}

// The Button at screen x, y, or -1
inline int buttonAt(int x, int y) {
  for (int b = 0; b < COUNT; b++)
    if (zone(b).contains(x, y)) return b;
  return -1;
}

}  // namespace autocross_layout

#endif
