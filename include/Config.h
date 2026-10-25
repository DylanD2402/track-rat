/**
 * Track Rat - Configuration
 *
 * Board: LCDWiki ES3C28P (ESP32-S3 N16R8, 2.8" ILI9341V IPS, FT6336G touch, SDIO SD slot)
 * GPS:   Beitian BE-252Q (UART: binary UBX-NAV-PVT, or NMEA from other modules)
 *
 * Display pins are passed to TFT_eSPI from platformio.ini build_flags.
 */

#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
// GPS (Beitian BE-252Q)
// Plug it into the board's UART connector (GPIO 43/44) or the expansion
// header (GPIO 14/21). At boot every pair below is tried, both ways round,
// so TX/RX orientation doesn't matter. {ESP32 RX pin, ESP32 TX pin}
// ---------------------------------------------------------------------------
#define GPS_PIN_PAIRS {{44, 43}, {43, 44}, {14, 21}, {21, 14}}
#define GPS_DEFAULT_BAUD 115200      // BE-252Q factory default
// Update rate vs satellite systems (u-blox M10 maximums): 4 systems 10 Hz,
// 3: 16 Hz, 2: 20 Hz, GPS only: 25 Hz. More systems = more satellites = more
// accurate fixes; faster = smoother map and live diff. 20 Hz + GPS/Galileo
// is the usual lap-timer choice. These are the GPS mode setting's "20 Hz"
// (its default when GPS_TARGET_RATE_HZ is 20 or more); its other choice,
// 10 Hz with all four systems, is for tracks lined with trees.
#define GPS_TARGET_RATE_HZ 20
#define GPS_SET_CONSTELLATIONS 1     // 0 = leave the module's satellite systems alone
#define GPS_USE_GALILEO 1
#define GPS_USE_GLONASS 0
#define GPS_USE_BEIDOU 0
#define GPS_CONFIGURE_UBLOX 1        // send UBX config (RAM only, never saved to the module)
#define GPS_USE_UBX 1                // u-blox: read binary UBX-NAV-PVT instead of NMEA (0 = always NMEA)
#define GPS_PVT_WAIT_MS 3000         // no NAV-PVT this long after asking = not u-blox, stay on NMEA
#define GPS_ECHO_NMEA 0              // 1 = copy raw NMEA to the USB serial monitor
#define GPS_RX_BUFFER_BYTES 16384     // ~5 s of NMEA at 20 Hz
#define GPS_MIN_SATELLITES 4
#define GPS_FIX_TIMEOUT_MS 3000      // no fresh fix for this long = fix lost
// While the board sleeps or is switched off, the GPS goes into standby (UBX-RXM-PMREQ): a few tens of
// microamps, and it keeps its clock and satellites, so it has a fix again about a second after waking.
// 0 = it keeps tracking instead (about 20 mA: a 3000 mAh battery is flat in a week or two).
#define GPS_STANDBY_IN_SLEEP 1
#define GPS_ASSISTNOW_AUTONOMOUS 1   // the GPS predicts orbits 3-6 days ahead, so a start a day or two later is seconds too
#define GPS_STANDBY_AOP_WAIT_MS 3000 // before standby, wait at most this long for it to finish working them out
#define GPS_WAKE_MS 1000             // a GPS woken from standby gets this long to start talking before it's looked for

// ---------------------------------------------------------------------------
// Bluetooth: stream GPS to phone apps as a RaceBox Mini (RaceChrono etc.)
// ---------------------------------------------------------------------------
#ifndef BLE_ENABLED
#define BLE_ENABLED 1
#endif

