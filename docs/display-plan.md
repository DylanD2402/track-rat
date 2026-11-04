# Any screen size, and LVGL

A plan for running Track Rat's screens on displays other than the ES3C28P's 2.8" 320×240 panel: bigger and smaller panels, portrait ones, and boards whose makers ship LVGL. Written on 2026-11-04, after v0.2.0-beta.2, with the [0.2 code review](code-review-0.2.md). Nothing here is built yet. Back to the [README](../README.md).

## Where the screens stand

- **One panel, one library.** An ILI9341V at 320×240 over SPI, driven by TFT_eSPI. The panel's driver, size and pins are build flags in `platformio.ini`, so a different panel means a different build.
- **31 screens** in `DisplayManager` and its seven `Display*.cpp` files, about 2,700 lines. Each frame clears a full-screen sprite (150 KB, in PSRAM), draws into it, and sends only the rows that changed.
- **Positions are pixels for 320×240.** About 320 drawing calls use literal coordinates (`centered(..., 168, ...)`, `fillRoundRect(8, 110, W - 16, 50, ...)`), and the rest use `ui::W`, `ui::H`, `HEADER_H` and `FOOTER_Y` from `UiTheme.h`.
- **Seven fonts at fixed sizes**, Inter from 12 px (captions) to 76 px (the big digits), made by `tools/make_fonts.py` in TFT_eSPI's smooth-font format and compiled into flash.
- **Touch is turned into meanings early.** `InputManager` reports the top bar, the left, centre or right third, or a hold, and screens with more buttons than that test `lastTapX/Y` against rectangles in shared layout headers (`HomeLayout.h`, `AutocrossLayout.h`, `AdjustView.h`, `SettingsFlow.h`, `DeviceScreen.h`). Where those headers are used, drawing and touch can't disagree. That's the pattern to extend.
- **Some drawing code reaches into the rest of the firmware.** The top bar reads `Settings::get()`, `EngineData`, `RaceBoxBle` and `CameraBle` itself, and a few screens read `Storage`. The rest is handed what to show by `main.cpp` (`RunningOverlay`, `DebriefView`, `AutocrossView`, `adjust::View`, `nextcard::Choice`).
- **The drawing surface is small.** The PC screen renderer (`tools/render_screens.py`) builds the real screen code against a 300-line stand-in for TFT_eSPI. That stand-in is the whole set of calls the screens need: filled and outlined rectangles and rounded rectangles, circles, triangles, lines, horizontal and vertical lines, text at an anchor point with its width, a font choice, and reading a pixel back (smooth text blends with what's under it).

So four things tie the screens to this board: pixel positions, font sizes, the drawing library, and the touch chip. None of them is deep. The positions are the most work.

## What LVGL is, and isn't

LVGL isn't a kind of display. It's a UI library (widgets, styles, layouts, animations, input handling) that sits on top of a panel driver, the way `DisplayManager` sits on TFT_eSPI. "A display that uses LVGL" almost always means a board whose maker ships LVGL examples. Those are usually bigger panels: 3.5" 480×320 over SPI, or 4.3" to 7" 800×480 RGB panels that the ESP32-S3 refreshes from a framebuffer in PSRAM.

That splits the question in two:

1. **Can Track Rat drive that panel and its touch chip?** Every new board needs this. It doesn't need LVGL: LovyanGFX (and ESP-IDF's `esp_lcd`) drive SPI, 8-bit parallel and RGB panels and the common touch chips.
2. **Should the screens be drawn with LVGL?** Optional. It's a second way of drawing every screen, kept up for good. It pays off on big panels and for configurable dashboards. It isn't needed to make Track Rat run on those boards.

The plan does 1 for any size first, and keeps 2 as the last step, built on the same pieces.

## The design

```
 main.cpp, split into screen controllers
 (state, timing, sessions, what each screen shows)
        |                                   ^
        | screen models                     | UiAction: Back, Next, Select,
        | (RunningModel, HomeModel, ...)    | TopBar, Item(n), Hold
        v                                   |
 +---------------------------+     +---------------------------+
 | Track Rat renderer        |     | LVGL renderer (optional)  |
 | today's screens, drawn on |     | the same models, as       |
 | gfx::Canvas, positions    |     | LVGL widgets              |
 | from ui::Metrics          |     |                           |
 +---------------------------+     +---------------------------+
        |                                   |
 Canvas backends:                   lv_display + lv_indev
  - TFT_eSPI sprite (today)         (flush through LovyanGFX
  - LovyanGFX (SPI, parallel, RGB)   or esp_lcd)
  - the PC stand-in (screenshots)
        |                                   |
 +--------------------------------------------------------+
 | board file: panel, size, rotation, touch chip, pins     |
 +--------------------------------------------------------+
```

Input goes the other way. Each renderer turns a touch into a `UiAction`, using the same rectangles it drew, and `main.cpp` never sees a pixel. The BOOT button and the big button already give actions (next, select), so units without touch keep working with either renderer.

### 1. Board files

