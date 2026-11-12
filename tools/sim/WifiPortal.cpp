// Stand-in for src/WifiPortal.cpp (tools/build_sim.py): the Wi-Fi screen comes up "on" with a network name,
// and nothing is served. Same header (include/WifiPortal.h).
#include "WifiPortal.h"

namespace WifiPortal {

static bool on = false;
static String name = "TrackRat-1A2B";
static WifiPortal::RetimeHandler retimeHandler;

bool start() {
  on = true;
  return true;
}
void stop() { on = false; }
bool active() { return on; }
bool networkUp() { return on; }
void loop(const GPSHandler&) {}
const String& networkName() { return name; }
String address() { return "192.168.4.1"; }
String shortAddress() { return "trackrat.local"; }
int clientCount() { return 0; }
int updateProgress() { return -1; }
bool busy() { return false; }
bool restarting() { return false; }
int uploadCodeValue() { return 4821; }
bool takeCodeChanged() { return false; }
int timeZoneOffset() { return 0; }
void onRetime(RetimeHandler handler) { retimeHandler = handler; }

}  // namespace WifiPortal
