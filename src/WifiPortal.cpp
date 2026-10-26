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
#include "FirmwareUpdate.h"
#include "JsonFile.h"
#include "Logbook.h"
#include "PortalLogic.h"
#include "ProfileStore.h"
#include "SessionLog.h"
#include "Settings.h"
#include "Storage.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <Update.h>
#include <WiFi.h>
#include <memory>
#include <mutex>
#include "esp_task_wdt.h"
#include "AppRollback.h"

#include "Version.h"

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
  bool data, fix, simulated, ubx;
  uint8_t sats;
  float hdop, rateHz, speedKmh;
  int rxPin;
  uint32_t baud;
};
static GpsSnapshot gpsSnap = {};
static portMUX_TYPE snapLock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastSnap = 0;

// Every change from the page needs the code shown on the screen, and uses it up (see UploadCode)
static portal::UploadCode uploadCode;
static volatile bool codeChanged = false;  // the screen shows the code: draw it again

// Session downloads running (the logger shares the card)
static portal::DownloadGate downloads;

// Firmware upload: one at a time (fwSlot holds the request sending it)
static portal::UploadSlot fwSlot;
static firmware::UploadGate fwGate;  // holds the start of the file until it has been checked
static volatile int fwProgress = -1;
static bool fwOk = false;
static String fwError;

// Re-timing: asked for by a web handler, started by loop() on the main loop
static WifiPortal::RetimeHandler retimeHandler;
static std::mutex retimeAskLock;
static bool retimeAsked = false;
static String retimeAskTrack, retimeAskLog, retimeAskError;

// tracks.json upload: one at a time
static portal::UploadSlot tracksSlot;
static File tracksFile;
static fs::FS* tracksFs = nullptr;
static bool tracksOk = false;
static String tracksError;
static int tracksStatus = 400;  // what the reply says if it wasn't saved

// ---------------------------------------------------------------------------
// Time zone from the phone. The page sends it every time it loads; it is
// the Time zone setting, which names session files in local time (see
// GPSHandler::timestampString). Settings keeps it in flash.

int timeZoneOffset() { return Settings::get().utcOffsetMin; }

static void saveTimeZoneOffset(int minutesEastOfUtc) {
  SettingsData s = Settings::get();
  if (s.utcOffsetMin == minutesEastOfUtc) return;
  s.utcOffsetMin = (int16_t)minutesEastOfUtc;
  Settings::set(s);
  DEBUG_PRINTF("Wi-Fi: time zone set to UTC%+d min\n", Settings::get().utcOffsetMin);
}

// Changes from the page need the 4-digit code on Track Rat's screen, and a change that went through
// uses it up: the network is open, so a code someone nearby saw is no use to them. Five wrong codes
// lock changes for a while (UploadCode), and a new code is drawn.

// Does this code open a change? Counts it if it's wrong.
static bool checkCode(const String& given) {
  const int before = uploadCode.value();
  const bool ok = uploadCode.check(given.c_str(), millis(), esp_random());
  if (uploadCode.value() != before) codeChanged = true;  // a lockout drew a new one
  return ok;
}

// The change went through: the code is spent, the screen shows the next one
static void spendCode() {
  uploadCode.spend(esp_random());
  codeChanged = true;
}

// The reply for a code that didn't open a change
static void sendCodeRefused(AsyncWebServerRequest* req) {
  const uint32_t wait = uploadCode.waitMs(millis());
  if (!wait) {
    req->send(403, "text/plain", "Wrong code. Use the 4-digit code on Track Rat's Wi-Fi screen.");
    return;
  }
  char msg[120];
  portal::lockText(wait, msg, sizeof(msg));
  AsyncWebServerResponse* res = req->beginResponse(403, "text/plain", msg);
  res->addHeader("Retry-After", String((wait + 999) / 1000));
  req->send(res);
}

