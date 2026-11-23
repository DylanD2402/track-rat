# Track Rat

An open-source GPS lap timer for the LCDWiki ES3C28P board (ESP32-S3, 320×240 touch screen), built with PlatformIO and Arduino. The repo also holds the Wi-Fi page the device serves (`web/`), the project website (`site/`, published at trackrat.ca), Python tools (`tools/`) and tests that run on a PC (`test/`).

Usage is limited, so Opus plans, cheaper models build, and everyone reads only what the task needs.

## Who does what

The main session runs on Sonnet and coordinates. (If this session is on Opus, say so once at the start, so Dylan can switch the model picker to Sonnet.) Three agents in `.claude/agents/` do the rest:

| Agent | Model | Does |
|---|---|---|
| `planner` | Opus | Reads the code, makes the design decisions and writes the brief. Edits nothing. |
| `builder` | Sonnet, medium effort | Builds one code step from a brief, in its own git worktree. |
| `docs` | Haiku, low effort | Documentation and images, in its own git worktree. |

How the main session routes a request:

- **A new feature, a roadmap step, a change across several files, or anything touching lap timing, GPS, saving or firmware update:** find the files and the roadmap section involved, then give the `planner` the request with those paths, so Opus reads little. When the brief comes back, show Dylan a summary of it. If he asked for it to be built, carry on. If he asked for a plan or an idea, wait for his OK.
- **Building:** one `builder` per code step and one `docs` agent per docs step. Pass each the brief for its step word for word: it has seen nothing else. Steps that touch different files run side by side.
- **A small fix in one or two files, where what to do is already clear:** do it in the main session. No planner, no builder.
- **When an agent reports back:** read the diff of its branch, run `python tools/run_tests.py` and `pio run -e es3c28p -s`, then merge it. If the diff touches lap timing, GPS, saving or firmware update, have the `planner` review the diff before merging.
- **If the planner can't run** (the Opus limit is reached), tell Dylan and ask whether to plan on Sonnet. Don't switch without saying so.
- Don't push, tag or release unless Dylan asks.

## Commands

- **Host tests:** `python tools/run_tests.py` runs them all and prints only failures. `python tools/run_tests.py laptimer vbo` runs the tests whose names contain those words; `--list` shows the names. It reads its list from `.github/workflows/ci.yml` and finds a compiler itself (g++, `CXX`, or the `ziglang` pip package), so don't type the `g++` commands from the docs by hand.
- **Firmware build:** `pio run -e es3c28p -s` (`-s` keeps the output short).
- **Screens on the PC:** `python tools/render_screens.py` writes each screen to `build/screens/*.png`. `python tools/check_screens.py` fails when `docs/images` is out of date; `python tools/render_screens.py && python tools/readme_images.py` fixes it.
- **Never flash the board:** no `-t upload`, no `-t uploadfs`, no `tools/autoflash.py`. Dylan flashes and tries it on hardware.

## House rules

- New logic goes in an Arduino-free header in `include/` with a host test in `test/`. `include/LapTimer.h` and `include/TempAlert.h` show the pattern. Add the test's step to the Host tests job of `.github/workflows/ci.yml` and describe it in the Tests section of `docs/development.md`.
- Match the surrounding code: same naming, layout and comment style.
- When a user would notice the change, update the README or `docs/`, and add it to the Unreleased section of `CHANGELOG.md`.
- The firmware must keep working with no SD card, no GPS fix and no optional hardware. Add-ons do nothing when they're absent.
- Say plainly what was tried on the board and what wasn't. Passing host tests and renders is not the same as tried on hardware.
- Never commit real GPS logs or positions from Dylan's home.

## Finding things without reading everything

- **The map of the code** is the "Layout" section at the end of `docs/development.md`. Read that section, not the whole file: most of the file describes each test.
- **Plans:** `ROADMAP.md` holds what's open (Phase 8 candidates, Phase 10, Phase 11). Finished phases are in `ROADMAP-ARCHIVE.md`; open it only for the history of a feature. Phase 11's design is `docs/display-plan.md`.
- **Large files: search them and read the part you need.** `web/index.html` (about 170 KB), `test/test_web.js` and `test/test_settings.cpp` (about 100 KB each), `docs/user-guide.md` (80 KB), `src/WifiPortal.cpp` (66 KB), `src/DisplayManager.cpp` (62 KB), `src/app/Session.cpp` (51 KB), `src/TrackDatabase.cpp` (47 KB).
- Compile-time options (pins, timings, feature switches) are in `include/Config.h`.

## Compact instructions

When compacting, keep: the brief being built, the branches waiting to be merged, the latest test and build results, and what Dylan decided.
