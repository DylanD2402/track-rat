/**
 * Track Rat - Wi-Fi page implementation
 *
 * Request handlers run in the AsyncTCP task (core 0, next to the Wi-Fi
 * driver), not in loop(). They only touch storage and the GPS snapshot that
 * loop() copies in, never the lap timer or track list.
 */

#include "WifiPortal.h"
#include "Config.h"
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

// Firmware upload (one at a time)
static volatile int fwProgress = -1;
static bool fwOk = false;
static bool fwStarted = false;
static String fwError;

// tracks.json upload (one at a time)
static File tracksFile;
static bool tracksOk = false;
static bool tracksStarted = false;
static String tracksError;

// ---------------------------------------------------------------------------
// Time zone from the phone. The page sends it every time it loads.
// The Settings module (separate branch) should take this over: replace the
// body with a call into it and keep the /api/time handler as it is.

static const char* TZ_PREFS_NAMESPACE = "trackrat";
static const char* TZ_PREFS_KEY = "tz_min";

// minutesEastOfUtc: e.g. -240 for EDT, 60 for CET
static void saveTimeZoneOffset(int minutesEastOfUtc) {
  Preferences prefs;
  if (!prefs.begin(TZ_PREFS_NAMESPACE, false)) return;
  if (!prefs.isKey(TZ_PREFS_KEY) || prefs.getShort(TZ_PREFS_KEY, 0) != minutesEastOfUtc) {
    prefs.putShort(TZ_PREFS_KEY, (int16_t)minutesEastOfUtc);
    DEBUG_PRINTF("Wi-Fi: time zone set to UTC%+d min\n", minutesEastOfUtc);
  }
  prefs.end();
}

static int savedTimeZoneOffset() {
  Preferences prefs;
  if (!prefs.begin(TZ_PREFS_NAMESPACE, true)) return 0;
  int tz = prefs.getShort(TZ_PREFS_KEY, 0);
  prefs.end();
  return tz;
}

// ---------------------------------------------------------------------------

static void appendJsonString(String& out, const String& s) {
  out += '"';
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
}

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

static fs::FS* tracksWriteFs() {
  if (Storage::sdMounted()) return &SD_MMC;
  if (Storage::flashMounted()) return &LittleFS;
  return nullptr;
}

