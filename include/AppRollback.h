/**
 * Track Rat - rolling back a bad firmware
 *
 * The Arduino core's bootloader supports rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE): a firmware just
 * installed (by the Wi-Fi page or from the SD card) starts "pending verify", and if it restarts before it says
 * it is good, the bootloader goes back to the app that was running before. The core says so at once, in
 * initArduino(), unless verifyRollbackLater() (main.cpp) returns true. So the app says it itself:
 *  - once it has run for a while (main loop), and
 *  - just before a restart or sleep it chose to make, which must not count as a crash.
 * Pulling the power in the first half minute counts as a crash too, and goes back.
 * Does nothing when the running app isn't pending verify (an ordinary boot, or one from USB).
 */

#ifndef APP_ROLLBACK_H
#define APP_ROLLBACK_H

#include "esp_ota_ops.h"

namespace approllback {

static const uint32_t HEALTHY_AFTER_MS = 30000;  // how long a new firmware must run before it is trusted

inline void markGood() { esp_ota_mark_app_valid_cancel_rollback(); }

}  // namespace approllback

#endif  // APP_ROLLBACK_H