// ---------------------------------------------------------------------------
// Settings: changed on the device (Menu -> Settings) or the Wi-Fi page, and
// kept in flash. These are what a new unit starts with. Rotation, GPS mode
// and Bluetooth take theirs from DISPLAY_ROTATION, GPS_TARGET_RATE_HZ and
// BLE_ENABLED (0 leaves Bluetooth out of the build). Pins stay in this file.
// ---------------------------------------------------------------------------
#define DEFAULT_SPEED_MPH 0          // 0 = km/h, 1 = mph (also on the Performance screens)
#define DEFAULT_BRIGHTNESS_PCT 100   // 10..100
#define DEFAULT_COMPARE_TO 2         // 0 = best of the day, 1 = best ever, 2 = this session
#define DEFAULT_BIG_NUMBER 2         // 0 = delta, 1 = predicted lap, 2 = lap time
#define DEFAULT_UTC_OFFSET_MIN 0     // minutes east of UTC, for file names
#define DEFAULT_RECORD_ROAD_DRIVES 1 // 0 = Free Roam drives aren't saved
#define DEFAULT_DELTA_COLOURS 0      // 0 = green ahead, red behind; 1 = blue ahead, orange behind
#define DEFAULT_SESSION_LENGTH 0     // header countdown: 0 = off (time since the start), 1 = 20 min, 2 = 25, 3 = 30

// ---------------------------------------------------------------------------
// Display (ILI9341V 240x320, used in landscape 320x240)
// ---------------------------------------------------------------------------
#define DISPLAY_ROTATION 1           // 1 or 3 for landscape: "right way up". The Screen setting's
                                     // "upside down" uses the other one; the touch mapping below is for this one
#define BACKLIGHT_PWM_CHANNEL 7      // LEDC channel dimming the backlight pin (TFT_BL, IO45)
#define BACKLIGHT_PWM_HZ 5000
#define BACKLIGHT_PWM_BITS 10
#define DISPLAY_INVERT 1             // IPS panel: if the background shows WHITE, set to 0
#define DISPLAY_FPS 10
#define SPLASH_ENABLED 1             // logo animation at start-up (BOOT skips it)
#define SPLASH_FADE_MS 1000          // each sweep (in, then out)
#define SPLASH_HOLD_MS 700
#define DISPLAY_PARTIAL_UPDATE 1     // 1 = send only changed rows (less tearing), 0 = send every frame whole
#define DISPLAY_TEST_PATTERN 0       // 1 = show 4 coloured stripes for 2 s at boot (red, green, blue, white)

// ---------------------------------------------------------------------------
// Touch (FT6336G, I2C)
// ---------------------------------------------------------------------------
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_INT 17
#define TOUCH_RST 18
#define TOUCH_I2C_ADDR 0x38
// Raw touch is in portrait panel coordinates. Adjust these if taps land in the
// wrong place (the serial monitor logs raw + mapped coordinates for each tap).
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 1

// BOOT button (GPIO0): short press = next, long press = select / confirm
#define BOOT_BUTTON_PIN 0
#define LONG_PRESS_MS 1000
#define HOLD_BAR_DELAY_MS 400           // the "keep holding to end" bar appears after this long, not on a tap
#define END_SESSION_HOLD_MS 2000     // hold (touch or BOOT) to end a running session
#define POWER_OFF_HOLD_MS 3000       // BOOT held this long switches off, on any screen (the 1 s hold's own action comes first)...
#define POWER_OFF_BAR_MS 1500        // ...with a bar filling from here, so an ordinary 1 s hold never shows it

