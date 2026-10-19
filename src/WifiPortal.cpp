/**
 * Track Rat - Wi-Fi page implementation
 *
 * Request handlers run in the AsyncTCP task (core 0, next to the Wi-Fi
 * driver), not in loop(). They only touch storage and the GPS snapshot that
 * loop() copies in, never the lap timer or track list. The rules they apply
 * (allowed names, time zones, upload ownership) are in PortalLogic.h, which
 * is tested on a PC.
 */

#include "WifiPortal.h"
#include "Config.h"
#include "PortalLogic.h"
#include "Storage.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Update.h>
#include <WiFi.h>

#ifndef TRACKRAT_VERSION
#define TRACKRAT_VERSION "dev"  // set from git by tools/build_web.py
#endif

namespace WifiPortal {

static const char* PAGE_FILE = "/web/index.html.gz";  // made by tools/build_web.py
static const char* TRACKS_UPLOAD_TMP = "/tracks.json.upload";
static const char* TRACKS_BACKUP = "/tracks.json.bak";

static bool on = false;
static String ssid;
static DNSServer dns;
static AsyncWebServer* server = nullptr;
static volatile uint32_t rebootAt = 0;

// GPS status for the Device page, copied from loop() so handlers never read
// the GPS handler while it's being updated.
struct GpsSnapshot {
  bool data, fix, simulated;
  uint8_t sats;
  float hdop, rateHz, speedKmh;
  double lat, lon;
  int rxPin;
  uint32_t baud;
};
static GpsSnapshot gpsSnap = {};
static portMUX_TYPE snapLock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastSnap = 0;

// Firmware and tracks.json uploads need the code shown on the screen
static portal::UploadCode uploadCode;

// Firmware upload: one at a time (fwSlot holds the request sending it)
static portal::UploadSlot fwSlot;
static volatile int fwProgress = -1;
static bool fwOk = false;
static String fwError;

// tracks.json upload: one at a time
static portal::UploadSlot tracksSlot;
static File tracksFile;
static fs::FS* tracksFs = nullptr;
static bool tracksOk = false;
static String tracksError;

// ---------------------------------------------------------------------------
// Time zone from the phone. The page sends it every time it loads, and
// session files are named in this local time (see GPSHandler::timestampString).
// The Settings module (separate branch) should take this over: replace the
// body with a call into it and keep the /api/time handler as it is.

static const char* TZ_PREFS_NAMESPACE = "trackrat";
static const char* TZ_PREFS_KEY = "tz_min";

// minutesEastOfUtc: e.g. -240 for EDT, 60 for CET
static volatile int tzCache = 0;
static volatile bool tzLoaded = false;

static void saveTimeZoneOffset(int minutesEastOfUtc) {
  tzCache = minutesEastOfUtc;
  tzLoaded = true;
  Preferences prefs;
  if (!prefs.begin(TZ_PREFS_NAMESPACE, false)) return;
  if (!prefs.isKey(TZ_PREFS_KEY) || prefs.getShort(TZ_PREFS_KEY, 0) != minutesEastOfUtc) {
    prefs.putShort(TZ_PREFS_KEY, (int16_t)minutesEastOfUtc);
    DEBUG_PRINTF("Wi-Fi: time zone set to UTC%+d min\n", minutesEastOfUtc);
  }
  prefs.end();
}

static int savedTimeZoneOffset() {
  if (tzLoaded) return tzCache;
  Preferences prefs;
  int tz = 0;
  if (prefs.begin(TZ_PREFS_NAMESPACE, true)) {
    tz = prefs.getShort(TZ_PREFS_KEY, 0);
    prefs.end();
  }
  tzCache = tz;
  tzLoaded = true;
  return tz;
}

int timeZoneOffset() { return savedTimeZoneOffset(); }

// ---------------------------------------------------------------------------

static fs::FS* fsByName(const String& name) {
  if (name == "sd" && Storage::sdMounted()) return &SD_MMC;
  if (name == "flash" && Storage::flashMounted()) return &LittleFS;
  return nullptr;
}

// tracks.json is read from the SD card first, then flash (as TrackDatabase does)
static fs::FS* tracksReadFs() {
  if (Storage::sdMounted() && SD_MMC.exists(TRACK_DATABASE_FILE)) return &SD_MMC;
  if (Storage::flashMounted() && LittleFS.exists(TRACK_DATABASE_FILE)) return &LittleFS;
  return nullptr;
}

// Free space, for the Device tab. Working out what's used can mean reading
// the card's whole FAT, so it's kept for 30 s rather than redone every poll.
struct StorageSpace {
  uint32_t sdTotalKB, sdUsedKB, flashTotalKB, flashUsedKB;
};
static StorageSpace space = {};
static uint32_t spaceAt = 0;
static bool spaceValid = false;

static const StorageSpace& storageSpace() {
  if (!spaceValid || millis() - spaceAt > 30000) {
    space = {};
    if (Storage::sdMounted()) {
      space.sdTotalKB = SD_MMC.totalBytes() / 1024;
      space.sdUsedKB = SD_MMC.usedBytes() / 1024;
    }
    if (Storage::flashMounted()) {
      space.flashTotalKB = LittleFS.totalBytes() / 1024;
      space.flashUsedKB = LittleFS.usedBytes() / 1024;
    }
    spaceAt = millis();
    spaceValid = true;
  }
  return space;
}

// Every file in /sessions, on the SD card and in flash.
static void listSessions(fs::FS& fs, const char* fsName, String& out, bool& first) {
  File dir = fs.open(SESSION_DIR);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    if (!first) out += ',';
    first = false;
    out += "{\"name\":";
    portal::appendJsonString(out, f.name());
    out += ",\"size\":";
    out += (uint32_t)f.size();
    out += ",\"fs\":\"";
    out += fsName;
    out += "\"}";
  }
}

