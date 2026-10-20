/**
 * Track Rat - Settings screen and first-boot setup: how the inputs move
 * through them. Pure C++ (no Arduino), so it is tested on the PC in
 * test/test_settings.cpp; main.cpp turns InputEvents into calls and
 * DisplaySettings.cpp draws the state.
 */

#ifndef SETTINGS_FLOW_H
#define SETTINGS_FLOW_H

#include "SettingsData.h"

// Touch: left / centre / right third, the top bar, a long hold. BOOT: short, long.
enum class Key { None, Left, Center, Right, Header, Hold, BootShort, BootLong };

// The Settings screen is a scrolling list like the phone's: sections of rows
// in rounded cards. A row either opens a page of its own (a list of choices,
// or a level with - and +), flips a switch, or leaves the screen. This struct
// is the model of it: where everything is (the drawing code uses the same
// numbers), what a touch hits, and what each input does. Coordinates are
// screen pixels, 320x240; the list scrolls under the fixed top bar.
//
// Touch: tap a row, drag to scroll, the top bar goes back (to the menu from
// the list, to the list from a page). BOOT: press moves the highlight, hold
// opens or flips the highlighted row; on a choice page hold chooses; on a
// level page press is "+" and hold goes back.
struct SettingsScreenState {
  enum class Result { Nothing, Changed, Exit };  // Exit: back to the menu
  enum class Page : uint8_t { List, Choice, Brightness, TimeZone };
  enum class RowKind : uint8_t { Choice, Toggle, Level, Back };

  // --- layout ---------------------------------------------------------------
  static constexpr int SCREEN_W = 320, SCREEN_H = 240;
  static constexpr int TOP = 30;       // the list starts under the top bar (taps above this are the top bar)
  static constexpr int HINT_H = 22;    // the BOOT hint at the bottom, when there is no touch
  static constexpr int CARD_X = 10, CARD_W = 300;
  static constexpr int ROW_H = 40;
  static constexpr int CAPTION_H = 26;   // the grey caption above a card
  static constexpr int SECTION_GAP = 12; // below a card
  static constexpr int FOOT_H = 30;      // below a card that has a footnote
  static constexpr int LIST_PAD = 4;     // above the first caption
  static constexpr int ROWS = 10, SECTIONS = 5;
  // A page of choices: the card, then the item's note under it
  static constexpr int CHOICE_TOP = TOP + 12;
  // A level page (brightness, time zone): the bar and the two big buttons
  static constexpr int BAR_X = 40, BAR_W = 240, BAR_Y = 122, BAR_H = 8, BAR_HIT = 14;
  static constexpr int BTN_Y = 156, BTN_H = 44, BTN_W = 145, MINUS_X = 10, PLUS_X = 165;

  struct Row {
    SettingItem item;  // SettingItem::Count for the Back row
    RowKind kind;
  };
  struct Section {
    const char* title;  // nullptr: no caption
    int first, count;   // rows
    const char* foot;   // footnote under the card, or nullptr
  };

  // --- state ----------------------------------------------------------------
  Page page = Page::List;
  SettingItem item = SettingItem::Count;  // the setting the open page is about
  int scroll = 0;                         // how far the list is scrolled up, 0..maxScroll()
  int highlight = 0;                      // BOOT's row on the list
  int choiceHl = 0;                       // BOOT's option on a choice page
  bool showHighlight = false;             // draw the highlight (BOOT in use, or no touch)
  int viewBottom = SCREEN_H;              // the list ends here; higher when the BOOT hint takes the bottom

  // Call on entering the screen. Without touch the highlight is shown from the start and the hint takes the bottom.
  void begin(bool touch) {
    *this = SettingsScreenState();
    showHighlight = !touch;
    viewBottom = touch ? SCREEN_H : SCREEN_H - HINT_H;
  }

