/**
 * Track Rat - screen size and colours, shared by the files that make up
 * DisplayManager (DisplayManager.cpp, DisplayPerf.cpp, DisplayPower.cpp, Splash.cpp)
 */

#ifndef DISPLAY_STYLE_H
#define DISPLAY_STYLE_H

#include <TFT_eSPI.h>

static const int W = 320;  // landscape
static const int H = 240;
static const int HEADER_H = 26;
static const int FOOTER_Y = 202;

static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_DIM = TFT_DARKGREY;
static const uint16_t C_HEADER = 0x10A2;  // dark slate
static const uint16_t C_GOOD = TFT_GREEN;
static const uint16_t C_BAD = TFT_RED;
static const uint16_t C_WARN = TFT_YELLOW;
static const uint16_t C_BUTTON = 0x2124;

#endif  // DISPLAY_STYLE_H