// Phones check for internet by fetching a known page from a known host.
// Answering with a redirect to us makes them show the page by themselves.
static bool redirectToPortal(AsyncWebServerRequest* req) {
  String ip = WiFi.softAPIP().toString();
  if (portal::isPortalHost(req->host().c_str(), ip.c_str())) return false;
  req->redirect("http://" + ip + "/");
  return true;
}

static const char FALLBACK_PAGE[] PROGMEM = R"(<!doctype html><meta name=viewport content="width=device-width">
<title>Track Rat</title><body style="font-family:sans-serif;margin:16px">
<h2>Track Rat</h2>
<p>The Wi-Fi page isn't in this board's flash yet. Copy it with
<code>pio run -e es3c28p -t uploadfs</code>.</p>
<p><a href="/api/sessions">Session list</a> &middot; <a href="/tracks.json">tracks.json</a></p>
<form method=post action=/api/firmware enctype=multipart/form-data>
<p>Install firmware (.bin): <input type=file name=firmware accept=.bin> <button>Install</button></p></form>)";

static void sendInfo(AsyncWebServerRequest* req) {
  GpsSnapshot g;
  portENTER_CRITICAL(&snapLock);
  g = gpsSnap;
  portEXIT_CRITICAL(&snapLock);

  DynamicJsonDocument doc(1536);
  doc["version"] = TRACKRAT_VERSION;
  doc["built"] = __DATE__ " " __TIME__;
  doc["network"] = ssid;
  doc["uptimeS"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["freePsram"] = ESP.getFreePsram();
  doc["tzMin"] = savedTimeZoneOffset();
  doc["sessionsOn"] = Storage::sessionFSName();

  const StorageSpace& sp = storageSpace();
  JsonObject sd = doc.createNestedObject("sd");
  sd["mounted"] = Storage::sdMounted();
  if (Storage::sdMounted()) {
    sd["totalKB"] = sp.sdTotalKB;
    sd["usedKB"] = sp.sdUsedKB;
  }
  JsonObject fl = doc.createNestedObject("flash");
  fl["mounted"] = Storage::flashMounted();
  if (Storage::flashMounted()) {
    fl["totalKB"] = sp.flashTotalKB;
    fl["usedKB"] = sp.flashUsedKB;
  }

  JsonObject gps = doc.createNestedObject("gps");
  gps["receiving"] = g.data;
  gps["fix"] = g.fix;
  gps["simulated"] = g.simulated;
  gps["satellites"] = g.sats;
  gps["hdop"] = g.hdop;
  gps["rateHz"] = g.rateHz;
  gps["speedKmh"] = g.speedKmh;
  gps["rxPin"] = g.rxPin;
  gps["baud"] = g.baud;
  if (g.fix) {
    gps["lat"] = serialized(String(g.lat, 6));
    gps["lon"] = serialized(String(g.lon, 6));
  }

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// tracks.json upload: written to a temp file, then swapped in, old one kept as .bak

// Why an upload that didn't get to start was refused
static void refuseUpload(AsyncWebServerRequest* req, const portal::UploadSlot& slot) {
  if (uploadCode.locked())
    req->send(403, "text/plain", "Too many wrong codes. Turn Wi-Fi off and on again on Track Rat for a new code.");
  else if (!uploadCode.matches(req->arg("code").c_str()))
    req->send(403, "text/plain", "Wrong code. Use the 4-digit code on Track Rat's Wi-Fi screen.");
  else if (slot.busy())
    req->send(409, "text/plain", "Another upload is running. Try again when it's done.");
  else
    req->send(400, "text/plain", "No file received");
}

static void dropTracksUpload() {
  if (tracksFile) tracksFile.close();
  if (tracksFs) tracksFs->remove(TRACKS_UPLOAD_TMP);
}

static void onTracksUpload(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
                           size_t len, bool final) {
  if (index == 0) {
    if (!uploadCode.check(req->arg("code").c_str())) return;  // refused in the reply
    if (!tracksSlot.claim(req)) return;                          // another upload is running
    tracksError = "";
    tracksFs = Storage::sessionFS();  // the SD card, else flash: where TrackDatabase saves
    tracksOk = tracksFs != nullptr;
    if (!tracksOk) tracksError = "No SD card or flash to save to";
    // The page checks the JSON before sending it; here just make sure it's an object.
    if (tracksOk && !portal::looksLikeJsonObject(data, len)) {
      tracksOk = false;
      tracksError = "That isn't a tracks.json file";
    }
    if (tracksOk) {
      tracksFile = tracksFs->open(TRACKS_UPLOAD_TMP, "w");
      if (!tracksFile) {
        tracksOk = false;
        tracksError = "Can't write to storage";
      }
    }
    req->onDisconnect([req]() {  // phone went away half way: keep the old file
      if (!tracksSlot.owns(req)) return;
      dropTracksUpload();
      tracksSlot.release(req);
    });
  }
  if (!tracksSlot.owns(req)) return;
  if (tracksOk && len && tracksFile.write(data, len) != len) {
    tracksOk = false;
    tracksError = "Storage full or write failed";
  }
  if (!final) return;

  fs::FS* fs = tracksFs;
  if (tracksFile) tracksFile.close();
  if (tracksOk) {
    fs->remove(TRACKS_BACKUP);
    if (fs->exists(TRACK_DATABASE_FILE)) fs->rename(TRACK_DATABASE_FILE, TRACKS_BACKUP);
    if (!fs->rename(TRACKS_UPLOAD_TMP, TRACK_DATABASE_FILE)) {
      fs->rename(TRACKS_BACKUP, TRACK_DATABASE_FILE);
      tracksOk = false;
      tracksError = "Couldn't replace tracks.json";
    }
  }
  if (!tracksOk) dropTracksUpload();
  DEBUG_PRINTF("Wi-Fi: tracks.json upload %s (%u bytes)%s\n", tracksOk ? "saved" : "FAILED",
               (unsigned)(index + len), tracksOk ? "" : (" - " + tracksError).c_str());
}

static void onTracksDone(AsyncWebServerRequest* req) {
  if (!tracksSlot.owns(req)) {
    refuseUpload(req, tracksSlot);
    return;
  }
  spaceValid = false;
  if (tracksOk) {
    // Restart so the track list in memory, and where it saves to, match the new file
    req->send(200, "text/plain", "Saved. Track Rat is restarting to load the new list.");
    rebootAt = millis() + 1500;
  } else {
    req->send(400, "text/plain", tracksError);
  }
  tracksSlot.release(req);
}

// ---------------------------------------------------------------------------
// Firmware upload: written straight into the other app slot by Update

static void onFirmwareUpload(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
                             size_t len, bool final) {
  if (index == 0) {
    if (!uploadCode.check(req->arg("code").c_str())) return;  // refused in the reply
    if (!fwSlot.claim(req)) return;                              // another upload is running
    fwError = "";
    fwProgress = 0;
    fwOk = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    if (!fwOk) fwError = Update.errorString();
    DEBUG_PRINTF("Wi-Fi: firmware upload started (%s)\n", filename.c_str());
    req->onDisconnect([req]() {  // phone went away half way: nothing installed
      if (!fwSlot.owns(req)) return;
      if (Update.isRunning()) Update.abort();
      if (fwProgress < 100) fwProgress = -1;
      fwSlot.release(req);
      DEBUG_PRINTLN("Wi-Fi: firmware upload cut off - not installed");
    });
  }
  if (!fwSlot.owns(req)) return;
  if (fwOk && len && Update.write(data, len) != len) {
    fwOk = false;
    fwError = Update.errorString();
    Update.abort();
  }
  if (fwOk) fwProgress = portal::uploadPercent(index + len, req->contentLength());
  if (!final) return;

  if (fwOk && !Update.end(true)) {
    fwOk = false;
    fwError = Update.errorString();
  }
  fwProgress = fwOk ? 100 : -1;
  DEBUG_PRINTF("Wi-Fi: firmware %s (%u bytes)%s\n", fwOk ? "installed" : "NOT installed", (unsigned)(index + len),
               fwOk ? "" : (" - " + fwError).c_str());
}

static void onFirmwareDone(AsyncWebServerRequest* req) {
  if (!fwSlot.owns(req)) {
    refuseUpload(req, fwSlot);
    return;
  }
  if (fwOk && fwProgress == 100) {
    req->send(200, "text/plain", "Installed. Track Rat is restarting.");
    rebootAt = millis() + 1500;  // after the reply has gone
  } else {
    if (Update.isRunning()) Update.abort();  // the file ended without a last piece
    fwProgress = -1;
    req->send(400, "text/plain", "Not installed: " + (fwError.length() ? fwError : String("incomplete file")));
  }
  fwSlot.release(req);
}

// ---------------------------------------------------------------------------

static void setupRoutes() {
  AsyncWebServer& s = *server;

  s.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (redirectToPortal(req)) return;
    if (!LittleFS.exists(PAGE_FILE)) {
      req->send(200, "text/html", FALLBACK_PAGE);
      return;
    }
    AsyncWebServerResponse* res = req->beginResponse(LittleFS, PAGE_FILE, "text/html");
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-cache");  // a firmware update may bring a new page
    req->send(res);
  });

  s.on("/api/info", HTTP_GET, sendInfo);

  s.on("/api/sessions", HTTP_GET, [](AsyncWebServerRequest* req) {
    String out;
    out.reserve(4096);
    out += "{\"files\":[";
    bool first = true;
    if (Storage::sdMounted()) listSessions(SD_MMC, "sd", out, first);
    if (Storage::flashMounted()) listSessions(LittleFS, "flash", out, first);
    out += "],\"tzMin\":";  // the time zone file names are written in
    out += savedTimeZoneOffset();
    out += "}";
    req->send(200, "application/json", out);
  });

  // /dl?fs=sd&f=<file in /sessions>. Sent a piece at a time as the phone
  // takes it, from the AsyncTCP task.
  s.on("/dl", HTTP_GET, [](AsyncWebServerRequest* req) {
    const String& name = req->arg("f");
    fs::FS* fs = fsByName(req->arg("fs"));
    if (!fs || !portal::isSafeFileName(name.c_str())) {
      req->send(400, "text/plain", "Bad file name");
      return;
    }
    String path = String(SESSION_DIR) + "/" + name;
    if (!fs->exists(path)) {
      req->send(404, "text/plain", "No such file");
      return;
    }
    req->send(req->beginResponse(*fs, path, portal::contentTypeFor(name.c_str()), true));
  });

  s.on("/tracks.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    fs::FS* fs = tracksReadFs();
    if (!fs) {
      req->send(404, "text/plain", "No tracks.json on the SD card or in flash (the built-in list is in use)");
      return;
    }
    AsyncWebServerResponse* res = req->beginResponse(*fs, TRACK_DATABASE_FILE, "application/json", true);
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
  });

  s.on("/api/tracks", HTTP_POST, onTracksDone, onTracksUpload);
  s.on("/api/firmware", HTTP_POST, onFirmwareDone, onFirmwareUpload);

  // The page sends the phone's time zone: tz = minutes east of UTC
  s.on("/api/time", HTTP_POST, [](AsyncWebServerRequest* req) {
    const AsyncWebParameter* p = req->getParam("tz", true);
    int tz;
    if (!p || !portal::parseTimeZone(p->value().c_str(), &tz)) {
      req->send(400, "text/plain", "Bad time zone");
      return;
    }
    saveTimeZoneOffset(tz);
    req->send(200, "text/plain", "OK");
  });

  s.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (fwSlot.busy()) {
      req->send(409, "text/plain", "A firmware update is running");
      return;
    }
    req->send(200, "text/plain", "Restarting");
    rebootAt = millis() + 1000;
  });

  // Settings page: add GET/POST /api/settings here once the Settings module lands.

  s.onNotFound([](AsyncWebServerRequest* req) {
    if (redirectToPortal(req)) return;
    req->send(404, "text/plain", "Not found");
  });
}