  // --- the rows -------------------------------------------------------------
  static const Row& row(int r) {
    static const Row rows[ROWS] = {
        {SettingItem::Units, RowKind::Choice},      {SettingItem::Upside, RowKind::Toggle},
        {SettingItem::Brightness, RowKind::Level},  {SettingItem::Big, RowKind::Choice},
        {SettingItem::Compare, RowKind::Choice},    {SettingItem::Gps, RowKind::Choice},
        {SettingItem::Bluetooth, RowKind::Toggle},  {SettingItem::RoadDrives, RowKind::Toggle},
        {SettingItem::TimeZone, RowKind::Level},    {SettingItem::Count, RowKind::Back},
    };
    return rows[r];
  }
  static const Section& section(int i) {
    static const Section sections[SECTIONS] = {
        {"DISPLAY", 0, 3, nullptr},
        {"TIMING", 3, 2, nullptr},
        {"GPS & BLUETOOTH", 5, 2, "GPS mode is used from the next restart"},
        {"RECORDING", 7, 2, nullptr},
        {nullptr, 9, 1, nullptr},
    };
    return sections[i];
  }
  static int sectionOf(int r) {
    for (int i = 0; i < SECTIONS; i++)
      if (r >= section(i).first && r < section(i).first + section(i).count) return i;
    return 0;
  }
  static int rowOfItem(SettingItem it) {
    for (int r = 0; r < ROWS; r++)
      if (row(r).item == it) return r;
    return 0;
  }
  // The name on a row (itemName() is the capitals used elsewhere)
  static const char* rowLabel(SettingItem it) {
    switch (it) {
      case SettingItem::Units: return "Speed units";
      case SettingItem::Upside: return "Screen upside down";
      case SettingItem::Brightness: return "Brightness";
      case SettingItem::Compare: return "Compare with";
      case SettingItem::Big: return "Big number";
      case SettingItem::Gps: return "GPS mode";
      case SettingItem::Bluetooth: return "Bluetooth";
      case SettingItem::TimeZone: return "Time zone";
      case SettingItem::RoadDrives: return "Record road drives";
      default: return "Back to menu";
    }
  }
  // The state of a switch row
  static bool toggleOn(const SettingsData& d, SettingItem it) {
    switch (it) {
      case SettingItem::Upside: return settings::upsideDown(d);
      case SettingItem::Bluetooth: return d.bluetooth;
      case SettingItem::RoadDrives: return d.recordRoadDrives;
      default: return false;
    }
  }

  // --- where things are on the list (content coordinates: 0 is the top of the list) ---
  static int captionH(int sec) { return (section(sec).title ? CAPTION_H : 0) + (sec == 0 ? LIST_PAD : 0); }
  static int sectionHeight(int sec) { return captionH(sec) + section(sec).count * ROW_H + (section(sec).foot ? FOOT_H : SECTION_GAP); }
  static int sectionTop(int sec) {
    int y = 0;
    for (int i = 0; i < sec; i++) y += sectionHeight(i);
    return y;
  }
  static int cardTop(int sec) { return sectionTop(sec) + captionH(sec); }
  static int cardBottom(int sec) { return cardTop(sec) + section(sec).count * ROW_H; }
  static int rowTop(int r) { return cardTop(sectionOf(r)) + (r - section(sectionOf(r)).first) * ROW_H; }
  static int contentHeight() { return sectionTop(SECTIONS); }

  int viewH() const { return viewBottom - TOP; }
  int maxScroll() const { return contentHeight() > viewH() ? contentHeight() - viewH() : 0; }
  int rowScreenY(int r) const { return TOP + rowTop(r) - scroll; }  // top of a row on the screen
  int sectionScreenY(int sec) const { return TOP + sectionTop(sec) - scroll; }

  void setScroll(int v) { scroll = v < 0 ? 0 : v > maxScroll() ? maxScroll() : v; }

