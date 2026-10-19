/**
 * Track Rat - Wi-Fi page for phones
 *
 * Opens an open Wi-Fi network "TrackRat-XXXX" with a captive portal: a DNS
 * server answers every name with the board's address, so a phone that joins
 * opens the page by itself (it's also at http://192.168.4.1).
 *
 * The page (data/web, gzipped, in LittleFS) lists and downloads session
 * files, downloads and replaces tracks.json, installs firmware, and shows
 * device info. The web server runs in its own task (AsyncTCP, core 0), so
 * downloads don't hold up the GPS loop.
 *
 * Wi-Fi is only on while the Wi-Fi screen is shown, never while timing.
 */

#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <Arduino.h>
#include "GPSHandler.h"

namespace WifiPortal {

bool start();   // Wi-Fi network, DNS and web server on
void stop();    // all off
bool active();

// Call every loop: answers DNS, copies the GPS status for the page, and
// restarts the board after a firmware update.
void loop(const GPSHandler& gps);

const String& networkName();  // "TrackRat-1A2B"
String address();             // "192.168.4.1"
int clientCount();            // phones joined

// Firmware install: -1 = none, 0-100 = percent received. True while the
// board must not be switched off or the Wi-Fi turned off.
int updateProgress();
bool busy();
bool restarting();

}  // namespace WifiPortal

#endif  // WIFI_PORTAL_H