// False (and the reply sent) when the code is wrong or changes are locked.
static bool codeOk(AsyncWebServerRequest* req) {
  if (checkCode(req->arg("code"))) return true;
  sendCodeRefused(req);
  return false;
}

// ---------------------------------------------------------------------------
// Settings tab: GET /api/settings sends them, POST changes any of them by name
// (the names and values are in SettingsData.h). Changing them needs the
// upload code on the screen, like an upload: the network is open, and only
// someone who can see Track Rat should be able to turn its screen upside down.

static void sendSettings(AsyncWebServerRequest* req, int code, bool restartNeeded) {
  char json[512];
  size_t n = settings::toJson(Settings::get(), json, sizeof(json));
  if (!n) {
    req->send(500, "text/plain", "Settings didn't fit in the reply");
    return;
  }
  json[n - 1] = 0;  // drop the closing brace to add one more field
  String out = json;
  out += restartNeeded ? ",\"restartNeeded\":true}" : ",\"restartNeeded\":false}";
  req->send(code, "application/json", out);
}

// Every setting in the POST is applied, or none: they go into a copy that is only saved if all of them were good.
// restart=1 restarts after the save, with the same code ("Save and restart").
static void onSettingsPost(AsyncWebServerRequest* req) {
  if (!codeOk(req)) return;
  const bool restart = req->arg("restart") == "1";
  if (restart && fwSlot.busy()) {
    req->send(409, "text/plain", "A firmware update is running");
    return;
  }
  const SettingsData before = Settings::get();
  SettingsData s = before;
  for (size_t i = 0; i < req->params(); i++) {
    const AsyncWebParameter* p = req->getParam(i);
    if (p->name() == "code" || p->name() == "restart") continue;
    if (!settings::setField(s, p->name().c_str(), p->value().c_str())) {
      req->send(400, "text/plain", "Can't set " + p->name() + " to \"" + p->value() + "\"");
      return;
    }
  }
  Settings::set(s);  // main.cpp's loop applies it (screen, backlight, Bluetooth)
  DEBUG_PRINTLN("Wi-Fi: settings changed from the page");
  sendSettings(req, 200, !restart && settings::needsRestart(before, s));
  if (restart) rebootAt = millis() + 1000;
  spendCode();
}

// ---------------------------------------------------------------------------
// Drivers and cars (8.5): GET /api/profiles sends the lists, the current ones and the last VIN the
// Bluetooth adapter reported; POST changes them: action (add, rename, remove, select, setVin), kind
// (driver or car), id, name, vin and the code. Best laps are kept per driver and car (Profiles.h).
// Whatever is current takes effect at the next session. Only the lists change here: the settings
// (SettingsData.h) don't hold them.

static void onProfilesPost(AsyncWebServerRequest* req) {
  if (!codeOk(req)) return;
  const String action = req->arg("action"), kindText = req->arg("kind");
  if (kindText != "driver" && kindText != "car") {
    req->send(400, "text/plain", "kind is driver or car");
    return;
  }
  const profiles::Kind kind = kindText == "car" ? profiles::Kind::Car : profiles::Kind::Driver;
  const int id = req->arg("id").toInt();  // (0, not an id, if it isn't a number)
  profiles::Err err;
  if (action == "add") err = Profiles::add(kind, req->arg("name").c_str());
  else if (action == "rename") err = Profiles::rename(kind, id, req->arg("name").c_str());
  else if (action == "remove") err = Profiles::remove(kind, id);
  else if (action == "select") err = Profiles::select(kind, id);
  else if (action == "setVin" && kind == profiles::Kind::Car) err = Profiles::setVin(id, req->arg("vin").c_str());
  else {
    req->send(400, "text/plain", "action is add, rename, remove, select or setVin (a car's)");
    return;
  }
  if (err != profiles::Err::Ok) {
    req->send(400, "text/plain", profiles::errText(err));
    return;
  }
  DEBUG_PRINTF("Wi-Fi: %s %s from the page\n", kindText.c_str(), action.c_str());
  req->send(200, "application/json", Profiles::apiJson());
  spendCode();
}

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