  // The thin scroll bar at the right edge, if there is more than fits
  bool scrollBar(int& y, int& h) const {
    if (maxScroll() <= 0) return false;
    h = viewH() * viewH() / contentHeight();
    if (h < 16) h = 16;
    y = TOP + (viewH() - h) * scroll / maxScroll();
    return true;
  }

  // The row under a touch on the list, or -1 (between cards, on a caption, under the top bar...)
  int hitRow(int x, int y) const {
    if (x < CARD_X || x >= CARD_X + CARD_W || y < TOP || y >= viewBottom) return -1;
    for (int r = 0; r < ROWS; r++) {
      const int top = rowScreenY(r);
      if (y >= top && y < top + ROW_H) return r;
    }
    return -1;
  }

  // --- choices ----------------------------------------------------------------
  static int choiceCount(SettingItem it) {
    switch (it) {
      case SettingItem::Units: return 2;
      case SettingItem::Compare: return 3;
      case SettingItem::Big: return 3;
      case SettingItem::Gps: return 2;
      default: return 0;
    }
  }
  static int choiceIndex(const SettingsData& d, SettingItem it) {
    switch (it) {
      case SettingItem::Units: return (int)d.units;
      case SettingItem::Compare: return (int)d.compareTo;
      case SettingItem::Big: return (int)d.bigNumber;
      case SettingItem::Gps: return (int)d.gpsMode;
      default: return 0;
    }
  }
  static void choiceSet(SettingsData& d, SettingItem it, int i) {
    if (i < 0 || i >= choiceCount(it)) return;
    switch (it) {
      case SettingItem::Units: d.units = (SpeedUnits)i; break;
      case SettingItem::Compare: d.compareTo = (CompareTo)i; break;
      case SettingItem::Big: d.bigNumber = (BigNumber)i; break;
      case SettingItem::Gps: d.gpsMode = (GpsMode)i; break;
      default: break;
    }
  }
  int choiceRowY(int i) const { return CHOICE_TOP + i * ROW_H; }
  // The option under a touch on a choice page, or -1
  int hitChoice(int x, int y) const {
    const int n = choiceCount(item);
    if (x < CARD_X || x >= CARD_X + CARD_W || y < CHOICE_TOP || y >= CHOICE_TOP + n * ROW_H) return -1;
    return (y - CHOICE_TOP) / ROW_H;
  }

  // --- levels (brightness, time zone) -------------------------------------------
  static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
  static bool onMinus(int x, int y) { return inRect(x, y, MINUS_X, BTN_Y, BTN_W, BTN_H); }
  static bool onPlus(int x, int y) { return inRect(x, y, PLUS_X, BTN_Y, BTN_W, BTN_H); }
  static bool onBar(int x, int y) { return inRect(x, y, BAR_X - 10, BAR_Y - BAR_HIT, BAR_W + 20, BAR_H + 2 * BAR_HIT); }
  // The brightness for a touch at x on the bar: where it is along the bar, to the nearest 10, 10..100
  static int barLevel(int x) {
    int dx = x - BAR_X;
    dx = dx < 0 ? 0 : dx > BAR_W ? BAR_W : dx;
    int v = (dx * 100 / BAR_W + settings::BRIGHTNESS_STEP / 2) / settings::BRIGHTNESS_STEP * settings::BRIGHTNESS_STEP;
    return v < settings::BRIGHTNESS_MIN ? settings::BRIGHTNESS_MIN : v > settings::BRIGHTNESS_MAX ? settings::BRIGHTNESS_MAX : v;
  }

  // --- inputs ---------------------------------------------------------------------

  // The top bar, a long hold, or a BOOT press or hold. Edits s if it changed a value.
  Result handle(Key k, SettingsData& s) {
    switch (k) {
      case Key::Header:
      case Key::Hold:
        if (page == Page::List) return Result::Exit;
        closePage();
        return Result::Nothing;
      case Key::BootShort: return bootShort(s);
      case Key::BootLong: return bootLong(s);
      default: return Result::Nothing;
    }
  }

