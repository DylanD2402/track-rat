/**
 * Track Rat - the DJI camera's GPS remote implementation (see CameraBle.h).
 * Untested with a camera.
 */

#include "CameraBle.h"
#include "Config.h"
#include <Arduino.h>

#if BLE_ENABLED && CAMERA_BLE_ENABLED

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_random.h>
#include <freertos/stream_buffer.h>
#include "RaceBoxBle.h"
#include "Settings.h"

namespace CameraBle {

constexpr uint32_t RETRY_MIN_MS = 5000, RETRY_MAX_MS = 10000;  // between attempts to reach the camera
constexpr uint8_t CONNECT_TIMEOUT_S = 5;
constexpr uint32_t GPS_PERIOD_MS = 100;  // 10 Hz, as DJI's remote
constexpr uint32_t GPS_STALE_MS = 500;   // no fix this recent: send nothing

// --- shared between the task and loop() (under lock) ---
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static Status shared;
static Found foundList[MAX_FOUND];
static int foundCount = 0;
static volatile bool scanAsked = false, scanOn = false;
static volatile bool settingsChanged = false;
static char wantAddr[18] = "", wantName[24] = "";
static uint8_t wantType = 0;
static bool paired = false;           // the camera has accepted us once: reconnect without the pop-up
static dji::GpsData gpsNow;
static uint32_t gpsAt = 0;            // millis() of gpsNow (0: none)
static volatile bool recordWanted = false;
static bool startedFlag = false;
static uint32_t startedMs = 0;

// --- loop()'s copy ---
static Status st;

// --- the task's own ---
static TaskHandle_t task = nullptr;
static StreamBufferHandle_t rx = nullptr;
static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* writeChr = nullptr;
static dji::Remote remote;
static dji::Assembler assembler;
static dji::RecordFollow follow;
static uint8_t frameBuf[dji::MAX_FRAME];
static bool wasLinked = false;
static bool writeWithResponse = false;
static bool refused = false;  // rejected on the camera: don't ask again until Pair or Forget
static uint32_t gpsSent = 0;

static void notified(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (rx) xStreamBufferSend(rx, data, len, 0);
}

static void copyText(char* dst, size_t n, const char* src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

static void savePaired(bool p) {
  Preferences prefs;
  if (prefs.begin("cam", false)) {
    prefs.putBool("paired", p);
    prefs.end();
  }
  portENTER_CRITICAL(&lock);
  paired = p;
  portEXIT_CRITICAL(&lock);
}

static void loadSaved() {
  Preferences p;
  char a[18] = "", nm[24] = "";
  uint8_t type = 0;
  bool pr = false;
  if (p.begin("cam", true)) {
    p.getString("addr", a, sizeof(a));
    p.getString("name", nm, sizeof(nm));
    type = p.getUChar("type", 0);
    pr = p.getBool("paired", false);
    p.end();
  }
  portENTER_CRITICAL(&lock);
  copyText(wantAddr, sizeof(wantAddr), a);
  copyText(wantName, sizeof(wantName), nm);
  wantType = type;
  paired = pr;
  portEXIT_CRITICAL(&lock);
}

// The camera's service FFF0: notify FFF4, write FFF5 (DJI's demo, and Telemet with the Action 4)
static bool findCharacteristics() {
  writeChr = nullptr;
  NimBLERemoteService* svc = client->getService(NimBLEUUID((uint16_t)0xFFF0));
  if (!svc) return false;
  NimBLERemoteCharacteristic* n = svc->getCharacteristic(NimBLEUUID((uint16_t)0xFFF4));
  NimBLERemoteCharacteristic* w = svc->getCharacteristic(NimBLEUUID((uint16_t)0xFFF5));
  if (!n || !w || !n->subscribe(true, notified, true)) return false;
  writeChr = w;
  writeWithResponse = !w->canWriteNoResponse();
  return true;
}

// Our Bluetooth address, in the order it's written (the demo's mac_addr[0] is the first pair)
static void ourMac(uint8_t mac[6]) {
  const uint8_t* le = NimBLEDevice::getAddress().getNative();  // least significant first
  for (int i = 0; i < 6; i++) mac[i] = le[5 - i];
}

static bool connectTo(const char* addr, uint8_t type, bool pair) {
  if (!client) {
    client = NimBLEDevice::createClient();
    if (!client) return false;
    client->setConnectTimeout(CONNECT_TIMEOUT_S);
  }
  if (!RaceBoxBle::radioTryLock(100)) return false;  // the OBD adapter is connecting or scanning: next time
  const bool ok = client->connect(NimBLEAddress(std::string(addr), type), true) && findCharacteristics();
  RaceBoxBle::radioUnlock();
  if (!ok) {
    if (client->isConnected()) {
      DEBUG_PRINTLN("Camera: no DJI remote service on that device");
      client->disconnect();
    }
    return false;
  }
  xStreamBufferReset(rx);
  assembler.clear();
  follow.reset();
  gpsSent = 0;
  uint8_t mac[6];
  ourMac(mac);
  const uint16_t code = pair ? (uint16_t)(1000 + esp_random() % 9000) : (uint16_t)(esp_random() & 0xFFFF);
  remote.begin(mac, pair, code, millis());
  RaceBoxBle::resumeAdvertising();
  DEBUG_PRINTF("Camera: connected to %s, %s\n", addr, pair ? "pairing" : "reconnecting");
  return true;
}

static void scanDone(NimBLEScanResults res) {
  Found list[MAX_FOUND];
  int n = 0;
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    if (!d.haveManufacturerData()) continue;
    const std::string m = d.getManufacturerData();
    if (!dji::isCamera((const uint8_t*)m.data(), (int)m.size())) continue;
    Found f;
    const std::string name = d.getName();
    copyText(f.name, sizeof(f.name), name.empty() ? "DJI camera" : name.c_str());
    copyText(f.address, sizeof(f.address), d.getAddress().toString().c_str());
    f.addressType = d.getAddress().getType();
    f.rssi = d.getRSSI();
    int at = n;
    while (at > 0 && list[at - 1].rssi < f.rssi) at--;
    if (at >= MAX_FOUND) continue;
    for (int k = (n < MAX_FOUND ? n : MAX_FOUND - 1); k > at; k--) list[k] = list[k - 1];
    list[at] = f;
    if (n < MAX_FOUND) n++;
  }
  NimBLEDevice::getScan()->clearResults();
  portENTER_CRITICAL(&lock);
  memcpy(foundList, list, sizeof(Found) * n);
  foundCount = n;
  scanOn = false;
  portEXIT_CRITICAL(&lock);
  RaceBoxBle::radioUnlock();
  DEBUG_PRINTF("Camera: scan found %d camera(s)\n", n);
}

static void publish(bool linked) {
  Status s;
  portENTER_CRITICAL(&lock);
  copyText(s.name, sizeof(s.name), wantName);
  copyText(s.address, sizeof(s.address), wantAddr);
  portEXIT_CRITICAL(&lock);
  s.configured = s.address[0] != 0;
  s.linked = linked;
  s.phase = linked ? remote.state() : dji::Remote::Phase::Off;
  s.pairing = remote.pairingAsked();
  s.code = remote.code();
  s.cam = remote.status();
  s.gpsSent = gpsSent;
  s.refused = refused;
  uint32_t at;
  const bool started = remote.takeStarted(at);
  portENTER_CRITICAL(&lock);
  shared = s;
  if (started) startedFlag = true, startedMs = at;
  portEXIT_CRITICAL(&lock);
}

static void writeOut() {
  for (int i = 0; i < remote.pending(); i++)
    if (writeChr && !writeChr->writeValue(remote.out(i).data, remote.out(i).len, writeWithResponse)) DEBUG_PRINTLN("Camera: write failed");
  remote.sent();
}

static void taskMain(void*) {
  uint32_t retryAt = 0, retryMs = RETRY_MIN_MS, lastGps = 0;
  for (;;) {
    const uint32_t now = millis();

    if (settingsChanged) {  // a new camera, or none
      settingsChanged = false;
      if (client && client->isConnected()) client->disconnect();
      remote.end();
      writeChr = nullptr;
      wasLinked = false;
      refused = false;
      retryAt = now;
      retryMs = RETRY_MIN_MS;
    }

    if (scanAsked && !scanOn) {
      scanOn = true;
      scanAsked = false;
      if (!RaceBoxBle::radioTryLock(3000)) {  // the OBD adapter is connecting or scanning: found nothing
        scanOn = false;
      } else {
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(60);
        if (!scan->start(CAMERA_SCAN_S, scanDone, false)) {
          scanOn = false;
          RaceBoxBle::radioUnlock();
        }
      }
    }

    char addr[18];
    uint8_t type;
    bool pr;
    portENTER_CRITICAL(&lock);
    copyText(addr, sizeof(addr), wantAddr);
    type = wantType;
    pr = paired;
    portEXIT_CRITICAL(&lock);

    const bool linked = client && client->isConnected() && writeChr;
    if (wasLinked && !linked) {
      DEBUG_PRINTLN("Camera: disconnected");
      remote.end();
      writeChr = nullptr;
      retryAt = now + RETRY_MIN_MS;
      retryMs = RETRY_MIN_MS;
    }
    wasLinked = linked;

    if (!linked) {
      if (addr[0] && !scanOn && !refused && (int32_t)(now - retryAt) >= 0) {
        if (connectTo(addr, type, !pr)) {
          wasLinked = true;
          retryMs = RETRY_MIN_MS;
          writeOut();
        } else {
          if (client && client->isConnected()) client->disconnect();
          retryAt = millis() + retryMs;
          retryMs = retryMs + 2500 > RETRY_MAX_MS ? RETRY_MAX_MS : retryMs + 2500;
        }
      }
    } else {
      uint8_t in[64];
      size_t n;
      while ((n = xStreamBufferReceive(rx, in, sizeof(in), 0)) > 0) {
        assembler.feed(in, (int)n);
        dji::Frame f;
        while (assembler.next(frameBuf, f)) {
          const dji::Remote::Phase before = remote.state();
          remote.onFrame(f, millis());
          if (before != dji::Remote::Phase::Connected && remote.connected()) {
            DEBUG_PRINTLN("Camera: accepted - Track Rat is its GPS remote");
            if (!pr) savePaired(true), pr = true;
          }
        }
      }
      remote.poll(millis());
      if (remote.state() == dji::Remote::Phase::Rejected || remote.state() == dji::Remote::Phase::Failed) {
        DEBUG_PRINTF("Camera: %s\n", remote.state() == dji::Remote::Phase::Rejected ? "rejected on the camera" : "no answer");
        if (remote.state() == dji::Remote::Phase::Rejected) {  // the driver said no: wait for Pair again
          refused = true;
          if (pr) savePaired(false);
        }
        client->disconnect();
        remote.end();
        writeChr = nullptr;
        wasLinked = false;
        retryAt = millis() + RETRY_MAX_MS;
      } else if (remote.connected()) {
        // GPS at 10 Hz from the latest fix
        dji::GpsData g;
        uint32_t at;
        portENTER_CRITICAL(&lock);
        g = gpsNow;
        at = gpsAt;
        portEXIT_CRITICAL(&lock);
        const uint32_t t = millis();
        if (at && t - at <= GPS_STALE_MS && t - lastGps >= GPS_PERIOD_MS) {
          lastGps = t;
          remote.sendGps(g, DJI_HOUR_OFFSET);
          gpsSent++;
        }
        // Recording follows the sessions
        if (Settings::get().cameraWithSessions) {
          const int cmd = follow.step(t, recordWanted, remote.status());
          if (cmd >= 0) {
            DEBUG_PRINTF("Camera: %s recording\n", cmd == 0 ? "start" : "stop");
            remote.sendRecord(cmd == 0);
          }
        }
      }
      writeOut();
    }

    publish(linked);
    vTaskDelay(pdMS_TO_TICKS(linked ? 5 : 50));
  }
}

static void startTask() {
  if (task) return;
  RaceBoxBle::initRadio();
  rx = xStreamBufferCreate(1024, 1);
  if (!rx || xTaskCreatePinnedToCore(taskMain, "camera", 8192, nullptr, 1, &task, 0) != pdPASS) {
    DEBUG_PRINTLN("Camera: task didn't start");
    task = nullptr;
  }
}

void begin() {
  loadSaved();
  if (wantAddr[0]) {
    DEBUG_PRINTF("Camera: %s (%s)%s\n", wantName, wantAddr, paired ? "" : ", not paired yet");
    startTask();
  }
}

void poll() {
  portENTER_CRITICAL(&lock);
  st = shared;
  portEXIT_CRITICAL(&lock);
}

void onFix(const GpsFix& fix, bool hasFix) {
  if (!task || !hasFix || fix.year == 0) return;
  dji::GpsData g;
  g.year = fix.year, g.month = fix.month, g.day = fix.day;
  g.utcMs = fix.gpsTimeMs;
  g.lat = fix.lat, g.lon = fix.lon;
  g.heightM = fix.altM;
  g.speedKmh = fix.speedKmh;
  g.courseDeg = fix.courseValid ? fix.courseDeg : 0;
  g.climbMs = fix.vertSpeedMs;
  g.hAccM = fix.hAccM, g.vAccM = fix.vAccM, g.speedAccMs = fix.speedAccMs;
  g.sats = fix.sats;
  portENTER_CRITICAL(&lock);
  gpsNow = g;
  gpsAt = fix.rxMillis ? fix.rxMillis : millis();
  portEXIT_CRITICAL(&lock);
}

void setRecording(bool want) { recordWanted = want; }

bool configured() { return st.configured; }
bool connected() { return st.phase == dji::Remote::Phase::Connected; }
Status status() { return st; }

bool takeStarted(uint32_t& atMs) {
  portENTER_CRITICAL(&lock);
  const bool s = startedFlag;
  atMs = startedMs;
  startedFlag = false;
  portEXIT_CRITICAL(&lock);
  return s;
}

void startScan() {
  startTask();
  if (!task) return;
  portENTER_CRITICAL(&lock);
  foundCount = 0;
  portEXIT_CRITICAL(&lock);
  scanAsked = true;
}

bool scanning() { return scanAsked || scanOn; }

int found(Found* out, int max) {
  portENTER_CRITICAL(&lock);
  const int n = foundCount < max ? foundCount : max;
  memcpy(out, foundList, sizeof(Found) * n);
  portEXIT_CRITICAL(&lock);
  return n;
}

void choose(const Found& f) {
  Preferences p;
  if (p.begin("cam", false)) {
    p.putString("addr", f.address);
    p.putString("name", f.name);
    p.putUChar("type", f.addressType);
    p.putBool("paired", false);  // a new camera: it asks the driver to accept
    p.end();
  }
  portENTER_CRITICAL(&lock);
  copyText(wantAddr, sizeof(wantAddr), f.address);
  copyText(wantName, sizeof(wantName), f.name);
  wantType = f.addressType;
  paired = false;
  portEXIT_CRITICAL(&lock);
  settingsChanged = true;
  startTask();
  DEBUG_PRINTF("Camera: pairing with %s (%s)\n", f.name, f.address);
}

void forget() {
  Preferences p;
  if (p.begin("cam", false)) {
    p.clear();
    p.end();
  }
  portENTER_CRITICAL(&lock);
  wantAddr[0] = wantName[0] = 0;
  paired = false;
  portEXIT_CRITICAL(&lock);
  settingsChanged = true;
  DEBUG_PRINTLN("Camera: forgotten");
}

}  // namespace CameraBle

#else  // no camera support

namespace CameraBle {
void begin() {}
void poll() {}
void onFix(const GpsFix&, bool) {}
void setRecording(bool) {}
bool configured() { return false; }
bool connected() { return false; }
bool takeStarted(uint32_t&) { return false; }
Status status() { return Status(); }
void startScan() {}
bool scanning() { return false; }
int found(Found*, int) { return 0; }
void choose(const Found&) {}
void forget() {}
}  // namespace CameraBle

#endif