// ---------------------------------------------------------------------------
// Session list: every file in /sessions, on the SD card and then in flash.
//
// After a season the card can hold thousands of files. The list is sent a few
// entries at a time as the phone takes them, since handlers run in the
// AsyncTCP task: it has a 5 s watchdog, and the log writer (also core 0, lower
// priority) waits while it works. The folder is read with Storage::DirLister,
// which gives each entry's name and size in one pass and carries on from where
// the last call stopped (see Storage.h for why the card needs that).

static const int LIST_READS_PER_CALL = 32;  // a buffer's worth: a few ms of card reads

class SessionList {
public:
  explicit SessionList(int tzMin) : tz(tzMin) {}

  size_t fill(uint8_t* buf, size_t maxLen) {
    return json.fill(buf, maxLen, LIST_READS_PER_CALL, [this]() { next(); });
  }

private:
  enum { ON_SD, ON_FLASH, DONE };

  bool openDir() {
    fs::FS* fs = nullptr;
    if (stage == ON_SD && Storage::sdMounted()) fs = &SD_MMC;
    if (stage == ON_FLASH && Storage::flashMounted()) fs = &LittleFS;
    return fs && lister.open(*fs, SESSION_DIR);
  }

  // Reads the next entry and adds it if it's a file. At the end of a folder it
  // moves on: the card, then flash, then the end of the list.
  void next() {
    if (lister.isOpen() || openDir()) {
      Storage::DirLister::Entry e;
      if (lister.next(e)) {
        if (!e.isDir) json.addFile(e.name.c_str(), e.size, stage == ON_SD ? "sd" : "flash");
        return;
      }  // (next() closed the folder at its end)
    }
    if (++stage == DONE) json.finish(tz);
  }

  portal::SessionListJson<String> json;
  int tz;
  int stage = ON_SD;
  Storage::DirLister lister;
};

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
<p>Code from Track Rat's screen: <input name=code size=4 maxlength=4 inputmode=numeric></p>
<p>Install firmware (.bin): <input type=file name=firmware accept=.bin> <button>Install</button></p></form>)";

