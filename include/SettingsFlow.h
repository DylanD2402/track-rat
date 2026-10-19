/**
 * Track Rat - Settings screen and first-boot setup: how the inputs move
 * through them. Pure C++ (no Arduino), so it is tested on the PC in
 * test/test_settings.cpp; main.cpp turns InputEvents into Keys and draws
 * the state.
 */

#ifndef SETTINGS_FLOW_H
#define SETTINGS_FLOW_H

#include "SettingsData.h"

// Touch: left / centre / right third, the top bar, a long hold. BOOT: short, long.
enum class Key { None, Left, Center, Right, Header, Hold, BootShort, BootLong };

// The settings screen shows one item at a time, then a "done" page.
// Left third -, right third +, centre next item, top bar or hold: back.
// BOOT: short = next item, long = change the value (going round at the ends).
struct SettingsScreenState {
  enum class Result { Nothing, Changed, Exit };
  int item = 0;  // 0..SETTING_ITEM_COUNT-1; SETTING_ITEM_COUNT is the "done" page

  bool donePage() const { return item >= SETTING_ITEM_COUNT; }

  // Handles a key, editing s if it changed a value.
  Result handle(Key k, SettingsData& s) {
    const bool done = donePage();
    switch (k) {
      case Key::Header:
      case Key::Hold:
        return Result::Exit;
      case Key::Center:
        if (done) return Result::Exit;
        item++;
        return Result::Nothing;
      case Key::Left:
        if (done) item = SETTING_ITEM_COUNT - 1;
        else return change(s, -1, false);
        return Result::Nothing;
      case Key::Right:
        if (done) item = 0;
        else return change(s, +1, false);
        return Result::Nothing;
      case Key::BootShort:
        item = (item + 1) % (SETTING_ITEM_COUNT + 1);
        return Result::Nothing;
      case Key::BootLong:
        if (done) return Result::Exit;
        return change(s, +1, true);
      default:
        return Result::Nothing;
    }
  }

private:
  Result change(SettingsData& s, int dir, bool wrap) {
    SettingsData before = s;
    settings::adjust(s, (SettingItem)item, dir, wrap);
    return s == before ? Result::Nothing : Result::Changed;
  }
};

// First boot: step 0 asks which way up (tap the arrow that points up: left is
// the screen as drawn, right is upside down), step 1 asks km/h or mph.
// BOOT: short = the other choice, long = choose it.
struct SetupState {
  int step = 0;       // 0 = which way up, 1 = units
  int highlight = 0;  // BOOT's choice: 0 = left, 1 = right

  // Handles a key. Returns true when s changed; s.setupDone is set when the last question is answered.
  bool handle(Key k, SettingsData& s) {
    int choice = -1;
    if (k == Key::Left) choice = 0;
    else if (k == Key::Right) choice = 1;
    else if (k == Key::BootShort) highlight ^= 1;
    else if (k == Key::BootLong) choice = highlight;
    if (choice < 0 || s.setupDone) return false;

    if (step == 0) {
      if (choice == 1) s.rotation = s.rotation == 1 ? 3 : 1;  // the "up" arrow was the other one
      step = 1;
      highlight = 0;
    } else {
      s.units = choice == 1 ? SpeedUnits::Mph : SpeedUnits::Kmh;
      s.setupDone = true;
    }
    return true;
  }
};

#endif  // SETTINGS_FLOW_H