static void buildNetworkName() {
  char buf[24];
  portal::networkName(ESP.getEfuseMac(), buf, sizeof(buf));
  ssid = buf;
}

bool start() {
  if (on) return true;
  if (ssid.length() == 0) buildNetworkName();
  uint32_t t0 = millis();

  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_AP) || !WiFi.softAP(ssid.c_str())) {  // open network, 192.168.4.1
    DEBUG_PRINTLN("Wi-Fi: could not start the network");
    WiFi.mode(WIFI_OFF);
    return false;
  }
  uploadCode.reset(esp_random());
  spaceValid = false;
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  if (!server) {  // created once, after the network stack is up
    server = new AsyncWebServer(80);
    setupRoutes();
    server->begin();
  }
  on = true;
  DEBUG_PRINTF("Wi-Fi: %s on, page at http://%s (%lu ms, %u bytes RAM free)\n", ssid.c_str(),
               WiFi.softAPIP().toString().c_str(), (unsigned long)(millis() - t0), ESP.getFreeHeap());
  return true;
}

void stop() {
  if (!on) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  on = false;
  DEBUG_PRINTLN("Wi-Fi: off");
}

bool active() { return on; }

void loop(const GPSHandler& gps) {
  uint32_t now = millis();
  if (rebootAt && (int32_t)(now - rebootAt) >= 0) {
    DEBUG_PRINTLN("Wi-Fi: restarting");
    stop();
    delay(100);
    ESP.restart();
  }
  if (!on) return;
  dns.processNextRequest();

  if (now - lastSnap >= 500) {
    lastSnap = now;
    GpsSnapshot g;
    g.data = gps.receivingData();
    g.fix = gps.hasFix();
    g.simulated = gps.isSimulated();
    g.sats = gps.satellites();
    g.hdop = gps.hdop();
    g.rateHz = gps.fixRateHz();
    g.speedKmh = gps.lastFix().speedKmh;
    g.lat = gps.lastFix().lat;
    g.lon = gps.lastFix().lon;
    g.rxPin = gps.rxPinUsed();
    g.baud = gps.baud();
    portENTER_CRITICAL(&snapLock);
    gpsSnap = g;
    portEXIT_CRITICAL(&snapLock);
  }
}

const String& networkName() {
  if (ssid.length() == 0) buildNetworkName();
  return ssid;
}

String address() { return on ? WiFi.softAPIP().toString() : String("192.168.4.1"); }

int clientCount() { return on ? WiFi.softAPgetStationNum() : 0; }

int updateProgress() { return fwProgress; }

int uploadCodeValue() { return uploadCode.value(); }

bool busy() { return (fwProgress >= 0 && fwProgress < 100) || rebootAt != 0; }

bool restarting() { return rebootAt != 0; }

}  // namespace WifiPortal