  // A tap at screen x, y
  Result tap(int x, int y, SettingsData& s) {
    showHighlight = false;
    if (y < TOP) return handle(Key::Header, s);
    if (y >= viewBottom) return Result::Nothing;
    if (page == Page::List) {
      const int r = hitRow(x, y);
      return r < 0 ? Result::Nothing : activate(r, s);
    }
    if (page == Page::Choice) {
      const int i = hitChoice(x, y);
      return i < 0 ? Result::Nothing : choose(i, s);
    }
    if (onMinus(x, y)) return step(-1, false, s);
    if (onPlus(x, y)) return step(+1, false, s);
    if (page == Page::Brightness && onBar(x, y)) {
      SettingsData before = s;
      s.brightness = (uint8_t)barLevel(x);
      return s == before ? Result::Nothing : Result::Changed;
    }
    return Result::Nothing;
  }

  // A finger is dragging the list: dy is how far it has moved since it touched down. The list follows it.
  void drag(int dy) {
    if (page != Page::List) return;
    if (!dragging) {
      dragging = true;
      dragBase = scroll;
    }
    setScroll(dragBase - dy);
  }
  void dragEnd() { dragging = false; }

private:
  bool dragging = false;
  int dragBase = 0;

  void closePage() {
    page = Page::List;
    choiceHl = 0;
  }

  // Scrolls just enough to show row r, with its caption if it is the first of its section
  void showRow(int r) {
    const int sec = sectionOf(r);
    int top = rowTop(r), bottom = top + ROW_H;
    if (r == section(sec).first) top = sectionTop(sec);
    if (r == section(sec).first + section(sec).count - 1) bottom = sectionTop(sec + 1);
    if (top < scroll) scroll = top;
    if (bottom > scroll + viewH()) scroll = bottom - viewH();
    setScroll(scroll);
  }

  void openPage(SettingItem it, const SettingsData& s) {
    item = it;
    if (it == SettingItem::Brightness) page = Page::Brightness;
    else if (it == SettingItem::TimeZone) page = Page::TimeZone;
    else {
      page = Page::Choice;
      choiceHl = choiceIndex(s, it);
    }
  }

  // Tap or hold on a row
  Result activate(int r, SettingsData& s) {
    highlight = r;
    const Row& rw = row(r);
    switch (rw.kind) {
      case RowKind::Back: return Result::Exit;
      case RowKind::Toggle: {
        SettingsData before = s;
        settings::adjust(s, rw.item, +1, false);
        return s == before ? Result::Nothing : Result::Changed;
      }
      default:
        openPage(rw.item, s);
        return Result::Nothing;
    }
  }

  // Picks option i on a choice page and goes back to the list
  Result choose(int i, SettingsData& s) {
    SettingsData before = s;
    choiceSet(s, item, i);
    page = Page::List;
    highlight = rowOfItem(item);
    showRow(highlight);
    return s == before ? Result::Nothing : Result::Changed;
  }

  // - or + on a level page
  Result step(int dir, bool wrap, SettingsData& s) {
    SettingsData before = s;
    settings::adjust(s, item, dir, wrap);
    return s == before ? Result::Nothing : Result::Changed;
  }

  Result bootShort(SettingsData& s) {
    showHighlight = true;
    switch (page) {
      case Page::List:
        highlight = (highlight + 1) % ROWS;
        showRow(highlight);
        return Result::Nothing;
      case Page::Choice:
        choiceHl = (choiceHl + 1) % choiceCount(item);
        return Result::Nothing;
      default:
        return step(+1, true, s);
    }
  }

  Result bootLong(SettingsData& s) {
    showHighlight = true;
    switch (page) {
      case Page::List: return activate(highlight, s);
      case Page::Choice: return choose(choiceHl, s);
      default:
        closePage();
        return Result::Nothing;
    }
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