Move everything that belongs to the board out of `Config.h` and `platformio.ini` into one header per board, chosen by the PlatformIO environment:

```ini
[env:es3c28p]
build_flags = ${env.build_flags} -DBOARD_ES3C28P
    -DILI9341_DRIVER=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=320 ...   ; TFT_eSPI's own flags, per board

[env:example-480x320]
build_flags = ${env.build_flags} -DBOARD_EXAMPLE_480X320
```

```cpp
// include/boards/es3c28p.h
#define BOARD_NAME "LCDWiki ES3C28P"
#define PANEL_W 320
#define PANEL_H 240
#define TOUCH_CHIP TOUCH_FT6336   // FT6336, GT911, CST816 or none
#define TOUCH_SDA 16
// SD bus, battery pin and divider, BOOT and big button pins...
```

`Config.h` keeps the tuning (lap timing, sessions, GPS rates) and includes the board file. CI builds every environment. See the review's [adaptability section](code-review-0.2.md#making-it-fit-more-setups) for the touch chip, SD and battery parts.

### 2. One way to draw: `gfx::Canvas`

The screens call a small interface instead of TFT_eSPI:

```cpp
namespace gfx {
enum class Anchor : uint8_t { TopLeft, TopCentre, TopRight, MiddleLeft, Middle, MiddleRight, BottomLeft, BottomCentre, BottomRight };

class Canvas {
 public:
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual void clear(uint16_t colour) = 0;
  virtual void fillRect(int x, int y, int w, int h, uint16_t c) = 0;
  virtual void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) = 0;
  virtual void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) = 0;
  virtual void fillCircle(int x, int y, int r, uint16_t c) = 0;
  virtual void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) = 0;
  virtual void line(int x0, int y0, int x1, int y1, uint16_t c) = 0;
  virtual void hLine(int x, int y, int w, uint16_t c) = 0;
  virtual void vLine(int x, int y, int h, uint16_t c) = 0;
  virtual void image(int x, int y, int w, int h, const uint16_t* pixels) = 0;  // the logos
  virtual int text(const char* s, int x, int y, Font f, uint16_t c, Anchor a) = 0;  // returns the width
  virtual int textWidth(const char* s, Font f) = 0;
  virtual void present() = 0;  // send the frame: changed rows, DMA, or a framebuffer swap
};
}
```

- **`TftCanvas`** wraps today's sprite, smooth fonts and row-by-row update. On the ES3C28P nothing changes, and the screenshots prove it (they must match pixel for pixel).
- **`LgfxCanvas`** (LovyanGFX) for other panels. It has sprites, the same kind of smooth fonts, DMA, and RGB panels on the S3.
- **The PC stand-in** becomes a `Canvas` too, so `tools/screens/mock` no longer has to imitate TFT_eSPI's classes.
- The anchor enum replaces TFT_eSPI's `TL_DATUM`/`MC_DATUM`, so screen code doesn't name the library at all.

### 3. Positions that scale: `ui::Metrics`

Screens are designed at 320×240 and placed from metrics worked out at boot from the panel's size:

```cpp
struct Metrics {
  int w, h;          // the panel, after rotation
  float s;           // scale from the 320x240 design: min(w / 320, h / 240), in steps of 0.25
  int header, footerY, footerH, margin, corner;  // HEADER_H, FOOTER_Y... times s
  SizeClass size;    // Compact (to 320x240), Medium (480x320), Large (800x480 and up)
  bool portrait;     // taller than wide
};
```

- **Most screens scale as they are.** Menus, settings, summaries and messages place things in the header, the body and the footer by proportion and by `m.s`. A 480×320 panel is 1.5× the design in both directions.
- **Other shapes get their own arrangement where it matters,** chosen per screen, not by a general rule. On an 800×480 panel (5:3), the timing screen can show the map beside the delta instead of switching between them. In portrait, the footer's three buttons stack. Screens that don't need a special case just scale and centre.
- **Every button's rectangle comes from a layout function** that both the screen and the input side call, as `HomeLayout.h` and `AutocrossLayout.h` already do. Each layout gets a test like `test_autocross_layout.cpp`, run at every size.
- **The top-bar tap height, the drag distance and the thirds** scale with `m.s` (the 0.2 review moved the first of these into `UiTheme.h`).
- **Round panels** (240×240 and 466×466 watch-style displays) are left out. Most screens would need redesigning, not placing.

### 4. Fonts for each size

`make_fonts.py` gets a `--scale` option and the build bundles the scales its board needs: 1× for 320×240, 1.5× for 480×320, 2× or 2.5× for 800×480. A board uses the set nearest its `m.s`. The `Font` names (Caption, Body, Button, Title, Hero, NumL, NumXL) don't change. Today's seven fonts are about 390 KB of source; the 1.5× set takes roughly twice the flash of the 1× set, and there are 6 MB free in the app partition.

### 5. Getting pixels out fast on bigger panels

