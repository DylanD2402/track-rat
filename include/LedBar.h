/**
 * Track Rat - LED delta bar
 *
 * Drives an optional WS2812B strip (LED_BAR_PIN) and the board's own RGB LED
 * (BOARD_LED_PIN) from the live delta. The pattern is in DeltaLeds.h; this
 * file sends it out over the ESP32-S3 RMT peripheral. Writes never block, and
 * with nothing connected the output pins just toggle harmlessly.
 */

#ifndef LED_BAR_H
#define LED_BAR_H

#include <Arduino.h>

namespace LedBar {

// Call after the GPS is found: an LED on a pin the GPS is using stays off.
void begin(int gpsRxPin, int gpsTxPin);

// Call every loop(). timing: a lap is running; hasDelta/deltaMs: live gap (+ = slower).
// It sends a new frame at most every LED_UPDATE_MS and only when the picture changed.
void update(bool timing, bool hasDelta, int32_t deltaMs);

void onBestLap(bool personalBest);  // flashes purple for a PB, green (blue) for another best lap
void setBlueOrange(bool on);        // the Delta colours setting: blue / orange instead of green / red
void onSplit();
// The engine speed from OBD-II (CanObd.h), known = read within the last second: from
// SHIFT_LIGHT_START_RPM the bar is a shift light instead of the delta (Obd.h).
void setRpm(bool known, float rpm);
// The overheat warning (6.5): while it shows, the whole bar and the board LED are red when
// `on`, off between (it blinks), over the delta and the shift light
void setOverheat(bool showing, bool on);

}  // namespace LedBar

#endif  // LED_BAR_H
