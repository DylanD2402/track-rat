# Third-party licences

Track Rat's own code is MIT (see [LICENSE](LICENSE)). The track data has its own licence, see [DATA_LICENSE.md](DATA_LICENSE.md).

The firmware binary also contains the libraries and parts listed below, under their own licences. The LGPL ones are used unmodified at the pinned versions. This repository has everything needed to rebuild and relink the firmware: `platformio.ini` pins each version, and the build steps are in the [flashing guide](docs/flashing.md#build-from-source-with-platformio).

| Component | Version | Licence | Link | Where it's used |
|---|---|---|---|---|
| TFT_eSPI | 2.5.43 | BSD-style: FreeBSD for Bodmer's code, BSD for the Adafruit GFX parts, MIT for the original Adafruit ILI9341 code (see its `license.txt`) | [github.com/Bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | The display driver (`src/DisplayManager.cpp`, `src/Splash.cpp`) |
| ArduinoJson | 6.21.6 | MIT | [github.com/bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Settings, sessions, tracks and the Wi-Fi page's JSON (`src/JsonFile.cpp`, `src/SessionLog.cpp`, `src/TrackDatabase.cpp`) |
| TinyGPSPlus | 1.1.0 | LGPL-2.1 or later | [github.com/mikalhart/TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | NMEA fallback for GPS modules that aren't u-blox (`src/GPSHandler.cpp`) |
| NimBLE-Arduino | 1.4.3 | Apache-2.0 | [github.com/h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | Bluetooth: RaceBox and RaceChrono profiles, the OBD-II adapter and the DJI camera (`src/RaceBoxBle.cpp`, `src/RaceChronoBle.cpp`, `src/ObdBle.cpp`, `src/CameraBle.cpp`) |
| AsyncTCP (ESP32Async) | 3.5.0 | LGPL-3.0 | [github.com/ESP32Async/AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | The Wi-Fi page's network layer (`src/WifiPortal.cpp`) |
| ESPAsyncWebServer (ESP32Async) | 3.12.1 | LGPL-3.0 | [github.com/ESP32Async/ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | The Wi-Fi page's web server (`src/WifiPortal.cpp`) |
| Arduino-ESP32 core (framework `espressif32@6.9.0`) | 2.0.17 | LGPL-2.1 or later | [github.com/espressif/arduino-esp32](https://github.com/espressif/arduino-esp32) | The Arduino framework the firmware is built on |
| ESP-IDF (inside the Arduino-ESP32 core) | 4.4 series, as shipped with core 2.0.17 | Apache-2.0 | [github.com/espressif/esp-idf](https://github.com/espressif/esp-idf) | The ESP32-S3 SDK: Wi-Fi, Bluetooth, drivers and the flash and file system |
| Inter (typeface) | Version as in `include/fonts/` and `web/fonts/` | SIL Open Font License 1.1 | [OFL text](include/fonts/OFL.txt) (also [web/fonts/OFL.txt](web/fonts/OFL.txt)); [github.com/rsms/inter](https://github.com/rsms/inter) | Every screen on the board (`include/fonts/Inter*.h`) and the Wi-Fi page (`web/fonts/`) |
| DJI Osmo GPS Controller Demo | Ported in `include/DjiRemote.h` | MIT | [github.com/dji-sdk/Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo) | The protocol layer for Track Rat as the Osmo Action's GPS remote (`include/DjiRemote.h`) |
| DJI R SDK terms | Current policy | DJI's own terms, not an open licence | [developer.dji.com/policies/eula](http://developer.dji.com/policies/eula/) | The DJI camera protocol, as used by `include/DjiRemote.h` and `src/CameraBle.cpp` |

## Notes

- **DJI.** The demo is MIT-licensed, but the protocol it talks is covered by DJI's SDK terms, and those apply to any use of the camera protocol. Track Rat's port keeps the demo's licence notice in `include/DjiRemote.h`.
- **Inter.** The OFL allows use, and bundling in a device, with its licence text kept with the font files. The text is in `include/fonts/OFL.txt`.
- **Track Rat's own tools.** `tools/overlay.py` uses Pillow and imageio-ffmpeg, listed in `tools/requirements-overlay.txt`. They are used only by the PC overlay tool and aren't in the firmware, so they aren't listed above.
