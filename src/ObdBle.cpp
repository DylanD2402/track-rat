/**
 * Track Rat - Bluetooth LE OBD-II adapter implementation (see ObdBle.h).
 * Untested on hardware.
 */

#include "ObdBle.h"
#include "Config.h"
#include "DeviceLink.h"
#include <Arduino.h>

#if BLE_ENABLED && OBD_BLE_ENABLED

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <algorithm>
#include <freertos/stream_buffer.h>
#include "RaceBoxBle.h"

namespace ObdBle {

constexpr uint32_t RETRY_MIN_MS = 5000, RETRY_MAX_MS = 10000;  // between attempts to reach the adapter
constexpr uint8_t CONNECT_TIMEOUT_S = 5;

// --- shared between the task and loop() (under lock) ---
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static Status shared;
static obd::Values sharedVals;
static Found foundList[MAX_FOUND];
static int foundCount = 0;
static volatile bool scanAsked = false, scanOn = false;
static volatile bool settingsChanged = false;  // choose() or forget(): the task reloads
static char wantAddr[18] = "", wantName[24] = "";
static uint8_t wantType = 0;

// --- loop()'s copies ---
static obd::Values vals;
static Status st;

// --- the task's own ---
static TaskHandle_t task = nullptr;
static StreamBufferHandle_t rx = nullptr;  // the adapter's notifications, from NimBLE's task to ours
static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* writeChr = nullptr;
static bool writeWithResponse = false;
static elm::Client elmc;
static bool wasConnected = false;
static uint32_t lastRxAt = 0;  // millis() of the adapter's last bytes on this link (0: none yet)

static void notified(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (rx) xStreamBufferSend(rx, data, len, 0);  // full: dropped, and the reply times out
}

static void copyText(char* dst, size_t n, const char* src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

static void loadSaved() {
  Preferences p;
  char a[18] = "", nm[24] = "";
  uint8_t type = 0;
  if (p.begin("obd", true)) {
    p.getString("addr", a, sizeof(a));
    p.getString("name", nm, sizeof(nm));
    type = p.getUChar("type", 0);
    p.end();
  }
  portENTER_CRITICAL(&lock);
  copyText(wantAddr, sizeof(wantAddr), a);
  copyText(wantName, sizeof(wantName), nm);
  wantType = type;
  portEXIT_CRITICAL(&lock);
}

// Which service of the adapter carries the ELM327 text, and its write and
// notify characteristics, found by their properties. FFF0 (Veepeak: FFF1/FFF2,
// either way round) and FFE0 (HM-10 style: FFE1 both ways) first.
static bool findCharacteristics() {
  writeChr = nullptr;
  NimBLERemoteCharacteristic* notifyChr = nullptr;
  std::vector<NimBLERemoteService*>* svcs = client->getServices(true);
  if (!svcs) return false;
  std::vector<NimBLERemoteService*> order;
  for (auto* s : *svcs)
    if (s->getUUID() == NimBLEUUID((uint16_t)0xFFF0) || s->getUUID() == NimBLEUUID((uint16_t)0xFFE0)) order.push_back(s);
  for (auto* s : *svcs)
    if (std::find(order.begin(), order.end(), s) == order.end()) order.push_back(s);
  for (auto* s : order) {
    const NimBLEUUID u = s->getUUID();
    if (u == NimBLEUUID((uint16_t)0x1800) || u == NimBLEUUID((uint16_t)0x1801) || u == NimBLEUUID((uint16_t)0x180A)) continue;
    std::vector<NimBLERemoteCharacteristic*>* chrs = s->getCharacteristics(true);
    if (!chrs) continue;
    NimBLERemoteCharacteristic *w = nullptr, *n = nullptr;
    for (auto* c : *chrs) {
      if (!n && (c->canNotify() || c->canIndicate())) n = c;
      if (!w && (c->canWriteNoResponse() || c->canWrite())) w = c;
    }
    if (w && n) {
      writeChr = w;
      notifyChr = n;
      break;
    }
  }
  if (!writeChr || !notifyChr) return false;
  writeWithResponse = !writeChr->canWriteNoResponse();
  if (!notifyChr->subscribe(notifyChr->canNotify(), notified, true)) return false;
  DEBUG_PRINTF("OBD: adapter's service %s, write %s, notify %s\n", notifyChr->getRemoteService()->getUUID().toString().c_str(),
               writeChr->getUUID().toString().c_str(), notifyChr->getUUID().toString().c_str());
  return true;
}

static bool connectTo(const char* addr, uint8_t type) {
  if (!client) {
    client = NimBLEDevice::createClient();
    if (!client) return false;
    client->setConnectTimeout(CONNECT_TIMEOUT_S);
  }
  if (!RaceBoxBle::radioTryLock(100)) return false;  // the camera is connecting or scanning: next time
  const bool ok = client->connect(NimBLEAddress(std::string(addr), type), true) && findCharacteristics();
  RaceBoxBle::radioUnlock();
  if (!ok) {
    if (client->isConnected()) {
      DEBUG_PRINTLN("OBD: no ELM327 service on that device");
      client->disconnect();
    }
    return false;
  }
  xStreamBufferReset(rx);
  lastRxAt = 0;  // not connected for display until it has answered
  elmc.begin(millis());
  RaceBoxBle::resumeAdvertising();  // the phone can still find the RaceBox output
  DEBUG_PRINTF("OBD: connected to %s\n", addr);
  return true;
}

static void scanDone(NimBLEScanResults res) {
  Found list[MAX_FOUND];
  int n = 0;
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    const std::string name = d.getName();
    const bool svc = d.isAdvertisingService(NimBLEUUID((uint16_t)0xFFF0)) || d.isAdvertisingService(NimBLEUUID((uint16_t)0xFFE0));
    if (!elm::looksLikeAdapter(name.c_str(), svc)) continue;
    Found f;
    copyText(f.name, sizeof(f.name), name.empty() ? "(no name)" : name.c_str());
    copyText(f.address, sizeof(f.address), d.getAddress().toString().c_str());
    f.addressType = d.getAddress().getType();
    f.rssi = d.getRSSI();
    // strongest first: insert in order, dropping the weakest when full
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
  DEBUG_PRINTF("OBD: scan found %d adapter(s)\n", n);
}

static void publish() {
  Status s;
  portENTER_CRITICAL(&lock);
  copyText(s.name, sizeof(s.name), wantName);
  copyText(s.address, sizeof(s.address), wantAddr);
  portEXIT_CRITICAL(&lock);
  s.configured = s.address[0] != 0;
  s.linked = client && client->isConnected() && writeChr;
  s.lastRxAt = s.linked ? lastRxAt : 0;
  s.phase = elmc.state();
  copyText(s.protocol, sizeof(s.protocol), elmc.protocol());
  s.stats = elmc.statistics();
  s.suffix = elmc.usesSuffix();
  s.multi = elmc.usesMulti();
  portENTER_CRITICAL(&lock);
  shared = s;
  sharedVals = elmc.values();
  portEXIT_CRITICAL(&lock);
}

static void taskMain(void*) {
  uint32_t retryAt = 0, retryMs = RETRY_MIN_MS;
  for (;;) {
    const uint32_t now = millis();

    if (settingsChanged) {  // a new adapter, or none
      settingsChanged = false;
      if (client && client->isConnected()) client->disconnect();
      elmc.end();
      writeChr = nullptr;
      wasConnected = false;
      retryAt = now;
      retryMs = RETRY_MIN_MS;
    }

    if (scanAsked && !scanOn) {  // (never while connecting: this task does one thing at a time)
      scanOn = true;  // (before clearing the ask, so scanning() never blinks false)
      scanAsked = false;
      if (!RaceBoxBle::radioTryLock(3000)) {  // the camera is connecting or scanning: found nothing
        scanOn = false;
      } else {
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setActiveScan(true);  // names are often in the scan response
        scan->setInterval(100);
        scan->setWindow(60);        // leaves the radio time for the phone and the adapter
        if (!scan->start(OBD_SCAN_S, scanDone, false)) {
          scanOn = false;
          RaceBoxBle::radioUnlock();
        }
      }
    }

    char addr[18];
    uint8_t type;
    portENTER_CRITICAL(&lock);
    copyText(addr, sizeof(addr), wantAddr);
    type = wantType;
    portEXIT_CRITICAL(&lock);

    const bool linked = client && client->isConnected() && writeChr;
    if (wasConnected && !linked) {
      DEBUG_PRINTLN("OBD: adapter disconnected");
      elmc.end();
      writeChr = nullptr;
      retryAt = now + RETRY_MIN_MS;
      retryMs = RETRY_MIN_MS;
    }
    wasConnected = linked;

    if (!linked) {
      if (addr[0] && !scanOn && (int32_t)(now - retryAt) >= 0) {
        if (connectTo(addr, type)) {
          wasConnected = true;
          retryMs = RETRY_MIN_MS;
        } else {
          if (client && client->isConnected()) client->disconnect();
          retryAt = millis() + retryMs;
          retryMs = retryMs + 2500 > RETRY_MAX_MS ? RETRY_MAX_MS : retryMs + 2500;  // 5, 7.5, then every 10 s
        }
      }
    } else {
      char in[64];
      size_t n;
      while ((n = xStreamBufferReceive(rx, in, sizeof(in), 0)) > 0) {
        lastRxAt = millis();  // it is talking
        elmc.feed(in, (int)n, lastRxAt);
      }
      char cmd[24];
      if (elmc.poll(millis(), cmd, sizeof(cmd))) {
        if (!writeChr->writeValue((const uint8_t*)cmd, strlen(cmd), writeWithResponse)) DEBUG_PRINTLN("OBD: write failed");
      }
    }

    publish();
    vTaskDelay(pdMS_TO_TICKS(linked ? 2 : 50));
  }
}

static void startTask() {
  if (task) return;
  RaceBoxBle::initRadio();  // (on loop()'s core, like the RaceBox start)
  rx = xStreamBufferCreate(512, 1);
  if (!rx || xTaskCreatePinnedToCore(taskMain, "obd", 8192, nullptr, 1, &task, 0) != pdPASS) {
    DEBUG_PRINTLN("OBD: task didn't start");
    task = nullptr;
  }
}

void begin() {
  loadSaved();
  if (wantAddr[0]) {
    DEBUG_PRINTF("OBD: adapter %s (%s)\n", wantName, wantAddr);
    startTask();
  }
}

void poll() {
  portENTER_CRITICAL(&lock);
  st = shared;
  vals = sharedVals;
  portEXIT_CRITICAL(&lock);
  // Judged here, at loop()'s time: a task stuck in a Bluetooth call can't leave a stale "connected" behind
  st.connected = devicelink::obdAlive(st.linked, st.lastRxAt, millis());
}

bool configured() { return st.configured; }
bool connected() { return st.connected; }
const obd::Values& values() { return vals; }
Status status() { return st; }

void startScan() {
  startTask();
  if (!task) return;
  portENTER_CRITICAL(&lock);
  foundCount = 0;
  portEXIT_CRITICAL(&lock);
  scanAsked = true;  // shows as scanning at once; the task starts it
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
  if (p.begin("obd", false)) {
    p.putString("addr", f.address);
    p.putString("name", f.name);
    p.putUChar("type", f.addressType);
    p.end();
  }
  portENTER_CRITICAL(&lock);
  copyText(wantAddr, sizeof(wantAddr), f.address);
  copyText(wantName, sizeof(wantName), f.name);
  wantType = f.addressType;
  portEXIT_CRITICAL(&lock);
  settingsChanged = true;
  startTask();
  DEBUG_PRINTF("OBD: chose %s (%s)\n", f.name, f.address);
}

void forget() {
  Preferences p;
  if (p.begin("obd", false)) {
    p.clear();
    p.end();
  }
  portENTER_CRITICAL(&lock);
  wantAddr[0] = wantName[0] = 0;
  portEXIT_CRITICAL(&lock);
  settingsChanged = true;
  DEBUG_PRINTLN("OBD: adapter forgotten");
}

}  // namespace ObdBle

#else  // no Bluetooth adapter support

namespace ObdBle {
static obd::Values none;
void begin() {}
void poll() {}
bool configured() { return false; }
bool connected() { return false; }
const obd::Values& values() { return none; }
Status status() { return Status(); }
void startScan() {}
bool scanning() { return false; }
int found(Found*, int) { return 0; }
void choose(const Found&) {}
void forget() {}
}  // namespace ObdBle

#endif
