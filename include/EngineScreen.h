/**
 * Track Rat - Settings > Engine data: how the inputs move through it (6.1)
 *
 * Two pages. Status: the adapter, its protocol, requests a second and each
 * channel's rate, with Find adapter and Forget buttons. Find: the scan's list,
 * strongest first; tapping one chooses it. Pure C++ (no Arduino), tested on
 * the PC in test/test_settings.cpp; main.cpp turns inputs into calls and
 * DisplaySettings.cpp draws it.
 */

#ifndef ENGINE_SCREEN_H
#define ENGINE_SCREEN_H

#include "SettingsFlow.h"

struct EngineScreenState {
  enum class Page : uint8_t { Status, Find };
  enum class Action { Nothing, Back, Scan, Choose, Forget };  // Back: to the settings list. Choose: found[chosen]

  // --- layout (screen pixels, 320x240) ---
  static constexpr int TOP = SettingsScreenState::TOP;
  static constexpr int BTN_Y = 192, BTN_H = 40, BTN_W = 145, FIND_X = 10, FORGET_X = 165;
  static constexpr int LIST_Y = TOP + 26, ROW_H = 30, CARD_X = 10, CARD_W = 300;

  Page page = Page::Status;
  int highlight = 0;   // BOOT: Find / Forget on the status page, a row on the list
  int chosen = -1;     // Choose: which one
  bool showHighlight = false;

  void begin(bool touch) {
    *this = EngineScreenState();
    showHighlight = !touch;
  }

  static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
  static int listRowY(int i) { return LIST_Y + i * ROW_H; }

  // A tap at x, y. found: how many adapters the list has.
  Action tap(int x, int y, int found) {
    showHighlight = false;
    if (y < TOP) return back();
    if (page == Page::Status) {
      if (inRect(x, y, FIND_X, BTN_Y, BTN_W, BTN_H)) return find();
      if (inRect(x, y, FORGET_X, BTN_Y, BTN_W, BTN_H)) return Action::Forget;
      return Action::Nothing;
    }
    if (x < CARD_X || x >= CARD_X + CARD_W || y < LIST_Y) return Action::Nothing;
    const int i = (y - LIST_Y) / ROW_H;
    return i < found ? choose(i) : Action::Nothing;
  }

  // The top bar, a hold, or BOOT
  Action handle(Key k, int found) {
    switch (k) {
      case Key::Header:
      case Key::Hold: return back();
      case Key::BootShort:
        showHighlight = true;
        if (page == Page::Status) highlight ^= 1;
        else highlight = found ? (highlight + 1) % (found + 1) : 0;  // the rows, then the way back
        return Action::Nothing;
      case Key::BootLong:
        showHighlight = true;
        if (page == Page::Status) return highlight == 0 ? find() : Action::Forget;
        return highlight < found ? choose(highlight) : back();
      default: return Action::Nothing;
    }
  }

private:
  Action back() {
    if (page == Page::Status) return Action::Back;
    page = Page::Status;
    highlight = 0;
    return Action::Nothing;
  }
  Action find() {
    page = Page::Find;
    highlight = 0;
    return Action::Scan;
  }
  Action choose(int i) {
    chosen = i;
    page = Page::Status;
    highlight = 0;
    return Action::Choose;
  }
};

#endif  // ENGINE_SCREEN_H
