# Code review for 0.2

A clean-up review of the whole firmware, the tests and the tools, done on 2026-11-04 after v0.2.0-beta.2. It looks for code that is unused, not needed or written twice, for code that is hard to read, and for ways to make Track Rat faster and easier to fit to other hardware. The plan for other screen sizes and LVGL is its own document: [display-plan.md](display-plan.md). The previous review is [code-review-0.1.1.md](code-review-0.1.1.md). Back to the [README](../README.md).

Timings and currents here are estimates from reading the code, not measurements on the board.

## How it was checked

- **The compiler.** A full firmware build with `-Wall -Wextra -Wunused -Wunused-function -Wunused-variable -Wunused-but-set-variable -Wunused-const-variable`.
- **Every declared name against its uses.** A script listed every function, method, constant, macro, type, field and enum value declared in `include/` and `src/`, then counted where each is used, with comments and strings left out, in the firmware, the tests and the screen renderer.
- **Copies.** A scan for runs of 6 or more matching lines (spacing and comments ignored) across `include/`, `src/` and `test/`.
- **Reading.** The display, input, main loop, settings and Bluetooth code, and how the board's hardware reaches the code.
- **Before and after every change:** the firmware build, every host test suite (46 before, 47 with the new one), and every one of the 127 screens rendered on the PC and compared pixel for pixel.

## In short

**What holds up well:**

- The code is in good shape for its size (about 28,000 lines of C++). With every warning turned on, the compiler found 4 warnings in Track Rat's own code. The name scan found 4 names that nothing uses and a few used only by tests, and the copy scan found one real copy of any size (the two Bluetooth device lists).
- The logic that matters is pure C++ in headers (timing, sessions, settings, parsers, layouts) and tested on a PC. The comments say why, not just what.

**The weak points:**

1. **`main.cpp` is still too big:** 2,450 lines and about 100 statics. `handleInput()` is 286 lines, `loop()` 181, `render()` 156, `enterState()` 147. It's the hardest file to read and the riskiest to change.
2. **The screens are tied to 320×240 and to TFT_eSPI.** About 320 drawing calls use pixel positions for this panel, and the fonts are fixed sizes. See the [display plan](display-plan.md).
3. **The board is wired into the code.** Pins, the panel, the touch chip (FT6336 registers read directly), the SD bus (4-bit SDIO) and the battery divider are spread over `Config.h`, `platformio.ini` and the drivers, so a different board means editing all three.

## Fixed in this pass

