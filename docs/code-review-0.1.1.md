# Code review for 0.1.1

A review of the whole firmware, the Wi-Fi page, the website and the tools, done on 2026-10-25 after beta 3 (`v0.1.0-beta.3`). Three read-only reviews looked at security, efficiency, and reuse and clean-up, and the findings were checked and sorted into what 0.1.1 fixes and what waits. Back to the [README](../README.md).

Timings in the efficiency part are estimates from reading the code, not measurements on the board.

## In short

**What holds up well:**

- Every parser has bounds: UBX, NMEA, VBO, the OBD replies, the Bluetooth packets and the JSON files. File paths from the Wi-Fi page are checked. No XSS was found in the page, and there are no secrets in the repo.
- Logging can't lose more than about a second. Fixes go through a lock-free ring buffer in PSRAM to a writer on the other core. Saves are atomic (write a temporary file, then rename). Boot recovery rebuilds anything a power cut left half-written.
- Threads share state through spinlocks, mutexes and stream buffers, with nothing shared without a lock. Memory is bounded everywhere: fixed maximums for laps, traces and points, and no leaks found.
- Lap timing uses GPS time, so none of the slow spots below change a recorded lap time. They delay the screen and, in the worst case, drop fixes.

**The weak points:**

1. **The Wi-Fi network is open.** Anyone in range can join, read the upload code as it's typed, and install firmware. A failed-code lockout that never expires lets anyone lock the owner out.
2. **Big folders make the card slow.** Listing `/sessions` opens every file by path, and FAT searches the folder from the start each time. With a season of logs (about 2000 files), boot recovery and the Autocross screens take minutes of card time.
3. **Saves happen on the timing loop.** After every lap, the session summary is re-read twice and rewritten. `tracks.json` (273 KB) is re-parsed at session start and when checking the layout. Each costs 100–500 ms of screen time.
4. **The main loop never sleeps.** It spins at full speed on every screen, which wastes battery.
5. **main.cpp is too big:** 2365 lines and 98 statics. The 0.1.1 fixes stay small, and splitting it is planned separately.

## Security

| ID | Finding | Fix | Size | Status |
|---|---|---|---|---|
| S1 | **The Wi-Fi network has no password.** The 4-digit code travels in clear text and stays valid all session, so someone nearby can capture it and upload firmware or tracks. | A WPA2 password on the network, or a code that changes after each use. | S/M | 0.1.1, decision pending |
| S2 | **Five wrong codes lock the page until restart, for everyone.** Anyone on the network can lock the owner out. | A temporary lockout per address: 30 s, doubling each time. | S | 0.1.1 |
| S3 | **Uploaded firmware isn't checked to be Track Rat's,** and a bad image can't roll back. | Check that the image's project name is `trackrat`. Use rollback if the Arduino bootloader supports it, and mark the app good after a healthy boot. | S/M | 0.1.1 |
| S4 | **Restart and set-time need no code,** and requests from other sites aren't refused. | Require the code for restart. Refuse a POST whose `Origin` isn't the board. | S | 0.1.1 |
| S5 | **Reads are open:** session downloads, the session list, the position in `/api/info`, and the car VINs. | A Wi-Fi password (S1) covers this. Without one, drop the position from `/api/info`. | S | 0.1.1, with S1 |
| S6 | **A tracks upload has no size limit and isn't validated before it replaces the file.** The server only checks that it starts with `{`, and the whole file is read into memory at boot. | Cap it at 2 MB. Validate the temporary file before the rename. Check free space. | S | 0.1.1 |
| S7 | **Large request bodies are buffered before the code is checked.** | Refuse by `Content-Length` before reading. | S | 0.1.1 |
| S8 | **Several downloads at once can starve the logger.** | At most two downloads at a time. | S | 0.1.1 |
| S9 | **The Bluetooth (RaceBox/RaceChrono) side has no pairing,** so a stranger can hold the only connection. | Document it. Drop a connection that never subscribes. | S | 0.1.1 |
| S10 | **The installer page loads ESP Web Tools from unpkg without an integrity hash.** | Pin it with `integrity`, or host it on trackrat.ca. | S | 0.1.1 |
| S11 | **A spoofed OBD adapter could send a fake VIN** and switch the car profile. | Bluetooth bonding. | M | Later |
| S12 | **The VBO reader doesn't limit the channel or comment count,** and casts NaN or out-of-range values to integers. | Caps and clamps. | S | 0.1.1 |
| S13 | **No security headers on the Wi-Fi page.** | `X-Frame-Options`, a content security policy, `nosniff`. | S | 0.1.1 |
| S14 | **Host tests run without sanitizers.** | A CI job with AddressSanitizer and UndefinedBehaviorSanitizer. | S | 0.1.1 if it passes; otherwise later |

## Efficiency and reliability

