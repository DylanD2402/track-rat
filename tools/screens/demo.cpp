// The website's demo: a scripted session at Calabogie through the real screen code and the real LapTimer,
// one frame per GPS fix (20 a second). tools/make_demo.py builds it and runs it, then encodes the frames.
// The storyboard and the captions are here.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>
#include "host.h"
#include "SituationScreen.h"

namespace demo {

const double TAU = 2.0 * 3.14159265358979323846;
const int FPS = 20, LCD_W = 320, LCD_H = 240;
const size_t FRAME_BYTES = LCD_W * LCD_H * 3;
const size_t FADE_FRAMES = 8;  // the last frames fade to black, so the loop restarts cleanly

// --- the speed round the lap ----------------------------------------------------
struct Pace {
  double p, a, ph;
};
Pace paceFor(int lapNo) {
  if (lapNo <= 0) return {0.93, 0, 0};  // the out lap
  if (lapNo == 1) return {1.012, 0.020, 0.6};
  if (lapNo == 2) return {0.990, 0.020, 2.4};
  if (lapNo == 3) return {1.003, 0.020, 4.1};
  return {0.997, 0.020, 5.3};
}

// --- output: frames.rgb, poster.ppm, demo.json ----------------------------------
std::string outDir;
FILE* framesFile = nullptr;
int frames = 0;    // emitted so far
int fadeLeft = 0;  // cross-fade frames still to come after a cut
int tapLeft = 0, tapX = 0, tapY = 0;
bool posterNext = false;
std::vector<uint8_t> lastFrame;            // the last frame written
std::deque<std::vector<uint8_t>> pending;  // the last FADE_FRAMES, held back to fade them to black
struct Cue {
  double t;
  std::string text;
};
std::vector<Cue> cues;

void blendPixel(std::vector<uint8_t>& px, int x, int y, double alpha) {
  uint8_t* q = &px[3 * (y * LCD_W + x)];
  for (int k = 0; k < 3; k++) q[k] = (uint8_t)(q[k] * (1 - alpha) + 255 * alpha + 0.5);
}

// Writes the display as one frame (converted as save() in screens.cpp does), with the cross-fade and the tap ring.
void emit(DisplayManager& d) {
  const uint16_t* p = d.tft.pixels();
  std::vector<uint8_t> px(FRAME_BYTES);
  for (int i = 0; i < LCD_W * LCD_H; i++) {
    uint16_t c = p[i];
    px[3 * i] = (uint8_t)((c >> 11) * 255 / 31);
    px[3 * i + 1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
    px[3 * i + 2] = (uint8_t)((c & 31) * 255 / 31);
  }
  if (fadeLeft > 0 && !lastFrame.empty()) {
    static const double NEW_WEIGHT[4] = {0.2, 0.4, 0.6, 0.8};
    const double w = NEW_WEIGHT[4 - fadeLeft];
    for (size_t i = 0; i < px.size(); i++) px[i] = (uint8_t)(w * px[i] + (1 - w) * lastFrame[i] + 0.5);
    fadeLeft--;
  }
  if (tapLeft > 0) {
    const int age = 8 - tapLeft;
    const double R = 6 + 2 * age, alpha = 0.9 * (1 - age / 8.0);
    const int reach = (int)R + 3;
    for (int y = std::max(0, tapY - reach); y <= std::min(LCD_H - 1, tapY + reach); y++)
      for (int x = std::max(0, tapX - reach); x <= std::min(LCD_W - 1, tapX + reach); x++) {
        const double dist = std::hypot(x - tapX, y - tapY);
        if (std::fabs(dist - R) <= 1.0) blendPixel(px, x, y, alpha);
        if (age < 3 && dist <= 4.0) blendPixel(px, x, y, 0.5);
      }
    tapLeft--;
  }
  if (posterNext) {
    FILE* f = fopen((outDir + "/poster.ppm").c_str(), "wb");
    fprintf(f, "P6\n320 240\n255\n");
    fwrite(px.data(), 1, FRAME_BYTES, f);
    fclose(f);
    posterNext = false;
  }
  lastFrame = px;
  pending.push_back(px);
  if (pending.size() > FADE_FRAMES) {
    fwrite(pending.front().data(), 1, FRAME_BYTES, framesFile);
    pending.pop_front();
  }
  frames++;
}

void cut() { fadeLeft = 4; }
void tap(int x, int y) {
  tapX = x;
  tapY = y;
  tapLeft = 8;
}
void cue(const char* text) { cues.push_back({frames / (double)FPS, text}); }
void poster() { posterNext = true; }

// The last frames fade to black: multiplied by 7/8, 6/8, ... 0/8
void finish() {
  const size_t n = pending.size();
  for (size_t i = 0; i < n; i++) {
    const int f = (int)(n - 1 - i);
    for (uint8_t& b : pending[i]) b = (uint8_t)(b * f / 8);
  }
  for (auto& fr : pending) fwrite(fr.data(), 1, FRAME_BYTES, framesFile);
  pending.clear();
  fclose(framesFile);
  framesFile = nullptr;
}

}  // namespace demo

int main(int argc, char** argv) {
  using namespace demo;
  if (argc < 2) {
    fprintf(stderr, "usage: demo OUTDIR\n");
    return 1;
  }
  outDir = argv[1];
  DisplayManager d;
  d.begin();
  d.setBatteryPercent(76);
  GPSHandler gps;

  Lap1 loop;
  loop.build();
  const double L = loop.length;
  Track track;
  track.id = "calabogie";
  track.venueId = "calabogie";
  track.name = TRACK_NAME;
  track.location = TRACK_LOCATION;
  track.country = "Canada";
  track.length = TRACK_LENGTH_M;
  track.hasStartFinish = true;
  track.startFinish = {SF_LAT, SF_LON, 15};
  track.hasMap = true;
  std::vector<geo::XYf> outline;
  for (size_t i = 0; i < loop.lat.size(); i++) {
    geo::XY p = geo::toLocal(loop.lat[i], loop.lon[i], SF_LAT, SF_LON);
    outline.push_back({(float)p.x, (float)p.y});
  }
  track.map.push_back(outline);
  Track layout = track;
  layout.layoutName = "Full";

  // The car's speed (km/h) at distance s round the loop, at the given pace
  auto kmhAt = [&](double s, Pace pc) {
    const double u = fmod(fmod(s, L) + L, L);
    const double base = 118 + 52 * sin(TAU * 3 * u / L) + 18 * sin(TAU * 8 * u / L);
    return base * (pc.p + pc.a * sin(TAU * 2 * u / L + pc.ph));
  };

  // --- the stored personal best, made before any frame: a lap timer at pace 1.000 through two laps
  LapTimer old;
  old.begin(track.startFinish, {}, MIN_LAP_TIME_MS);
  double ps = -150;
  uint32_t pg = 36000000;
  mockMillis = 1000;
  std::vector<TrackPoint> gates;
  bool gatesMade = false;
  while (ps < 2 * L + 20) {
    const float k = (float)kmhAt(ps, Pace{1.0, 0, 0});
    ps += k / 3.6 * 0.05;
    pg += 50;
    mockMillis += 50;
    double la, lo;
    loop.at(ps, la, lo);
    old.onFix(la, lo, k, pg, mockMillis);
    if (!gatesMade && ps > L + 20) {  // the corner sectors, from the first lap, as in screens.cpp
      std::vector<sectors::PathPt> p;
      for (const LapTimer::TracePt& tp : old.bestPath()) p.push_back({tp.x, tp.y, tp.t, tp.speedKmh});
      for (const sectors::Cut& c : sectors::findCuts(p)) {
        TrackPoint g = {0, 0, 15, c.headingDeg, HeadingFrom::Set};
        geo::fromLocal(c.x, c.y, track.startFinish.lat, track.startFinish.lon, g.lat, g.lon);
        gates.push_back(g);
      }
      old.setSectors(gates);
      gatesMade = true;
    }
  }
  const uint32_t pbMs = old.bestLap()->timeMs;
  const std::vector<LapTimer::TracePt> pbPath = old.bestPath();
  std::vector<int32_t> everBest;
  for (size_t i = 0; i <= gates.size(); i++) everBest.push_back(old.sectorBestMs(i));

  // --- the session's lap timer ---
  LapTimer timer;
  timer.begin(track.startFinish, {}, MIN_LAP_TIME_MS, gates);
  timer.setReference(pbPath, pbMs);
  d.setPersonalBest(pbMs, 20260914);
  RunningOverlay ov;
  timing::DeltaTrend trend;
  double s = -1800;  // about a minute before the line
  uint32_t gpsMs = pg;
  const uint32_t sessionStartMillis = mockMillis;
  uint32_t pbNow = pbMs, flashUntil = 0;
  float kmh = 0;
  double la = SF_LAT, lo = SF_LON;
  std::vector<std::pair<uint16_t, uint32_t>> lapsDone;  // (number, time) of each lap completed
  std::vector<bool> lapWasPb;

  // One GPS fix: the car moves, the timer sees it, and the overlay follows as src/app/Session.cpp keeps it
  auto step = [&]() {
    const int lapNo = (int)floor(s / L) + 1;
    kmh = (float)kmhAt(s, paceFor(lapNo));
    s += kmh / 3.6 * 0.05;
    gpsMs += 50;
    mockMillis += 50;
    loop.at(s, la, lo);
    LapEvents evs = timer.onFix(la, lo, kmh, gpsMs, mockMillis);
    for (const LapEvent& ev : evs) {
      if (ev.type != LapEventType::LapCompleted) continue;
      const Lap* lap = timer.lastLap();
      ov.showLap = true;
      ov.lapNumber = lap->number;
      ov.lapMs = lap->timeMs;
      ov.lapIsBest = ev.isBest;
      ov.lapIsPb = lap->timeMs < pbNow;
      ov.lapIsDayBest = ov.lapIsPb;
      ov.lapDeltaMs = (int32_t)lap->timeMs - (int32_t)pbNow;
      lapsDone.push_back({lap->number, lap->timeMs});
      lapWasPb.push_back(ov.lapIsPb);
      if (ov.lapIsPb) pbNow = lap->timeMs;
      flashUntil = mockMillis + 4000;
      trend.clear();
    }
    if (ov.showLap && mockMillis > flashUntil) ov.showLap = false;
    trend.add(gpsMs, timer.liveDeltaMs(), timer.lapRunning() && timer.liveDeltaMs() != NO_DELTA);
    ov.trendMs = trend.change();
    ov.sectorBestEver = everBest;
    ov.sectorBestDay.clear();
    for (size_t i = 0; i <= gates.size(); i++) ov.sectorBestDay.push_back(timer.sectorBestMs(i));
    setGps(gps, true, la, lo, kmh, gpsMs);
  };
  auto skipTo = [&](double target) {
    while (s < target) step();
  };
  auto draw = [&](bool map) {
    d.setSessionClock(situation::sessionClock(mockMillis - sessionStartMillis, 0).c_str(), false);
    if (map)
      d.drawRunningMap(track, timer, gps, ov, 0);
    else
      d.drawRunning(track, timer, gps, ov, 0);
  };
  auto play = [&](int n, bool map) {
    for (int i = 0; i < n; i++) {
      step();
      draw(map);
      emit(d);
    }
  };
  auto hold = [&](int n) {
    for (int i = 0; i < n; i++) {
      mockMillis += 50;
      emit(d);
    }
  };

  framesFile = fopen((outDir + "/frames.rgb").c_str(), "wb");
  if (!framesFile) {
    fprintf(stderr, "demo: cannot write %s/frames.rgb\n", outDir.c_str());
    return 1;
  }

  // 1. Boot, 24 frames
  cue("Switch on. It finds the track by GPS.");
  d.drawBoot("Detecting GPS…");
  hold(24);

  // 2. Home, 56 frames
  cut();
  setGps(gps, true, SF_LAT + 0.0004, SF_LON, 0, 0);
  {
    nextcard::Inputs in;
    in.fix = true;
    in.today = "20261025";
    in.circuitIndex = 3;
    in.circuitDistM = 40;
    in.circuitName = "Calabogie";
    in.layoutName = "Full";
    in.pbMs = pbMs;
    d.drawHome(nextcard::choose(in), -1, gps, true);
  }
  hold(48);
  tap(160, 70);  // the Next up card
  hold(8);

  // 3. Ready, 44 frames
  cue("Timing starts by itself at the start/finish line.");
  d.drawTrackConfirm(layout, 40, true, true, 0, 3, false, true);
  hold(44);

  // 4. Out lap and the line, 110 frames
  cut();
  skipTo(-70);
  play(110, false);

  // 5. Lap 1, ahead, 110 frames
  cut();
  cue("Live delta against your best lap, 20 times a second.");
  skipTo(L * 0.42);
  play(52, false);
  poster();
  play(50, false);
  tap(160, 110);
  play(8, false);

  // 6. The map, 80 frames
  cue("Tap for the map, with your best lap as a ghost.");
  play(80, true);

  // 7. The line and the lap flash, 130 frames
  cut();
  cue("Every lap timed at the line. Purple is a new personal best.");
  skipTo(L - 75);
  play(130, false);

  // 8. Lap 2, behind, 70 frames
  cut();
  cue("Green when you're ahead, red when you're behind.");
  skipTo(L + L * 0.30);
  play(70, false);

  // 9. Pit debrief, 100 frames
  cut();
  cue("Park, and it shows where the time went.");
  skipTo(4 * L + 300);
  setGps(gps, true, la, lo, 0, gpsMs);
  {
    std::vector<debrief::LapIn> laps;
    for (size_t i = 0; i < timer.lapCount(); i++)
      laps.push_back({timer.lap(i).number, timer.lap(i).timeMs, timer.lap(i).tags, timer.lap(i).sectorMs});
    DebriefView dv;
    dv.r = debrief::analyse(laps, timer.sectorGates().size() + 1);
    dv.session = 1;
    if (dv.r.valid && timer.bestPathLapIndex() == dv.r.bestIndex) {
      const std::vector<int32_t>& g = timer.lap(dv.r.bestIndex).sectorMs;
      for (size_t k = 0; k < dv.r.top.size(); k++) {
        int sec = dv.r.top[k];
        if (timer.sectorBestMs(sec) != dv.r.best[sec].ms) continue;
        int32_t from = sec == 0 ? 0 : g[sec - 1], to = sec < (int)g.size() ? g[sec] : (int32_t)dv.r.bestMs;
        dv.hints[k] = debrief::hintText(debrief::hint(debrief::slice(timer.bestPath(), from, to),
                                                      debrief::slice(timer.sectorBestPath(sec), INT32_MIN, INT32_MAX)));
      }
    }
    if (!dv.r.valid || dv.r.top.empty())
      printf("demo: WARNING the debrief has no sectors to show (valid %d, top %zu)\n", dv.r.valid ? 1 : 0,
             dv.r.top.size());
    d.drawDebrief(track, timer, dv, 0);
  }
  hold(100);

  // 10. Wi-Fi, 60 frames
  cut();
  cue("Download sessions over Wi-Fi. No app.");
  d.drawWifi(true, true, "TrackRat-3F2A", "trackrat.local", 1, 4821, -1, false, true);
  hold(60);

  finish();

  // demo.json: the frame rate, the count, the length and the captions
  FILE* j = fopen((outDir + "/demo.json").c_str(), "w");
  fprintf(j, "{\"fps\":%d,\"frames\":%d,\"duration\":%.2f,\"cues\":[", FPS, frames, frames / (double)FPS);
  for (size_t i = 0; i < cues.size(); i++)
    fprintf(j, "%s{\"t\":%.2f,\"text\":\"%s\"}", i ? "," : "", cues[i].t, cues[i].text.c_str());
  fprintf(j, "]}\n");
  fclose(j);

  printf("demo: %d frames (%.2f s), PB %u:%06.3f", frames, frames / (double)FPS, pbMs / 60000,
         (pbMs % 60000) / 1000.0);
  for (size_t i = 0; i < lapsDone.size(); i++)
    printf(", lap %u %s%s", (unsigned)lapsDone[i].first, formatLapTime(lapsDone[i].second).c_str(),
           lapWasPb[i] ? " (PB)" : "");
  printf("; lap 1 was a PB: %s\n", !lapWasPb.empty() && lapWasPb[0] ? "yes" : "no");
  return 0;
}
