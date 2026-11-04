#include "SdUpdate.h"

#include "App.h"
#include "AppRollback.h"
#include "Config.h"
#include "FirmwareUpdate.h"
#include "UiTheme.h"

namespace SdUpdate {

// ---------------------------------------------------------------------------
// Firmware update from the SD card
// ---------------------------------------------------------------------------

static void drawInstallProgress(size_t done, size_t total) {
  static int lastPct = -1;
  int pct = total ? (int)(done * 100 / total) : 0;
  if (pct == lastPct) return;
  lastPct = pct;
  display.drawFirmwareProgress(pct);
}

// If /trackrat.bin is on the card, offer to install it: tap the centre or hold
// BOOT to confirm; anything else (or no answer) skips.
void offer() {
  FirmwareUpdate::Offer offer;
  if (!FirmwareUpdate::find(offer)) return;

  if (offer.check != firmware::Check::Ok) {
    display.drawMessage("FIRMWARE FILE SKIPPED", "Can't use trackrat.bin", firmware::describe(offer.check), ui::C_RED);
    delay(4000);
    return;
  }

  char modified[24] = "";
  if (offer.modified > 0) {
    struct tm t;
    localtime_r(&offer.modified, &t);  // back to the local time FAT stores
    strftime(modified, sizeof(modified), "%Y-%m-%d %H:%M", &t);
  }
  display.drawFirmwarePrompt(modified, offer.size, input.touchAvailable());
  bool install = false;
  uint32_t start = millis();
  while (millis() - start < FIRMWARE_PROMPT_TIMEOUT_MS) {
    gps.update();  // keep draining the GPS while waiting
    InputEvent ev = input.poll();
    if (ev == InputEvent::None) {
      delay(5);
      continue;
    }
    install = ev == InputEvent::TapCenter || ev == InputEvent::BootLong;
    break;
  }
  if (!install) {
    DEBUG_PRINTLN("Firmware: update skipped");
    return;
  }

  DEBUG_PRINTLN("Firmware: installing");
  approllback::markGood();  // Update refuses to start while this app is still waiting to be trusted
  const char* error = FirmwareUpdate::install(offer, drawInstallProgress);
  if (error) {
    DEBUG_PRINTF("Firmware: install failed: %s\n", error);
    display.drawMessage("UPDATE FAILED", error, "The old firmware is unchanged", ui::C_RED);
    delay(5000);
    return;
  }
  display.drawMessage("UPDATE INSTALLED", "Restarting…", "", ui::C_GREEN);
  delay(1500);
  approllback::markGood();  // (this app, which is leaving on purpose; the new one starts pending)
  ESP.restart();
}

}  // namespace SdUpdate