| ID | Finding | Fix | Status |
|---|---|---|---|
| R1 | **Two identical Bluetooth device lists.** `ObdBle.h` and `CameraBle.h` each declared the same `Found` struct and `MAX_FOUND`, and their scans each had the same strongest-first insert | `BleFound.h` (pure C++, with `test_blefound.cpp`); both keep their own names through `using` | Fixed |
| R2 | **The Engine data and Camera screens drew the same Find/Pair list page** | One shared list page and one set of status-page buttons in `DisplaySettings.cpp` | Fixed |
| R3 | **Battery mode names written twice:** a table in Settings > Battery, and `battery::modeName()`, which nothing called | The screen uses `modeName()` | Fixed |
| R4 | **Session length worked out twice:** `situation::sessionLengthMin()` with its own table (used only by a test) and `settings::sessionMinutes()` (used by the firmware) | The first is gone; the test checks `sessionMinutes()` | Fixed |
| R5 | **The same "read a whole log into a replay" loop** in boot recovery and in re-timing | One helper in `SessionLog.cpp`, which also frees its second 4 KB buffer (static RAM 89,336 to 85,240 bytes) | Fixed |
| R6 | **The screen size written twice:** `InputManager.cpp` had its own 320 and 240, and the top bar's tap height was a bare 30 | `ui::W`, `ui::H` and a new `ui::HEADER_TAP_H` in `UiTheme.h` | Fixed |
| R7 | **Unused code:** `Profiles::sessionLocked()`, `DisplayManager::BRAND_MARK_W` | Removed | Fixed |
| R8 | **Compiler warnings:** the placeholder logo function compiled when the real logo is used, `/*` inside a comment (twice), and two `snprintf` that might truncate | Each fixed without changing behaviour | Fixed |
| R9 | **Test helpers copied between test files** (0.1.1's C6): metres per degree, the circle a simulated car drives, the drive-to and brake-to-a-stop code, a UBX frame | `test/geo_helpers.h` and `test/ubx_helpers.h`. Copies that differ in what they test were left | Fixed |

What was looked at and kept:

- **Names used only by tests** (`UbxParser::checksumErrors()`, `msgClass()`, `msgId()`, `linenudge::sliceNear()`, `SettingsFlow::cardBottom()` and a few more): each lets a test check something the firmware relies on. Kept.
- **`DEBUG_PRINT`:** unused, but it's one of a set of three debug macros. Kept.
- **The CAN code** (`CAN_ENABLED`, off by default, untested on hardware; 0.1.1's C13). It's 105 lines, compiled out, and shares the OBD poller with the Bluetooth adapter, so it costs almost nothing to keep. Kept, and still marked untested in `Config.h`.
- **The NMEA fallback:** it's what makes non-u-blox GPS modules work at all. Kept (see "More GPS modules" below).
- **The tools:** every script in `tools/` is used by CI, the release workflow or the docs.

## Still to do

### Readability

| ID | Finding | Fix | Size | Status |
|---|---|---|---|---|
| C9 | **`main.cpp`: 2,450 lines, about 100 statics,** four functions over 140 lines (from 0.1.1) | Split it into screen controllers, each with its own state, an `enter()`, an input handler and the model its screen draws. A small `App` struct holds what's shared (the GPS, the timer, the track database, the session). About six steps, one group of screens each. This is also step 2 of the display plan | L | Next |
| C8 | **BOOT navigation is handled screen by screen,** 23 places in `main.cpp`, and the Adjust screen's order is the reverse of the others (from 0.1.1) | One convention, in the screen controllers | M | With C9 |
| C7 | **Three versions of a button:** `DisplayManager::button()`, and lambdas in `DisplayAdjust.cpp` and `DisplayAutocross.cpp` that add a greyed-out state and a font choice (from 0.1.1, now down from about 10) | One `button()` with an enabled flag and a font, checked against the screenshots | S | Later |
| C10 | **Session file-name rules in five places** (from 0.1.1) | `SessionName.h`, with a fixture shared with the web test | M | Later |
| C11 | **Three VBO readers** (C++, JavaScript, Python) (from 0.1.1) | One golden file all three are tested against | M | Later |
| C12 | **Settings listed by hand in four places** (from 0.1.1) | A table describing each setting | L | Later |
| C15 | **No formatter config.** The code is consistently formatted by hand (2 spaces, about 120 columns, 32 lines over 140) | A `.clang-format` that matches today's style, used on changed lines only, so no file is reformatted at once | S | Later |
| C16 | **The OBD and camera Bluetooth tasks** (`ObdBle.cpp`, `CameraBle.cpp`) still share the shape of their task loop: scan on request, connect with the radio lock, retry every 5–10 s, forget | A shared Bluetooth-central helper, once both have been tried with a real adapter and camera (neither has yet) | M | Later |

### Carried from 0.1.1 (not re-checked in this pass)

E12 (fixes read together after a stall share one arrival time), E15 (saving a long drive with no SD card needs one big block), E16 (a removed card isn't noticed), and S11 (Bluetooth bonding for the OBD adapter). All still Later.

## Making it faster

| ID | Idea | Why | Size |
|---|---|---|---|
| F1 | **DMA for the panel, and draw while it sends.** TFT_eSPI can push the sprite by DMA. Then the next frame is drawn while the last one goes out | Sending all 320×240 takes about 50 ms at 40 MHz; today the loop waits for it | M |
| F2 | **Know what changed while drawing, instead of after.** Each frame compares the 150 KB sprite with a 150 KB copy of the panel, then copies the changed rows. Two sprites, swapped each frame, drop the copy; marking the bands each screen touches drops most of the compare | Two passes over 150 KB of PSRAM every frame, on every live screen | M |
| F3 | **Fewer temporary Strings when drawing.** Every `text()` call builds an Arduino `String`, and `fit()` re-measures a long name once per character it removes | Heap churn in internal RAM (the pool Wi-Fi and Bluetooth need), at 10 frames a second | S |
| F4 | **A lower CPU clock where nothing is live.** Try 160 MHz while timing and 80 MHz on menus, settings and the Wi-Fi page. Measure the current and the frame time before keeping it | Possibly 10–20 mA (estimate). The radios need at least 80 MHz | S |
| F5 | **Arduino core 3 (ESP-IDF 5).** Newer NimBLE, `esp_lcd` with RGB bounce buffers, and the drivers LVGL 9 expects. PlatformIO gets it through the community `pioarduino` platform | Needed for the big RGB panels in the display plan; some APIs change (`ledcSetup`, SD_MMC) | M/L |

## Making it fit more setups

Track Rat already adapts at runtime more than most: it finds the GPS on any of four pin pairs at any baud, tells u-blox from NMEA, switches add-ons off when the GPS took their pin, and works with BOOT alone when there's no touch screen. What still needs a rebuild:

| ID | Idea | Why | Size |
|---|---|---|---|
| A1 | **Board files.** `include/boards/<board>.h` with the panel, its size, the touch chip, the SD bus, the battery pin and divider, and the buttons, picked by `[env:<board>]` in `platformio.ini`. `Config.h` keeps the tuning only. CI builds every board | Today a new board means editing `Config.h`, `platformio.ini` and the drivers. This is step 1 of the display plan | S/M |
| A2 | **Touch drivers behind one interface:** FT6336 (today), GT911, CST816, and the resistive XPT2046, found by probing the I2C addresses at boot | The FT6336's registers are read directly in `InputManager.cpp` | M |
| A3 | **Touch orientation as a setting.** A "tap the corners" step in first-boot setup, instead of `TOUCH_SWAP_XY` and `TOUCH_INVERT_X/Y` at build time | Panels are mounted every way round | S |
| A4 | **SD card over SPI,** as well as 4-bit and 1-bit SDIO | Many boards wire the card to SPI | S |
| A5 | **More GPS modules at their full rate.** NMEA modules are read at whatever rate they start at, often 1 Hz. Send the rate command for the common chips: MediaTek (`PMTK220`), Quectel L76 and LC86 (`PAIR050`), and the u-blox M8 (which takes UBX already) | A cheap 10 Hz module would then time laps properly | S/M |
| A6 | **Batteries:** a fuel gauge chip (MAX17048 and similar) when one is found, and no battery code at all on boards without a battery pin | Voltage-only charge estimates are what the 0.2 battery work had to fight | M |
| A7 | **Add-on pins in a file on the SD card** (`/config.json`: the big button, the IMU's I2C, the CAN pins) | No rebuild to move an add-on | S |
| A8 | **What a board must have:** an ESP32-S3 (Bluetooth LE and USB), PSRAM (the frame, the log buffer and the fix history live there), and a panel of at least 320×240. Write this in `docs/hardware.md` | So nobody buys a board that can't work | S |

## Housekeeping

- **Old worktrees.** There are over 20 worktrees under `.claude/worktrees` from earlier sessions and agents. Their branches are merged into `main`, except `settings-v1-backup`. They can be removed (the desktop app's worktree clean-up does it), which also stops `git grep` and editors from finding copies of old code.
