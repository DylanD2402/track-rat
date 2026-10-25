# Contributing to Track Rat

Thanks for helping. Track Rat is an MIT-licensed GPS lap timer for the LCDWiki ES3C28P board. Your changes are under the same [MIT licence](LICENSE).

## Build

You need [PlatformIO](https://platformio.org/) (the VS Code extension, or `pip install platformio`).

```bash
pio run -e es3c28p
```

To flash the board, see [Build and flash](README.md#build-and-flash) in the README.

## Host tests

The logic that doesn't need Arduino is tested on the PC. Each test is a `g++` command in the [README's Tests section](README.md#tests), and the same commands run in [`.github/workflows/ci.yml`](.github/workflows/ci.yml). On Windows, add `-D_USE_MATH_DEFINES` to each one. When you add a test, add its command to the workflow too.

## Screen renderer

[`tools/render_screens.py`](tools/render_screens.py) draws the real screen code on your PC, so you can check a screen without the board:

```bash
pip install pillow
python tools/render_screens.py
```

It needs a C++ compiler. On Windows, set `CXX` to one, for example:

```bat
set CXX=zig c++ -D_USE_MATH_DEFINES
python tools/render_screens.py
```

(In Git Bash: `CXX="zig c++ -D_USE_MATH_DEFINES" python tools/render_screens.py`.)

## House rules

- **New logic goes in Arduino-free headers, with a host test.** See [`include/LapTimer.h`](include/LapTimer.h) and [`include/TempAlert.h`](include/TempAlert.h) for the pattern.
- **Match the surrounding code.** Same naming, same layout, same comment style.
- **Update the README or docs with the feature.** If a user would notice it, say how it works.
- **Keep the firmware working with no SD card, no GPS fix and no optional hardware.** Add-ons (the big button, the LED bar, the IMU, CAN, Bluetooth adapters, the camera) must do nothing when they're absent.

## Reporting a bug

Open an issue with the **Bug report** form. It asks for:

1. **The firmware version,** from the bottom of the Settings list (for example `v0.1.0-beta.1`).
2. **A log** from the board, taken with:

   ```bash
   python tools/serial_log.py --port COM4 --seconds 30 --reset
   ```

   Change `COM4` to your port. `--reset` restarts the board first, so the log covers startup.
3. **The session's `.vbo` and `.json` files** from the SD card, if the bug is about a session.
4. **Remove your home GPS positions** before you post. A log or a session file can show where you live, including where the board sits when you test at home. Cut those lines out, or post the file in a private message instead.

## Pull request checklist

The [pull request template](.github/pull_request_template.md) has the checklist. In short:

- `pio run -e es3c28p` builds.
- The host tests pass.
- The README or docs are updated for any feature.
- You've tried the change on hardware, or say that you haven't.
