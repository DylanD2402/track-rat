/**
 * Track Rat - Battery, sleep and firmware update screens
 *
 * Part of DisplayManager, kept in its own file so the main display code stays untouched.
 */

#include "DisplayManager.h"
#include "Config.h"
#include "Power.h"
#include "UiTheme.h"

using namespace ui;
using Font = DisplayManager::Font;

void DisplayManager::setBacklight(bool on) { Power::backlight(on); }

void DisplayManager::powerDown() {
  beginFrame();
  endFrame();
  tft.writecommand(0x10);  // ILI9341 SLPIN
  setBacklight(false);
}

void DisplayManager::titlePill(const char* title, uint16_t color) {
  int w = textWidth(title, Font::Button) + 36;
  gfx->fillRoundRect((W - w) / 2, 44, w, 32, 16, color);
  text(title, W / 2, 61, Font::Button, C_BG, MC_DATUM);
}

// Full-screen notice: a coloured title pill, then up to two lines.
void DisplayManager::drawMessage(const char* title, const char* line1, const char* line2, uint16_t color) {
  beginFrame();
  titlePill(title, color);
  if (line1 && line1[0]) centered(line1, 104, Font::Title, C_TEXT);
  if (line2 && line2[0]) centered(line2, 140, Font::Body, C_TEXT2);
  endFrame();
}

void DisplayManager::drawPowerHold(uint32_t heldMs) {
  beginFrame();
  titlePill("POWER OFF", C_ORANGE);
  centered("Keep holding BOOT", 104, Font::Title, C_TEXT);
  const int bx = 32, by = 146, bw = W - 64, bh = 12;
  const uint32_t span = POWER_OFF_HOLD_MS - POWER_OFF_BAR_MS;
  const uint32_t done = heldMs > POWER_OFF_BAR_MS ? min(heldMs - POWER_OFF_BAR_MS, span) : 0;
  gfx->fillRoundRect(bx, by, bw, bh, bh / 2, C_SURFACE);
  const int fill = (int)(bw * done / span);
  if (fill > 0) gfx->fillRoundRect(bx, by, max(bh, fill), bh, bh / 2, C_ORANGE);
  centered("Let go to cancel", 176, Font::Body, C_TEXT2);
  endFrame();
}

void DisplayManager::drawFirmwarePrompt(const char* modified, uint32_t sizeBytes, bool touchOk) {
  beginFrame();
  header("Firmware update", nullptr);

  char buf[64];
  centered("trackrat.bin is on the SD card", HEADER_H + 14, Font::Body, C_TEXT2);
  snprintf(buf, sizeof(buf), "%u KB", (unsigned)((sizeBytes + 512) / 1024));
  centered(buf, HEADER_H + 38, Font::Title, C_TEXT);
  if (modified && modified[0]) {
    snprintf(buf, sizeof(buf), "File dated %s", modified);
    centered(buf, HEADER_H + 68, Font::Caption, C_TEXT2);
  }
  centered("Install it, then restart?", 124, Font::Body, C_TEXT);
  if (touchOk) {
    footerButtons("Skip", "Install", "Skip");
    centered("Or hold BOOT for 1 s, then let go.", 160, Font::Caption, C_TEXT3);
  } else {
    centered("Hold BOOT 1 s, then let go, to install.", 156, Font::Body, C_TEXT2);
    centered("Any other press skips.", 178, Font::Body, C_TEXT2);
  }
  endFrame();
}

void DisplayManager::drawFirmwareProgress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  beginFrame();
  header("Installing firmware", nullptr);

  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  centered(buf, 66, Font::NumL, C_TEXT);

  const int bx = 32, by = 136, bw = W - 64, bh = 12;
  gfx->fillRoundRect(bx, by, bw, bh, bh / 2, C_SURFACE);
  if (percent > 0) gfx->fillRoundRect(bx, by, max(bh, bw * percent / 100), bh, bh / 2, C_BRAND);
  centered("Don't switch it off", 168, Font::Body, C_TEXT2);
  endFrame();
}
