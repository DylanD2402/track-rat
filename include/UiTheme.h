/**
 * Track Rat - Screen theme
 *
 * Colours and layout shared by every screen (DisplayManager, DisplayPerf,
 * DisplayPower). A dark theme in the style of a modern car dash: true black,
 * raised grey surfaces, one bright colour per meaning. The colours are
 * Apple's dark-mode system colours, which are tuned to read well on black.
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>

namespace ui {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Surfaces
constexpr uint16_t C_BG = 0x0000;                 // true black
constexpr uint16_t C_SURFACE = rgb(28, 28, 30);   // cards and buttons
constexpr uint16_t C_RAISED = rgb(44, 44, 46);    // highlighted button, lap flash
constexpr uint16_t C_LINE = rgb(56, 56, 58);      // hairlines and outlines

// Text
constexpr uint16_t C_TEXT = 0xFFFF;
constexpr uint16_t C_TEXT2 = rgb(152, 152, 159);  // labels
constexpr uint16_t C_TEXT3 = rgb(99, 99, 102);    // hints

// Meanings. Text on C_GREEN, C_RED, C_YELLOW and C_ORANGE is black: it holds up in sun.
constexpr uint16_t C_GREEN = rgb(48, 209, 88);    // ahead, best, fix, ready
constexpr uint16_t C_RED = rgb(255, 69, 58);      // behind, no fix, error
constexpr uint16_t C_YELLOW = rgb(255, 214, 10);  // waiting, warning, bench test
constexpr uint16_t C_ORANGE = rgb(255, 159, 10);  // go, ending a session
constexpr uint16_t C_BLUE = rgb(10, 132, 255);    // Wi-Fi and Bluetooth
constexpr uint16_t C_PURPLE = rgb(191, 90, 242);  // reserved for personal bests
constexpr uint16_t C_TRACK = rgb(200, 200, 204);  // the track line on maps

// Layout (landscape 320x240)
constexpr int W = 320;
constexpr int H = 240;
constexpr int HEADER_H = 28;  // InputManager counts taps above y 30 as the top bar
constexpr int FOOTER_Y = 200;
constexpr int FOOTER_H = 36;
constexpr int CORNER = 10;

}  // namespace ui

#endif  // UI_THEME_H