// ---------------------------------------------------------------------------
// Optional add-ons on the expansion header (see README, "Big button and LED bar").
// Each add-on is switched off at boot if the GPS was found on its pin.
// ---------------------------------------------------------------------------
#define EXT_BUTTON_ENABLED 1         // momentary push button, IO21 to GND; same as BOOT
#define EXT_BUTTON_PIN 21
#define LED_BAR_ENABLED 1            // WS2812B strip delta bar
#define LED_BAR_PIN 2
#define LED_BAR_COUNT 10             // LEDs on the strip (2 to 64)
#define LED_BAR_BRIGHTNESS 64        // 0-255; a full strip at 255 can draw over 500 mA
#define LED_BAR_REVERSED 0           // 1 = flip the bar if "ahead" grows to the left
#define LED_BAR_FULL_SCALE_MS 1000   // the whole bar side lights at this much delta
#define BOARD_LED_ENABLED 1          // mirror the delta colour on the board's own WS2812
#define BOARD_LED_PIN 42
#define BOARD_LED_BRIGHTNESS 40      // 0-255
// An LSM6DSO-class IMU on the I2C header (with the touch controller): real g in the log and the
// RaceBox output once it has learned how it's mounted (Imu.h). Harmless with none fitted.
// Untested on hardware.
#define IMU_ENABLED 1
// OBD-II over CAN: an SN65HVD230 transceiver on IO3 (TX) and IO14 (RX) to the car's OBD-II socket.
// Engine speed, throttle and coolant temperature in the log, and a shift light on the LED bar
// (CanObd.h). Off by default: IO14 is also a GPS pin on the header. Untested on hardware.
#define CAN_ENABLED 0
#define CAN_TX_PIN 3
#define CAN_RX_PIN 14
#define CAN_BITRATE_KBPS 500         // 500 for most cars since 2008; some use 250
#define SHIFT_LIGHT_START_RPM 5500   // the LED bar starts filling here...
#define SHIFT_LIGHT_RPM 6800         // ...and flashes blue to shift. Set for your engine; 0 = no shift light
// OBD-II from a Bluetooth LE adapter (ELM327: Veepeak OBDCheck BLE+, OBDLink CX...): the same
// engine data as CAN, chosen in Settings > Engine data (ObdBle.h). Nothing happens until an
// adapter has been chosen. Untested on hardware: no adapter has been tried yet.
#define OBD_BLE_ENABLED 1
#define OBD_SCAN_S 10                // Find adapter scans this long
// The engine's temperatures (6.5), the defaults of Settings > Coolant: the top bar's coolant tag is
// blue below COLD, yellow from WARM and blinking red from HOT (COOLANT_WARN_C), where a full-screen
// warning shows too, as it does for oil at OIL_HOT_C (TempAlert.h)
#define COOLANT_COLD_C 70
#define COOLANT_WARM_C 100
#define COOLANT_WARN_C 110           // Hot at
#define OIL_HOT_C 130                // Oil hot at, for cars that report oil temperature (PID 0x5C)
#define TEMP_ALERT_MS 10000          // the full-screen warning shows this long (a tap ends it sooner)...
#define TEMP_ALERT_FLASH_MS 350      // ...flashing red and yellow (and the LED bar with it) this fast...
#define TEMP_REARM_DROP_C 3          // ...and again once the temperature has dropped this far below the limit and come back,
#define TEMP_REPEAT_MS 60000         // or after this long still over it
// Track Rat as a DJI Osmo Action's GPS remote (Action 4, 5 Pro, 6, Osmo 360): GPS into the video
// at 10 Hz, recording with the sessions (CameraBle.h). Paired in Settings > Camera; nothing happens
// until then. Untested with a camera.
#define CAMERA_BLE_ENABLED 1
#define CAMERA_SCAN_S 10
#define CAM_REC_BLINK_MS 700        // the camera's REC tag blinks red this slowly (on, then off)
#define CAM_ASK_SHOW_MS 3000         // after a tap on the camera's tag: "REC..." until its status follows, at most this long
#define CLOCK_RESYNC_MS 2000         // the system clock (file dates on the card) is set again from GPS if this far off
#define SYNC_FLASH_MS 3000           // the SYNC flash for lining up a video (tap the summary's top bar)
#define DJI_HOUR_OFFSET 8            // DJI's demo sends the hour + 8 (China time). 0 if the video's GPS times come out 8 h ahead

// ---------------------------------------------------------------------------
// SD card (SDIO 4-bit)
// ---------------------------------------------------------------------------
#define SD_CLK 38
#define SD_CMD 40
#define SD_D0 39
#define SD_D1 41
#define SD_D2 48
#define SD_D3 47

