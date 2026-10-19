/**
 * Track Rat - Battery, sleep and firmware update screens
 *
 * Part of DisplayManager, kept in its own file so the main display code stays untouched.
 */

#include "DisplayManager.h"
#include "Config.h"
#include "DisplayStyle.h"
#include "Power.h"

void DisplayManager::setBacklight(bool on) { Power::backlight(on); }

void DisplayManager::powerDown() {
  beginFrame();
  endFrame();
  tft.writecommand(0x10);  // ILI9341 SLPIN
  setBacklight(false);
}

// Full-screen notice: a title bar in `color`, then up to two lines.
void DisplayManager::drawMessage(const char* title, const char* line1, const char* line2, uint16_t color) {
  beginFrame();
  gfx->fillRect(0, 0, W, HEADER_H, color);
  gfx->setTextDatum(MC_DATUM);
  gfx->setTextColor(TFT_BLACK);
  gfx->drawString(title, W / 2, HEADER_H / 2, 2);
  gfx->setTextDatum(TL_DATUM);
  if (line1 && line1[0]) centered(line1, 90, 4, C_TEXT);
  if (line2 && line2[0]) centered(line2, 135, 2, C_DIM);
  endFrame();
}

void DisplayManager::drawFirmwarePrompt(const char* modified, uint32_t sizeBytes, bool touchOk) {
  beginFrame();
  gfx->fillRect(0, 0, W, HEADER_H, C_HEADER);
  gfx->setTextColor(C_TEXT);
  gfx->drawString("FIRMWARE UPDATE", 6, 5, 2);

  char buf[64];
  centered("trackrat.bin is on the SD card", 42, 2, C_TEXT);
  snprintf(buf, sizeof(buf), "%u KB", (unsigned)((sizeBytes + 512) / 1024));
  centered(buf, 66, 4, C_TEXT);
  if (modified && modified[0]) {
    snprintf(buf, sizeof(buf), "File dated %s", modified);
    centered(buf, 100, 2, C_DIM);
  }
  centered("Install it, then restart?", 130, 2, C_TEXT);
  if (touchOk) {
    footerButtons("SKIP", "INSTALL", "SKIP");
    centered("Tap INSTALL, or hold BOOT 1 s.", 156, 2, C_DIM);
    centered("Anything else skips.", 176, 2, C_DIM);
  } else {
    centered("Hold BOOT 1 s to install.", 160, 2, C_DIM);
    centered("Any other press skips.", 180, 2, C_DIM);
  }
  endFrame();
}

void DisplayManager::drawFirmwareProgress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  beginFrame();
  gfx->fillRect(0, 0, W, HEADER_H, C_HEADER);
  gfx->setTextColor(C_TEXT);
  gfx->drawString("INSTALLING FIRMWARE", 6, 5, 2);

  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  centered(buf, 70, 6, C_TEXT);

  const int bx = 30, by = 140, bw = W - 60, bh = 24;
  gfx->drawRect(bx, by, bw, bh, C_TEXT);
  gfx->fillRect(bx + 2, by + 2, (bw - 4) * percent / 100, bh - 4, TFT_GREEN);
  centered("Don't switch it off", 180, 2, C_DIM);
  endFrame();
}