- **480×320 over SPI:** 300 KB a frame, about 60 ms to send whole at 40 MHz. The row-by-row update already sends only what changed. Adding DMA lets the next frame be drawn while the last one is still going out.
- **800×480 RGB:** the panel refreshes itself from a framebuffer in PSRAM. Draw into a second buffer and swap them at the panel's vertical sync, so there's no tearing and no row comparison. Two buffers are 1.5 MB of the 8 MB PSRAM. The limit becomes PSRAM bandwidth, shared with the session log, so a full redraw at 10 fps is the most to plan for; still screens already redraw less.
- **Arduino core:** LovyanGFX drives RGB panels on the current core (Arduino-ESP32 2.0.17). LVGL 9's ready-made ESP32 display drivers, and ESP-IDF's bounce buffers for RGB panels, need core 3 (ESP-IDF 5), which PlatformIO gets through the community `pioarduino` platform. Moving to core 3 is its own step, with its own testing on the board.

### 6. An LVGL renderer, if wanted

- **When it's worth it:** a board of 480×320 or more where LVGL's look and animations are wanted, or if configurable dash pages ever come back (they're under "Not planned" in the [roadmap](../ROADMAP.md)).
- **How:** LVGL 9 as a library, one `lv_display` that flushes through LovyanGFX or `esp_lcd`, one touch `lv_indev`, and a keypad `lv_indev` with a group for the BOOT and big buttons. Each screen model gets an LVGL view that builds its widgets when the screen opens and only changes labels and colours after that. LVGL then redraws only what changed, which suits big panels.
- **Fonts:** `lv_font_conv` from the same Inter files, at the sizes in the same table as `make_fonts.py`.
- **Cost:** all 31 screens drawn a second way (about 2,700 lines today), and every new screen built twice from then on. About 250–400 KB more flash. LVGL's draw buffers want internal RAM that DMA can reach (a tenth of an 800×480 screen is 77 KB), next to Wi-Fi, NimBLE and the web server. Measure the internal heap with everything running before committing to it.
- **Recommendation:** don't make LVGL the only renderer, and don't start with it. Most "LVGL boards" are panels LovyanGFX drives directly. Get every size working on the Canvas first (steps 1–7 below). Add the LVGL renderer only for a board where it's wanted, once the screen models exist and both renderers can be fed the same thing.

### 7. Testing every size

- `render_screens.py --size 480x320` (and 240x320, 800x480) renders every screen at that size, and CI renders each board's size. The README pictures stay 320×240.
- The PC canvas records where every piece of text and every button went. A check fails when text runs off the screen or out of its button, or two buttons overlap. That catches most layout bugs without the board.
- The 320×240 renders must stay identical to today's through steps 2–5. That proves the refactor changed nothing on the ES3C28P.

## Steps

| Step | What | Size | Needs | Built by |
|---|---|---|---|---|
| 1 | **Board files:** pins and the panel out of `Config.h` and `[env]` into `boards/<board>.h` and `[env:<board>]`; CI builds each one | S/M | — | Sonnet, medium |
| 2 | **Split main.cpp into screen controllers** that hand out screen models and take `UiAction`s, with one BOOT-button convention (review items C8, C9) | L | — | Sonnet, medium, in about six steps, one screen group each |
| 3 | **`gfx::Canvas`,** with the TFT_eSPI backend and the PC stand-in as a Canvas. Screenshots identical | M | 2 helps, not needed | Sonnet, medium |
| 4 | **Services out of the drawing code:** the top bar's status (battery, GPS, BT, OBD, camera, coolant) and the other reads passed in as a model | S/M | 3 | Sonnet, low |
| 5 | **`ui::Metrics` and layout functions for every screen,** rendered at 480×320 and 240×320 in CI with the overflow check | L | 3, 4 | Sonnet, medium, screen by screen |
| 6 | **Font sets per scale** from `make_fonts.py` | S | 5 | Haiku, low |
| 7 | **LovyanGFX backend and a second board,** for example a 3.5" 480×320 ESP32-S3 board (ST7796 with an FT6336 or GT911 is a common pairing), with its touch driver | M | 1, 3, 5, 6 | Sonnet, medium, then Dylan on the board |
| 8 | **DMA, and the RGB framebuffer path,** for 800×480 boards (possibly with the move to Arduino core 3) | M | 7 | Sonnet, medium, then Dylan on the board |
| 9 | **LVGL renderer** (optional) | XL | 2, 4, 7 | Opus plans it, Sonnet builds it screen by screen |

Steps 1 and 2 are worth doing for their own sake: board files make Track Rat easier to build for anyone's hardware, and the main.cpp split is the biggest readability fix left (see the review).

## Risks

- **Two renderers drift apart.** Only add LVGL if it's wanted, and test both from the same screen models.
- **Bugs that only show on the board:** tearing, touch orientation, panel colour order and inversion, DMA timing. Each new board needs a session on the real hardware before it's offered in the web installer.
- **Flash for fonts** grows with each scale bundled. Bundle only what the board needs.
- **Units without touch** must keep working with every layout. The BOOT order comes from the same layout functions.