// ---------------------------------------------------------------------------
// Lap timing
// ---------------------------------------------------------------------------
#define TRACK_DETECTION_RADIUS_M 5000.0  // auto-select a track if its S/F is this close
#define AUTO_START_KMH 20.0              // at boot a parked car waits on the Ready screen; this speed starts the session
#define AUTO_START_HOLD_MS 2000          // ...once it has stayed that fast this long (GPS time)
#define DEFAULT_GATE_HALF_WIDTH_M 15.0   // used when a track has no "radius"
#define MIN_LAP_TIME_MS 20000            // S/F crossings sooner than this are ignored (the Min lap time setting can change it)
// Karting: a track with "kind": "kart" in tracks.json, or every track with the Kart everywhere setting
#define KART_GATE_HALF_WIDTH_M 8.0       // lines without a "radius" on a kart track
#define KART_MIN_LAP_MS 10000            // the shortest kart lap (Min lap time on Auto)
#define MAX_LAP_TIME_MS 900000           // longer laps (pit stops) aren't recorded
#define MIN_CROSSING_SPEED_KMH 5.0       // ignore jitter while parked on the line
#define MAX_FIX_GAP_MS 2000              // don't interpolate across GPS dropouts
#define MAX_LAPS_STORED 500
#define MAX_TRACE_POINTS 30000           // path points kept per lap for live diff (50 min at 10 Hz)
#define LIVE_DELTA_MAX_OFFSET_M 40.0     // further than this from the best lap's line = no live diff

// Automatic sessions (see AutoStart.h): parked this slow for this long = in the pits (summary shown)
#define PIT_PARK_KMH 5.0
#define PIT_PARK_MS 60000UL
#define SESSION_RESUME_MS (10UL * 60UL * 1000UL)  // driving off within this of stopping: same session; later: the next one
// Automatic corner sectors (see Sectors.h), made from the first clean lap at a track
#define SECTOR_BRAKE_DROP_KMH 10.0f      // a braking zone loses at least this much speed from its peak
#define SECTOR_MIN_MS 3000               // pieces shorter than this are merged
#define SECTOR_SMOOTH_MS 600             // GPS speed is smoothed over this before looking for peaks
// Tracks that learn themselves (see TrackLearn.h): away from known tracks, driving the same
// loop twice makes a track. A loop: back within LEARN_MATCH_M of a point passed at LEARN_MIN_KMH
// or more, heading the same way, at least LEARN_MIN_LOOP_MS and LEARN_MIN_LOOP_M later.
#define TRACK_LEARNING_ENABLED 1
#define LEARN_MATCH_M 10.0f
#define LEARN_HEADING_DEG 30.0f          // "the same way": directions of travel this close
#define LEARN_MIN_KMH 20.0f
#define LEARN_MIN_LOOP_MS 20000UL
#define LEARN_MIN_LOOP_M 300.0f
#define LEARN_SAME_LOOP 0.25f            // the two loops' lengths within this fraction of each other
#define LEARN_WINDOW_MS (12UL * 60UL * 1000UL)  // both loops must fit in this (fixes kept in PSRAM to time them)
#define LEARN_NEAR_TRACK_M 1500.0        // a loop this close to a known track is that track: with no line yet it gets this one
// Lap tags (see LapTags.h). Tagged laps are never the best lap or the reference, and aren't averaged.
#define PIT_STOP_KMH 5.0                 // a lap with a fix below this is a pit lap
#define SLOW_LAP_PERCENT 107             // a lap over this % of the best clean lap is slow
#define SUSPECT_GAP_MS 1000              // a gap between fixes over this makes a lap suspect...
#define SUSPECT_ACCURACY_M 5.0           // ...or a fix less accurate than this (when the GPS says)...
#define SUSPECT_DISTANCE_PERCENT 5.0     // ...or a lap this % longer or shorter than the median lap...
#define SUSPECT_DISTANCE_MIN_LAPS 3      // ...once there are this many laps to take the median from
// The timing screen (see TimingScreen.h)
#define TREND_WINDOW_MS 3000             // the trend bar shows the delta's change over this long...
#define TREND_FULL_SCALE_MS 300          // ...and is full height at this much gained or lost
// The screen follows the situation (see SituationScreen.h)
#define SLOW_SCREEN_KMH 40.0             // under this for SLOW_SCREEN_MS: the slow screen (below the slowest hairpins)
#define SLOW_SCREEN_MS 5000
#define PIT_LANE_M 10.0                  // this close to a map line marked "pit" (and nearer it than any other): in the pit lane