| ID | Finding | Cost (estimated) | Fix | Size | Status |
|---|---|---|---|---|---|
| E1 | **Folder listings open every entry by path,** which is O(n²) on FAT. Affects `CourseRuns`, `AdjustLoad`, boot recovery and re-timing. The Wi-Fi page already avoids it with `f_readdir`. | About 85 ms per loop pass at 500 files, and 340 ms at 2000 files, for 7 s to 2 min | `Storage::listDir` built on `f_readdir`, with a time budget per pass instead of 6 entries | M | 0.1.1 |
| E2 | **Boot recovery re-checks every session on every boot.** | About 3 searches per file: minutes at 2000 files. Holds the card the whole time. | A marker for the open session, so recovery only looks at that one | S/M | 0.1.1 |
| E3 | **The summary is rewritten on the timing loop after every lap.** The old file is parsed twice (camera marks, then sync marks). | 60–150 ms, up to 500 ms with a big folder, just after the line | Parse once. Save about a second later, outside the lap event. | M | 0.1.1 |
| E4 | **`tracks.json` is fully parsed at session start and for each layout check.** One check comes right after the first lap. | 100–250 ms each | Keep every track's map in PSRAM (about 90 KB). Parse the file only to write it. | M | 0.1.1 |
| E5 | **The fix queue holds 64 fixes (3.2 s)** and drops the oldest, the ones most likely to hold a crossing. | Lost fixes after a long stall | 256 entries in PSRAM | S | 0.1.1 |
| E6 | **The main loop never yields.** | About 20–30 mA wasted | Sleep 1 ms when nothing is waiting | S | 0.1.1 |
| E7 | **Every screen pays for a full frame:** clear, compare and push 150 KB, about 25–45 ms of every 100 ms, even on menus. | Battery and frame time | Redraw menus and other still screens only on input or change, at most twice a second. Dirty rectangles and DMA later. | S (M later) | 0.1.1, part |
| E8 | **Small JSON documents use internal RAM,** the pool Bluetooth and Wi-Fi need. | Fragmentation | A PSRAM allocator for ArduinoJson | S | 0.1.1 |
| E9 | **Start-up probes the GPS for about 3 s,** most of it hidden by the splash. | Slower boot when the splash is skipped | Remember the GPS pins and baud rate, and try them first | S | 0.1.1 |
| E10 | **Off the line, every fix searches the whole reference lap.** | 1–10 ms per fix in the pit lane | Do the full search at most once a second | S | 0.1.1 |
| E11 | **`LoopFinder::retraced` repeats its search for each new point near a pass.** | 5–20 ms | Remember a "no" for that pass | S | 0.1.1 |
| E12 | **Fixes read together after a stall share one arrival time,** so the on-screen lap clock can start late. | Display only | Time each fix from GPS time | S | Later |
| E13 | **The I2C bus waits 50 ms when it's stuck,** on every IMU and touch poll. | Stalls if the bus wedges | `Wire.setTimeOut(5)` | S | 0.1.1 |
| E14 | **Switching cars mid-session writes flash,** which can overrun the GPS UART. | A lost fix | Apply a new VIN's profile when the session ends | S | 0.1.1 |
| E15 | **Saving a long drive without an SD card needs up to 1.6 MB in one block.** | The drive isn't saved if the allocation fails | Write it in a stream | S | Later |
| E16 | **A removed card isn't noticed.** Each failing call may wait out a timeout. | Unverified | Notice and stop using the card | S | Later |

## Reuse and clean-up

| ID | Finding | Fix | Size | Status |
|---|---|---|---|---|
| C1 | **Stale text.** The user guide lists the Adjust screen's BOOT order wrongly (it's Later, Cancel, Save, Earlier, Reset). Also: the header comment of main.cpp, a misplaced comment, and the code layout list in development.md. | Correct them | S | 0.1.1 |
| C2 | **The Autocross hit zones are off by 2 pixels from the drawn rows.** main.cpp tests y ≥ 150/190/192; the screen draws rows at 152 and 194. | `AutocrossLayout.h`, shared by both, with a test | S | 0.1.1 |
| C3 | **Lap time formatting is written twice** (SessionLog and the screen renderer) | `TimeFormat.h`, with a test | S | 0.1.1 |
| C4 | **Atomic saves are written five ways,** and `BestLapStore` removes the old file *before* renaming the new one, so a power cut there loses the best lap. | One `SafeFile` helper (write, rename, recover), used everywhere | M | 0.1.1 |
| C5 | **Dead code:** `currentLapTags`, `currentLapCoolantMaxC`, `gpsBadge`, `cardBottom`, `lastCommand`, `heardFromApp` | Remove | S | 0.1.1 |
| C6 | **Test helpers are copied** across 8 test files (start/finish constants, circle points, metres per degree). | `test/geo_helpers.h` and one golden `.vbo` | M | Later |
| C7 | **About 10 button-drawing lambdas** across the screens | `DisplayManager::button` | M | Later |
| C8 | **BOOT navigation is handled per screen,** and Adjust's order is the reverse of the others. | A `BootNav` helper and one convention | M | Later |
| C9 | **main.cpp: 2365 lines, 98 statics** | Split into screens: state structs first, then one file per screen. About six steps, after C2 and C8. | L | Later (0.2) |
| C10 | **Session file-name rules are written in five places** (the web page, Courses, Logbook, AdjustLine, SessionLog) | `SessionName.h`, with a fixture shared with the web test | M | Later |
| C11 | **Three VBO readers** (C++, JavaScript, Python) | One golden file all three are tested against | M | Later |
| C12 | **Settings are listed by hand in four places** | A table describing each setting | L | Later |
| C13 | **The CAN code (`CAN_ENABLED`) and the NMEA fallback** may not earn their keep | Decide: keep and label experimental, or remove | S | Later |
| C14 | **The test list is in both ci.yml and development.md,** and the screen renderer isn't built in CI. | One list; build the renderer in CI | S | 0.1.1 (the renderer) |

## How 0.1.1 is built

The fixes are split so no two agents touch the same files:

- **Screenshots and CI:** C14. The README images are regenerated, and CI fails when they're stale.
- **Storage:** E1–E4 (the parts in storage code), E8, C3 and C4.
- **Main loop and screens:** E3 (save later), E6, E7, E14, C1, C2 and C5 (screen parts).
- **GPS and timing:** E5, E9, E10, E11, E13 and C5 (timing parts).
- **Wi-Fi page and security:** S1–S10, S12 and S13, after the Wi-Fi password decision.

S14 runs last, once everything is merged.