static const char* contentTypeFor(const String& name) {
  if (name.endsWith(".json")) return "application/json";
  if (name.endsWith(".csv")) return "text/csv";
  if (name.endsWith(".gpx")) return "application/gpx+xml";
  if (name.endsWith(".vbo") || name.endsWith(".txt")) return "text/plain";
  return "application/octet-stream";
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
    appendJsonString(out, f.name());
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
  String host = req->host();
  if (host == ip || host == ip + ":80") return false;
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

  JsonObject sd = doc.createNestedObject("sd");
  sd["mounted"] = Storage::sdMounted();
  if (Storage::sdMounted()) {
    sd["totalKB"] = (uint32_t)(SD_MMC.totalBytes() / 1024);
    sd["usedKB"] = (uint32_t)(SD_MMC.usedBytes() / 1024);
  }
  JsonObject fl = doc.createNestedObject("flash");
  fl["mounted"] = Storage::flashMounted();
  if (Storage::flashMounted()) {
    fl["totalKB"] = (uint32_t)(LittleFS.totalBytes() / 1024);
    fl["usedKB"] = (uint32_t)(LittleFS.usedBytes() / 1024);
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

static void onTracksUpload(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
                           size_t len, bool final) {
  fs::FS* fs = tracksWriteFs();
  if (index == 0) {
    tracksStarted = true;
    tracksError = "";
    tracksOk = fs != nullptr;
    if (!fs) tracksError = "No SD card or flash to save to";
    // The page checks the JSON before sending it; here just make sure it's an object.
    size_t i = 0;
    while (i < len && isspace(data[i])) i++;
    if (tracksOk && (i == len || data[i] != '{')) {
      tracksOk = false;
      tracksError = "That isn't a tracks.json file";
    }
    if (tracksOk) {
      tracksFile = fs->open(TRACKS_UPLOAD_TMP, "w");
      if (!tracksFile) {
        tracksOk = false;
        tracksError = "Can't write to storage";
      }
    }
  }
  if (tracksOk && len && tracksFile.write(data, len) != len) {
    tracksOk = false;
    tracksError = "Storage full or write failed";
  }
  if (final) {
    if (tracksFile) tracksFile.close();
    if (tracksOk) {  // keep the old file as tracks.json.bak
      fs->remove(TRACKS_BACKUP);
      if (fs->exists(TRACK_DATABASE_FILE)) fs->rename(TRACK_DATABASE_FILE, TRACKS_BACKUP);
      if (!fs->rename(TRACKS_UPLOAD_TMP, TRACK_DATABASE_FILE)) {
        fs->rename(TRACKS_BACKUP, TRACK_DATABASE_FILE);
        tracksOk = false;
        tracksError = "Couldn't replace tracks.json";
      }
    } else if (fs) {
      fs->remove(TRACKS_UPLOAD_TMP);
    }
    DEBUG_PRINTF("Wi-Fi: tracks.json upload %s (%u bytes)%s\n", tracksOk ? "saved" : "FAILED",
                 (unsigned)(index + len), tracksOk ? "" : (" - " + tracksError).c_str());
  }
}

static void onFirmwareUpload(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
                             size_t len, bool final) {
  if (index == 0) {
    fwStarted = true;
    fwError = "";
    fwProgress = 0;
    fwOk = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    if (!fwOk) fwError = Update.errorString();
    DEBUG_PRINTF("Wi-Fi: firmware upload started (%s)\n", filename.c_str());
    req->onDisconnect([]() {  // phone gave up half way
      if (Update.isRunning()) {
        Update.abort();
        DEBUG_PRINTLN("Wi-Fi: firmware upload cut off - not installed");
      }
      if (fwProgress >= 0 && fwProgress < 100) fwProgress = -1;
    });
  }
  if (fwOk && len && Update.write(data, len) != len) {
    fwOk = false;
    fwError = Update.errorString();
    Update.abort();
  }
  size_t total = req->contentLength();
  if (fwOk && total) fwProgress = min(99, (int)((uint64_t)(index + len) * 100 / total));
  if (final) {
    if (fwOk && !Update.end(true)) {
      fwOk = false;
      fwError = Update.errorString();
    }
    fwProgress = fwOk ? 100 : -1;
    DEBUG_PRINTF("Wi-Fi: firmware %s (%u bytes)%s\n", fwOk ? "installed" : "NOT installed",
                 (unsigned)(index + len), fwOk ? "" : (" - " + fwError).c_str());
  }
}

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
    String out = "{\"files\":[";
    bool first = true;
    if (Storage::sdMounted()) listSessions(SD_MMC, "sd", out, first);
    if (Storage::flashMounted()) listSessions(LittleFS, "flash", out, first);
    out += "]}";
    req->send(200, "application/json", out);
  });

  // /dl?fs=sd&f=<file in /sessions>. Sent a piece at a time as the phone
  // takes it, from the AsyncTCP task.
  s.on("/dl", HTTP_GET, [](AsyncWebServerRequest* req) {
    String name = req->arg("f");
    fs::FS* fs = fsByName(req->arg("fs"));
    if (!fs || name.length() == 0 || name.indexOf('/') >= 0 || name.indexOf("..") >= 0) {
      req->send(400, "text/plain", "Bad file name");
      return;
    }
    String path = String(SESSION_DIR) + "/" + name;
    if (!fs->exists(path)) {
      req->send(404, "text/plain", "No such file");
      return;
    }
    req->send(req->beginResponse(*fs, path, contentTypeFor(name), true));
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

  s.on(
      "/api/tracks", HTTP_POST,
      [](AsyncWebServerRequest* req) {
        if (!tracksStarted) req->send(400, "text/plain", "No file received");
        else if (tracksOk) req->send(200, "text/plain", "Saved. Restart Track Rat to use the new track list.");
        else req->send(400, "text/plain", tracksError);
        tracksStarted = false;
      },
      onTracksUpload);

  s.on(
      "/api/firmware", HTTP_POST,
      [](AsyncWebServerRequest* req) {
        if (!fwStarted) {
          req->send(400, "text/plain", "No file received");
        } else if (fwOk) {
          req->send(200, "text/plain", "Installed. Track Rat is restarting.");
          rebootAt = millis() + 1500;  // after the reply has gone
        } else {
          req->send(400, "text/plain", "Not installed: " + fwError);
        }
        fwStarted = false;
      },
      onFirmwareUpload);

  // The page sends the phone's time zone: tz = minutes east of UTC
  s.on("/api/time", HTTP_POST, [](AsyncWebServerRequest* req) {
    const AsyncWebParameter* p = req->getParam("tz", true);
    int tz = p ? p->value().toInt() : 9999;
    if (tz < -720 || tz > 840) {
      req->send(400, "text/plain", "Bad time zone");
      return;
    }
    saveTimeZoneOffset(tz);
    req->send(200, "text/plain", "OK");
  });

  s.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
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
  uint64_t mac = ESP.getEfuseMac();  // byte 0 of the MAC is the lowest byte
  char buf[24];
  snprintf(buf, sizeof(buf), "TrackRat-%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
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
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  if (!server) {  // created once, after the network stack is up
    server = new AsyncWebServer(80);
    setupRoutes();
    server->begin();
  }
  on = true;
  DEBUG_PRINTF("Wi-Fi: %s on, page at http://%s (%lu ms)\n", ssid.c_str(), WiFi.softAPIP().toString().c_str(),
               (unsigned long)(millis() - t0));
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

bool busy() { return (fwProgress >= 0 && fwProgress < 100) || rebootAt != 0; }

bool restarting() { return rebootAt != 0; }

}  // namespace WifiPortal