// Point to point: autocross, hill climbs, sprints (a "stage" track: see LapTimer.h)
#define STAGE_ARM_M 25.0                 // stopped this close to the start: the run is armed
#define STAGE_STOPPED_KMH 3.0            // slower than this is stopped
#define STAGE_STILL_KMH 1.5              // standing still: the clock starts from the last fix this slow
#define STAGE_GO_KMH 5.0                // armed, this fast is the first movement: the clock starts
#define STAGE_LIGHTS_M 1.0               // stopped further than this behind the start line: the clock starts at the line
#define STAGE_END_STOP_MS 5000           // stopped this long in a run without crossing the finish: it ends, untimed
#define STAGE_MIN_RUN_MS 5000            // finish crossings sooner than this after the start are ignored

// ---------------------------------------------------------------------------
// Performance runs (acceleration and braking)
// ---------------------------------------------------------------------------
#define PERF_ROLLOUT_DEFAULT 0           // 1 = start the clock after 1 foot of rollout (drag-strip style)

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
#define TRACK_DATABASE_FILE "/tracks.json"
#define SESSION_DIR "/sessions"
// Full-rate session logs (.vbo, SD card only): every fix is buffered in PSRAM
// and written from core 0 about once a second
#define LOG_BUFFER_BYTES (256 * 1024)    // power of two; ~2 min of 20 Hz fixes
#define LOG_FLUSH_MS 1000

// Logbook (Logbook.h): a note per session, typed on the Wi-Fi page.
// Voice notes (hold on the debrief to record 30 s from the ES8311 microphone)
// aren't built yet: the codec has to be brought up on a board first. Leave 0.
#define VOICE_NOTES_ENABLED 0

// ---------------------------------------------------------------------------
// Battery (3.7 V LiPo on the board's socket, TP4054 charger, voltage on IO9)
// The board divides the battery voltage by 2 (two 200 k resistors) before IO9.
// ---------------------------------------------------------------------------
#define BATTERY_ENABLED 1
#define BATTERY_ADC_PIN 9
#define BATTERY_DIVIDER_RATIO 2.0f   // 200 k + 200 k
#define BATTERY_CAL_OFFSET_MV 0      // trim against a multimeter on the battery (+ reads higher)
#define BATTERY_SAMPLES 16           // ADC readings averaged each time
#define BATTERY_READ_MS 1000         // how often the voltage is read
// No battery fitted: the pin floats low, or the charger holds the empty socket at its float voltage.
#define BATTERY_PRESENT_MIN_MV 2500
#define BATTERY_PRESENT_MAX_MV 4230
#define BATTERY_PRESENT_HYST_MV 30
#define BATTERY_ABSENT_SAMPLES 5     // this many bad readings in a row (1 s apart) = no battery
#define BATTERY_LOW_MV 3300          // save the session and sleep at or below this...
#define BATTERY_LOW_HOLD_MS 10000    // ...for this long
#define BATTERY_LOW_SCREEN_MS 5000   // how long the "battery low" message shows before sleeping

// ---------------------------------------------------------------------------
// Auto-sleep: parked with no touch or BOOT press. Waking is by BOOT (IO0) or a touch (INT, IO17).
// Power off (Settings > Power off, or BOOT held POWER_OFF_HOLD_MS) is the same deep sleep, straight away.
// ---------------------------------------------------------------------------
#define AUTO_SLEEP_ENABLED 1
#define AUTO_SLEEP_IDLE_MS (20UL * 60UL * 1000UL)  // parked and untouched this long: backlight off
#define AUTO_SLEEP_DEEP_AFTER_MS (60UL * 1000UL)   // backlight off this long: deep sleep
#define AUTO_SLEEP_MOVING_KMH 5.0                  // at or above this speed the car counts as moving
#define POWER_OFF_MESSAGE_MS 1500                  // POWER OFF shows this long before the screen goes dark

// ---------------------------------------------------------------------------
// Firmware update from the SD card
// ---------------------------------------------------------------------------
#define FIRMWARE_FILE "/trackrat.bin"
#define FIRMWARE_DONE_FILE "/trackrat.installed.bin"  // the file is renamed to this after a successful install
#define FIRMWARE_PROMPT_TIMEOUT_MS 30000              // no answer in this time = skip

// ---------------------------------------------------------------------------
// Debug output (USB serial)
// ---------------------------------------------------------------------------
#ifndef DEBUG_MODE
#define DEBUG_MODE 1
#endif

#if DEBUG_MODE
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
#endif

#endif  // CONFIG_H