static void sendInfo(AsyncWebServerRequest* req) {
  GpsSnapshot g;
  portENTER_CRITICAL(&snapLock);
  g = gpsSnap;
  portEXIT_CRITICAL(&snapLock);

  DynamicJsonDocument doc(1536);
  doc["version"] = firmwareVersion();
  doc["built"] = firmwareBuilt();
  doc["network"] = ssid;
  doc["uptimeS"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["freePsram"] = ESP.getFreePsram();
  doc["tzMin"] = timeZoneOffset();
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
  gps["hdop"] = g.hdop;  // PDOP when ubx
  gps["ubx"] = g.ubx;
  gps["rateHz"] = g.rateHz;
  gps["speedKmh"] = g.speedKmh;
  doc["units"] = Settings::get().units == SpeedUnits::Mph ? "mph" : "kmh";  // the Speed units setting
  gps["rxPin"] = g.rxPin;
  gps["baud"] = g.baud;
  // (No position: reads are open to anyone on the network, and the page doesn't show it)

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// The logbook (Logbook.h): a note per session, saved as <base>_note.json next
// to it. A .vbo downloaded while it has a note gets the note in its [comments].

// POST /api/note: code, fs (sd or flash), base (the session's file name
// without .vbo/.json), note (empty deletes it). Small, so written here.
static void onNotePost(AsyncWebServerRequest* req) {
  if (!codeOk(req)) return;
  fs::FS* fs = fsByName(req->arg("fs"));
  const String base = req->arg("base");
  if (!fs || !logbook::validBase(base.c_str())) {
    req->send(400, "text/plain", "Bad session");
    return;
  }
  const String dir = String(SESSION_DIR) + "/";
  if (!fs->exists(dir + base + ".vbo") && !fs->exists(dir + base + ".json")) {
    req->send(404, "text/plain", "No such session");
    return;
  }
  const std::string note = logbook::clean(req->arg("note").c_str());
  const String path = logbook::notePath(SESSION_DIR, base.c_str()).c_str();
  bool ok = true;
  if (note.empty()) {
    fs->remove(path + ".tmp");  // (a save cut off by a power loss)
    if (fs->exists(path)) ok = fs->remove(path);
  } else {
    const std::string json = logbook::noteJson(note);
    ok = writeFileSafe(*fs, path, (const uint8_t*)json.data(), json.size());
  }
  spaceValid = false;
  DEBUG_PRINTF("Wi-Fi: note for %s %s (%u bytes)\n", base.c_str(), ok ? "saved" : "NOT saved", (unsigned)note.size());
  if (!ok) {
    req->send(500, "text/plain", "Couldn't save the note. Is the card full?");
    return;
  }
  req->send(200, "application/json", logbook::noteJson(note).c_str());
  spendCode();
}

// The note in <base>_note.json, or empty
static std::string readNote(fs::FS& fs, const String& base) {
  File f = fs.open(logbook::notePath(SESSION_DIR, base.c_str()).c_str());
  if (!f || f.size() > 4 * logbook::MAX_NOTE) return std::string();
  DynamicJsonDocument doc(3 * logbook::MAX_NOTE);
  if (deserializeJson(doc, f)) return std::string();
  const char* note = doc["note"] | "";
  return logbook::clean(note);
}

// A .vbo with a note: sent with the note's lines after "[comments]". False
// if it has no note (or no [comments]), to send the file as it is.
static bool sendVboWithNote(AsyncWebServerRequest* req, fs::FS& fs, const String& path, const String& name) {
  const String base = name.substring(0, name.length() - 4);
  if (!logbook::validBase(base.c_str())) return false;
  // (a note whose save was cut between removing the old file and renaming the new one is put back first)
  if (!finishLeftoverSave(fs, logbook::notePath(SESSION_DIR, base.c_str()).c_str(), jsonFileWhole)) return false;

  struct Splice {
    File f;
    size_t size, at;
    std::string lines;
  };
  std::shared_ptr<Splice> sp = std::make_shared<Splice>();
  sp->lines = logbook::commentLines(readNote(fs, base));
  if (sp->lines.empty()) return false;
  sp->f = fs.open(path);
  if (!sp->f) return false;
  char head[1024];
  const size_t n = sp->f.read((uint8_t*)head, sizeof(head));
  sp->at = logbook::commentsEnd(head, n);
  if (!sp->at) return false;
  sp->size = sp->f.size();

  AsyncWebServerResponse* res = req->beginResponse(
      portal::contentTypeFor(name.c_str()), sp->size + sp->lines.size(),
      [sp](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
        return logbook::splicedChunk(buf, maxLen, index, sp->size, sp->at, sp->lines,
                                     [sp](size_t off, uint8_t* dst, size_t len) -> size_t {
                                       if (sp->f.position() != off && !sp->f.seek(off)) return 0;
                                       return sp->f.read(dst, len);
                                     });
      });
  res->addHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  req->send(res);
  return true;
}

// ---------------------------------------------------------------------------
// tracks.json upload: written to a temp file, checked, then swapped in, old one kept as .bak
// (at most TRACKS_MAX_BYTES: the gate in setupRoutes refuses a bigger Content-Length before reading it)

// Why an upload that didn't get to start was refused
static void refuseUpload(AsyncWebServerRequest* req, const portal::UploadSlot& slot) {
  if (uploadCode.locked(millis()) || !uploadCode.matches(req->arg("code").c_str()))
    sendCodeRefused(req);
  else if (slot.busy())
    req->send(409, "text/plain", "Another upload is running. Try again when it's done.");
  else
    req->send(400, "text/plain", "No file received");
}

static void dropTracksUpload() {
  if (tracksFile) tracksFile.close();
  if (tracksFs) tracksFs->remove(TRACKS_UPLOAD_TMP);
}

// A file read through a 1 KB buffer. The JSON parser takes a byte at a time, which from the card is slow, and
// the AsyncTCP task has a 5 s watchdog: it is fed as the file is read.
class BufferedFile : public Stream {
public:
  explicit BufferedFile(File& f) : file(f) {}
  int available() override { return (int)(filled - at) + file.available(); }
  int read() override { return (at < filled || fill()) ? buf[at++] : -1; }
  int peek() override { return (at < filled || fill()) ? buf[at] : -1; }
  size_t write(uint8_t) override { return 0; }
  size_t readBytes(char* out, size_t len) override {
    size_t got = 0;
    while (got < len && (at < filled || fill())) {
      size_t n = filled - at < len - got ? filled - at : len - got;
      memcpy(out + got, buf + at, n);
      at += n;
      got += n;
    }
    return got;
  }

private:
  bool fill() {
    esp_task_wdt_reset();
    filled = file.read(buf, sizeof(buf));
    at = 0;
    return filled > 0;
  }
  File& file;
  uint8_t buf[1024];
  size_t at = 0, filled = 0;
};

// The same check as TrackDatabase's validTracksFile: parses as JSON and has a "tracks" list. Nothing is kept in
// memory, so a file up to the 2 MB cap is no strain.
static bool validTracksUpload(fs::FS& fs, const char* path) {
  File f = fs.open(path, "r");
  if (!f) return false;
  BufferedFile in(f);
  StaticJsonDocument<64> filter;
  filter["tracks"][0] = false;
  StaticJsonDocument<64> doc;
  const DeserializationError err = deserializeJson(doc, in, DeserializationOption::Filter(filter));
  f.close();
  return !err && doc["tracks"].is<JsonArray>();
}

// Free space on the card or flash, from the figures the Device tab keeps (at most 30 s old: a write that
// runs out of room still fails, this only turns most of them away before they start)
static bool roomForUpload(fs::FS& fs, size_t bytes) {
  const StorageSpace& sp = storageSpace();
  const uint32_t total = &fs == &SD_MMC ? sp.sdTotalKB : sp.flashTotalKB;
  const uint32_t used = &fs == &SD_MMC ? sp.sdUsedKB : sp.flashUsedKB;
  return portal::roomFor(total > used ? (uint64_t)(total - used) * 1024 : 0, bytes);
}

static void onTracksUpload(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
                           size_t len, bool final) {
  if (index == 0) {
    if (!checkCode(req->arg("code"))) return;  // refused in the reply
    if (!tracksSlot.claim(req)) return;         // another upload is running
    tracksError = "";
    tracksStatus = 400;
    tracksFs = Storage::sessionFS();  // the SD card, else flash: where TrackDatabase saves
    tracksOk = tracksFs != nullptr;
    if (!tracksOk) tracksError = "No SD card or flash to save to";
    // The page checks the JSON before sending it; here just make sure it's an object (the whole file is checked at the end).
    if (tracksOk && !portal::looksLikeJsonObject(data, len)) {
      tracksOk = false;
      tracksError = "That isn't a tracks.json file";
    }
    if (tracksOk && !roomForUpload(*tracksFs, req->contentLength())) {
      tracksOk = false;
      tracksStatus = 507;
      tracksError = "Not enough room for that file on the SD card or in flash";
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
  if (tracksOk && index + len > portal::TRACKS_MAX_BYTES) {
    tracksOk = false;
    tracksStatus = 413;
    tracksError = "That file is too big. Track Rat takes a tracks.json up to 2 MB.";
  }
  if (tracksOk && len && tracksFile.write(data, len) != len) {
    tracksOk = false;
    tracksStatus = 507;
    tracksError = "Storage full or write failed";
  }
  if (!tracksOk && tracksFile) dropTracksUpload();  // no use keeping what's been written
  if (!final) return;

  fs::FS* fs = tracksFs;
  if (tracksFile) tracksFile.close();
  if (tracksOk && !validTracksUpload(*fs, TRACKS_UPLOAD_TMP)) {
    tracksOk = false;
    tracksError = "That isn't a good tracks.json: it needs a \"tracks\" list, and the file must be whole.";
  }
  if (tracksOk) {
    fs->remove(TRACKS_BACKUP);
    const bool hadOld = fs->exists(TRACK_DATABASE_FILE);
    if (hadOld) fs->rename(TRACK_DATABASE_FILE, TRACKS_BACKUP);
    if (!replaceFile(*fs, TRACKS_UPLOAD_TMP, TRACK_DATABASE_FILE)) {
      if (hadOld) fs->rename(TRACKS_BACKUP, TRACK_DATABASE_FILE);
      tracksOk = false;
      tracksStatus = 500;
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
    spendCode();
  } else {
    req->send(tracksStatus, "text/plain", tracksError);
  }
  tracksSlot.release(req);
}

// ---------------------------------------------------------------------------
// Firmware upload: the first 128 bytes are checked the way an SD card file is (chip, app image)
// before anything is written. Then it goes straight into the other app slot by Update.

static void writeFirmware(const uint8_t* data, size_t len) {
  if (!fwOk || !len) return;
  if (Update.write(const_cast<uint8_t*>(data), len) != len) {
    fwOk = false;
    fwError = Update.errorString();
    Update.abort();
  }
}

static void onFirmwareUpload(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
                             size_t len, bool final) {
  if (index == 0) {
    if (!checkCode(req->arg("code"))) return;  // refused in the reply
    if (!fwSlot.claim(req)) return;             // another upload is running
    fwError = "";
    fwProgress = 0;
    fwOk = true;
    fwGate.reset(FirmwareUpdate::slotSize());
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
  size_t got = len;  // this chunk's size, for the progress figure
  if (fwOk && !fwGate.decided()) {
    size_t used = fwGate.feed(data, len, final);  // held in the gate until it has seen the header
    data += used;
    len -= used;
    if (fwGate.decided()) {
      if (fwGate.ok()) {
        approllback::markGood();  // Update refuses to start while this app is still waiting to be trusted
        fwOk = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
        if (!fwOk) fwError = Update.errorString();
        writeFirmware(fwGate.header(), fwGate.headerLen());
      } else {
        fwOk = false;
        fwError = firmware::describe(fwGate.outcome());
      }
    }
  }
  if (fwGate.decided()) writeFirmware(data, len);  // before the decision the whole chunk is in the gate
  if (fwOk) fwProgress = portal::uploadPercent(index + got, req->contentLength());
  if (!final) return;

  if (fwOk && !Update.end(true)) {
    fwOk = false;
    fwError = Update.errorString();
  }
  fwProgress = fwOk ? 100 : -1;
  DEBUG_PRINTF("Wi-Fi: firmware %s (%u bytes)%s\n", fwOk ? "installed" : "NOT installed", (unsigned)(index + got),
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
    spendCode();
  } else {
    if (Update.isRunning()) Update.abort();  // the file ended without a last piece
    fwProgress = -1;
    req->send(400, "text/plain", "Not installed: " + (fwError.length() ? fwError : String("incomplete file")));
  }
  fwSlot.release(req);
}

// ---------------------------------------------------------------------------

// Refuses a POST from the headers alone, before its body is read: one from another web page (Origin), or one
// whose Content-Length is over its limit (portal::postRefusal). The library skips a body that no handler wants
// instead of keeping it, so a big one costs time on the Wi-Fi link and no memory. Added first, so it sees every POST.
class PostGate : public AsyncWebHandler {
public:
  bool canHandle(AsyncWebServerRequest* req) const override { return req->method() == HTTP_POST && refusal(req) != 0; }

  void handleRequest(AsyncWebServerRequest* req) override {
    switch (refusal(req)) {
      case 403: req->send(403, "text/plain", "Changes only come from Track Rat's own page."); break;
      case 411: req->send(411, "text/plain", "The request needs a length."); break;
      default: req->send(413, "text/plain", "That is too big to send to Track Rat."); break;
    }
  }

private:
  static int refusal(AsyncWebServerRequest* req) {
    const String ip = WiFi.softAPIP().toString();
    const bool hasOrigin = req->hasHeader("Origin");
    const String origin = hasOrigin ? req->header("Origin") : String();
    return portal::postRefusal(req->url().c_str(), req->contentLength(), req->hasHeader("Transfer-Encoding"),
                               hasOrigin ? origin.c_str() : nullptr, ip.c_str(), FirmwareUpdate::slotSize());
  }
};

// A session download: at most two at once, or the logger waits for the card. The place is given back when the
// connection closes, which is when every reply ends. False (and the reply sent) when both places are taken.
static bool beginDownload(AsyncWebServerRequest* req) {
  if (!downloads.enter()) {
    AsyncWebServerResponse* res = req->beginResponse(503, "text/plain", "Track Rat is busy sending two files. Try again in a moment.");
    res->addHeader("Retry-After", "2");
    req->send(res);
    return false;
  }
  req->onDisconnect([]() { downloads.leave(); });
  return true;
}

static void setupRoutes() {
  AsyncWebServer& s = *server;
  s.addHandler(new PostGate());

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

  // The page's fonts (Inter), copied from web/fonts by tools/build_web.py. They
  // only change with a new page, so phones may keep them.
  s.serveStatic("/fonts/", LittleFS, "/web/fonts/").setCacheControl("max-age=86400");

  // The page's logo, copied from web/logo.png by tools/build_web.py
  s.serveStatic("/logo.png", LittleFS, "/web/logo.png").setCacheControl("max-age=86400");

  s.on("/api/info", HTTP_GET, sendInfo);

  // Sent a piece at a time (see SessionList). The reply holds the list, so its
  // folder is closed when the reply ends or the phone goes away.
  s.on("/api/sessions", HTTP_GET, [](AsyncWebServerRequest* req) {
    std::shared_ptr<SessionList> list = std::make_shared<SessionList>(timeZoneOffset());
    req->sendChunked("application/json",
                     [list](uint8_t* buf, size_t maxLen, size_t) { return list->fill(buf, maxLen); });
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
    if (!beginDownload(req)) return;
    if (portal::endsWith(name.c_str(), ".vbo") && sendVboWithNote(req, *fs, path, name)) return;
    req->send(req->beginResponse(*fs, path, portal::contentTypeFor(name.c_str()), true));
  });

  s.on("/tracks.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    fs::FS* fs = tracksReadFs();
    if (!fs) {
      req->send(404, "text/plain", "No tracks.json on the SD card or in flash (the built-in list is in use)");
      return;
    }
    if (!beginDownload(req)) return;
    AsyncWebServerResponse* res = req->beginResponse(*fs, TRACK_DATABASE_FILE, "application/json", true);
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
  });

  s.on("/api/tracks", HTTP_POST, onTracksDone, onTracksUpload);
  s.on("/api/note", HTTP_POST, onNotePost);
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
    if (!codeOk(req)) return;
    if (fwSlot.busy()) {
      req->send(409, "text/plain", "A firmware update is running");
      return;
    }
    req->send(200, "text/plain", "Restarting");
    rebootAt = millis() + 1000;
    spendCode();
  });

  // Re-timing (the Tracks tab): track=<id>, file=<a .vbo in /sessions> or none for every session there
  s.on("/api/retime", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!codeOk(req)) return;
    const String track = req->arg("track"), file = req->arg("file");
    if (!track.length() || track.length() > 80 ||
        (file.length() && (!portal::isSafeFileName(file.c_str()) || !portal::endsWith(file.c_str(), ".vbo")))) {
      req->send(400, "text/plain", "Bad track or file");
      return;
    }
    std::lock_guard<std::mutex> g(retimeAskLock);
    if (retimeAsked || SessionLog::retiming()) {
      req->send(409, "text/plain", "Already re-timing");
      return;
    }
    retimeAsked = true;
    retimeAskTrack = track;
    retimeAskLog = file;
    retimeAskError = "";
    req->send(200, "text/plain", "Re-timing");
    spendCode();
  });
  s.on("/api/retime", HTTP_GET, [](AsyncWebServerRequest* req) {
    const SessionLog::RetimeStatus st = SessionLog::retimeStatus();
    String error;
    bool asked;
    {
      std::lock_guard<std::mutex> g(retimeAskLock);
      asked = retimeAsked;
      error = retimeAskError;
    }
    String out = "{\"running\":";
    out += (st.running || asked) ? "true" : "false";
    out += ",\"done\":" + String(st.done) + ",\"total\":" + String(st.total) + ",\"laps\":" + String(st.laps);
    out += ",\"message\":";
    portal::appendJsonString(out, (error.length() ? error : st.message).c_str());
    out += ",\"error\":";
    out += error.length() ? "true" : "false";
    out += "}";
    req->send(200, "application/json", out);
  });

  // The Settings tab
  s.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req) { sendSettings(req, 200, false); });
  s.on("/api/settings", HTTP_POST, onSettingsPost);
  s.on("/api/profiles", HTTP_GET, [](AsyncWebServerRequest* req) { req->send(200, "application/json", Profiles::apiJson()); });
  s.on("/api/profiles", HTTP_POST, onProfilesPost);

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
  codeChanged = true;
  downloads = portal::DownloadGate();  // (none can be running with the network down)
  spaceValid = false;
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  if (!server) {  // created once, after the network stack is up
    // On every reply. The page has inline scripts and styles, nothing from elsewhere: its own files, the fonts, the
    // logo, and the board's own API. It can't be framed (clickjacking) and files keep the type they were sent as.
    DefaultHeaders::Instance().addHeader("X-Frame-Options", "DENY");
    DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
    DefaultHeaders::Instance().addHeader(
        "Content-Security-Policy",
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data: blob:; "
        "font-src 'self'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
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
    approllback::markGood();  // a restart asked for isn't a crash (and the one after an install is of the new app)
    stop();
    delay(100);
    ESP.restart();
  }
  if (!on) return;
  dns.processNextRequest();

  // Re-timing asked for: started here, where the track list may be read
  String track, log;
  bool ask = false;
  {
    std::lock_guard<std::mutex> g(retimeAskLock);
    if (retimeAsked) {
      ask = true;
      track = retimeAskTrack;
      log = retimeAskLog;
    }
  }
  if (ask) {
    String error;
    bool ok = retimeHandler && retimeHandler(track, log, error);
    if (!ok && !error.length()) error = "Re-timing isn't available";
    std::lock_guard<std::mutex> g(retimeAskLock);
    retimeAsked = false;
    retimeAskError = ok ? String() : error;
  }

  if (now - lastSnap >= 500) {
    lastSnap = now;
    GpsSnapshot g;
    g.data = gps.receivingData();
    g.fix = gps.hasFix();
    g.simulated = gps.isSimulated();
    g.ubx = gps.usingUbx();
    g.sats = gps.satellites();
    g.hdop = gps.hdop();
    g.rateHz = gps.fixRateHz();
    g.speedKmh = gps.lastFix().speedKmh;
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

bool takeCodeChanged() {
  const bool changed = codeChanged;
  codeChanged = false;
  return changed;
}

bool busy() { return (fwProgress >= 0 && fwProgress < 100) || rebootAt != 0 || SessionLog::retiming(); }

void onRetime(RetimeHandler handler) { retimeHandler = handler; }

bool restarting() { return rebootAt != 0; }

}  // namespace WifiPortal
