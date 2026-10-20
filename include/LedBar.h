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

void onBestLap(bool personalBest);  // flashes purple for a PB, green for another best lap
void onSplit();

}  // namespace LedBar

#endif  // LED_BAR_H
