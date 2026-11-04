#include "TrackDay.h"

#include <algorithm>
#include "App.h"
#include "BestLapStore.h"
#include "Config.h"
#include "GeoMath.h"
#include "HomeScreen.h"
#include "ProfileStore.h"
#include "SimTrack.h"
#include "TrackOrder.h"

namespace TrackDay {

// Track list: trackDB indices, nearest first (see TrackOrder.h), and where in it we are
static std::vector<int> listOrder;
static int listPos = 0;
static bool listSorted = false;  // ordered by distance (false: no GPS fix when it was made)
// The Ready screen: where it was entered from decides whether its left button
// is "Back" (to the track list) or "Menu".
static bool readyFromList = false;
// The Track day list also holds two entries that are not a track: + New circuit, and (no touch screen) Back
static const int ENTRY_NEW_CIRCUIT = -2, ENTRY_BACK = -3;

// Orders the track list by distance from here. keepIndex >= 0 stays selected;
// otherwise the nearest track is.
static void sortTrackList(int keepIndex) {
  std::vector<trackorder::Item> items;
  std::vector<int> trackOf;    // trackDB index of each item: autocross courses are left out (the Autocross hub has them)
  std::vector<String> venues;  // venue ids, so layouts of one venue share a group number
  for (int i = 0; i < trackDB.count(); i++) {
    const Track* t = trackDB.get(i);
    if (t->course) continue;
    trackOf.push_back(i);
    int group = -1;
    if (t->layoutIndex >= 0) {
      auto it = std::find(venues.begin(), venues.end(), t->venueId);
      group = it - venues.begin();
      if (it == venues.end()) venues.push_back(t->venueId);
    }
    items.push_back({distanceToTrack(i), group, t->bench});
  }
  bool hasBench = false;
  for (const trackorder::Item& it : items) hasBench |= it.last;
  listOrder = trackorder::byDistance(items);
  for (int& i : listOrder) i = trackOf[i];
  listOrder = trackorder::withEntries(listOrder, hasBench, ENTRY_NEW_CIRCUIT, !input.touchAvailable(), ENTRY_BACK);
  listSorted = gps.hasFix();
  listPos = 0;
  for (size_t p = 0; keepIndex >= 0 && p < listOrder.size(); p++)
    if (listOrder[p] == keepIndex) listPos = p;
  if (!listOrder.empty() && listOrder[listPos] >= 0) selectedIndex = listOrder[listPos];
}

// How many entries the Track day list has: the circuits (and the Bench Test), + New circuit, and Back without touch
static int trackListSize() {
  int n = 1 + (input.touchAvailable() ? 0 : 1);
  for (int i = 0; i < trackDB.count(); i++) n += !trackDB.get(i)->course;
  return n;
}

static void moveInTrackList(int step) {
  const int n = listOrder.size();
  if (n == 0) return;
  listPos = (listPos + step + n) % n;
  if (listOrder[listPos] >= 0) selectedIndex = listOrder[listPos];
}

// PBs for the track list and Ready screen, read from the card once per track
struct PbInfo {
  bool read = false;
  uint32_t ms = 0, date = 0;
};
static std::vector<PbInfo> pbCache;

void personalBestFor(int index, uint32_t& ms, uint32_t& date) {
  if ((int)pbCache.size() != trackDB.count()) pbCache.assign(trackDB.count(), PbInfo());
  PbInfo& c = pbCache[index];
  if (!c.read) {
    c.read = true;
    const Track* t = trackDB.get(index);
    bestlap::Record r;
    if (t->hasStartFinish && BestLapStore::load(t->id, BestLapStore::Kind::Ever, r, false) &&
        bestlap::sameLine(r, t->startFinish.lat, t->startFinish.lon)) {
      c.ms = r.lapMs;
      c.date = r.date;
    }
  }
  ms = c.ms;
  date = c.date;
}

// The selected track's PB, for the track list and Ready screen
static void showPersonalBest() {
  static uint32_t peopleSeen = 0xFFFFFFFF;
  if (Profiles::changeCount() != peopleSeen) {  // another driver or car: their PBs are in other files
    peopleSeen = Profiles::changeCount();
    pbCache.clear();
  }
  char who[48] = "";
  if (Profiles::showLabel()) Profiles::label(who, sizeof(who));
  display.setDriverCar(who);
  uint32_t ms, date;
  personalBestFor(selectedIndex, ms, date);
  display.setPersonalBest(ms, date);
}

void forgetPb(int index) {
  if (index < (int)pbCache.size()) pbCache[index].read = false;
}

void forgetPbs() { pbCache.clear(); }

// Track day, then + New circuit: created here, start/finish marked on the first pass.
void createNewTrack() {
  if (!gps.hasFix()) return enterState(AppState::Menu);
  const GpsFix& f = gps.lastFix();
  int idx = trackDB.createTrack(f.lat, f.lon);
  if (idx < 0) return enterState(AppState::Menu);
  selectedIndex = idx;
  enterState(AppState::Running);
  sessionKeepLayout();  // the driver asked for a new track: don't swap it for one nearby
}

// The world tracks from the SD card's /world tiles (WorldTiles.h) for where we are: read at the first fix and
// again after moving into another tile, only on the screens before a session (reading a tile takes a moment,
// and the lap timer runs on this loop). With no card or no /world folder it does nothing.
void refreshWorldTracks() {
  if (!gps.hasFix()) return;
  if (state != AppState::GpsWait && state != AppState::Menu && state != AppState::TrackSelect) return;
  const String keepId = trackDB.get(selectedIndex) ? selectedTrack().id : String("");
  const GpsFix& f = gps.lastFix();
  if (!trackDB.refreshWorld(f.lat, f.lon)) return;
  // World tracks' indices changed: what is kept by index is worked out again
  pbCache.clear();
  listOrder.clear();  // (the Track day list is made again on entering it)
  HomeScreen::forgetNextUp();
  const int again = keepId.length() ? trackDB.indexOfId(keepId) : -1;
  selectedIndex = again >= 0 ? again : 0;
  if (state == AppState::TrackSelect) sortTrackList(selectedIndex);
}

// Bench test: picking this track runs a simulated car around this circle
// (see SimTrack.h and GPSHandler::simulateStep) instead of using the GPS.
void addBenchTrack() {
  Track sim;
  sim.id = "bench";
  sim.name = "Bench Test";
  sim.location = "Simulated car - no GPS needed";
  sim.bench = true;
  sim.length = 2 * M_PI * SIM_RADIUS_M;
  sim.startFinish = {SIM_SF_LAT, SIM_SF_LON, (float)DEFAULT_GATE_HALF_WIDTH_M, 0, HeadingFrom::Set};  // north
  sim.hasStartFinish = true;
  sim.centerLat = SIM_SF_LAT;
  sim.centerLon = SIM_SF_LON;
  sim.splits.push_back({SIM_SPLIT_LAT, SIM_SPLIT_LON, (float)DEFAULT_GATE_HALF_WIDTH_M, 180, HeadingFrom::Set});
  std::vector<geo::XYf> outline;  // the circle, for the map screen
  for (int i = 0; i <= 72; i++) {
    float a = i * 2 * M_PI / 72;
    outline.push_back({(float)(SIM_RADIUS_M * (cos(a) - 1.0)), (float)(SIM_RADIUS_M * sin(a))});
  }
  sim.map.push_back(outline);
  sim.hasMap = true;
  trackDB.add(sim);
}

void stateChanging(AppState next) {
  // Opening the track list from the menu: nearest first. Coming back to it
  // (from a track's Ready screen or GPS status): stay on the same track.
  if (next == AppState::TrackSelect) {
    if (state == AppState::Menu || (int)listOrder.size() != trackListSize()) sortTrackList(state == AppState::Menu ? -1 : selectedIndex);
    else for (size_t p = 0; p < listOrder.size(); p++) if (listOrder[p] == selectedIndex) listPos = p;
  }
}

void onInputList(InputEvent ev) {
  if (ev == InputEvent::TapHeader) {  // the top bar: back to the home screen (GPS status is on its top bar)
    enterState(AppState::Menu);
    return;
  }
  if (ev == InputEvent::Hold) enterState(AppState::Menu);
  else if (ev == InputEvent::TapLeft) moveInTrackList(-1);
  else if (ev == InputEvent::TapRight || ev == InputEvent::BootShort) moveInTrackList(1);
  else if (ev == InputEvent::TapCenter || ev == InputEvent::BootLong) {
    const int entry = listOrder.empty() ? 0 : listOrder[listPos];
    if (entry == ENTRY_BACK) {
      enterState(AppState::Menu);
    } else if (entry == ENTRY_NEW_CIRCUIT) {
      if (gps.hasFix()) createNewTrack();  // then tap the screen as you cross the line
    } else {
      enterState(AppState::TrackConfirm);
    }
  }
  lastRender = 0;
}

void renderList() {
  // The list opened before the GPS had a fix: sort it once a fix arrives
  // (to the nearest track, unless the driver has already moved along it).
  if (!listSorted && gps.hasFix()) sortTrackList(listPos == 0 ? -1 : selectedIndex);
  {
    const int entry = listOrder.empty() ? 0 : listOrder[listPos];
    const int shown = (int)listOrder.size() - (input.touchAvailable() ? 0 : 1);  // (Back isn't counted)
    if (entry == ENTRY_NEW_CIRCUIT || entry == ENTRY_BACK) {
      display.drawTrackDayExtra(entry == ENTRY_BACK, listPos, shown, gps, input.touchAvailable());
      return;
    }
    showPersonalBest();
    display.drawTrackSelect(selectedTrack(), listPos, shown, distanceToTrack(selectedIndex), gps,
                            input.touchAvailable());
  }
}

void onInputReady(InputEvent ev) {
  if (ev == InputEvent::TapCenter || ev == InputEvent::BootLong) {
    enterState(AppState::Running);
  } else if (ev == InputEvent::TapRight && !selectedTrack().bench) {
    enterState(AppState::Running);
    sessionMarkNewLine();  // mark a new start/finish (replaces the old one)...
                           // ...on the layout the driver picked, not one the check finds
  } else if (ev == InputEvent::TapLeft || ev == InputEvent::BootShort) {
    enterState(readyFromList ? AppState::TrackSelect : AppState::Menu);
  } else if (ev == InputEvent::TapHeader) {  // next layout of this venue (still waiting for the car to drive off)
    selectedIndex = trackDB.nextLayout(selectedIndex);
    lastRender = 0;
  }
}

void renderReady() {
  showPersonalBest();
  display.drawTrackConfirm(selectedTrack(), distanceToTrack(selectedIndex), gps.hasFix(),
                           input.touchAvailable(), trackDB.layoutNumber(selectedIndex),
                           trackDB.layoutCount(selectedIndex), readyFromList, autoStartArmed());
}

}  // namespace TrackDay
