/**
 * Track Rat - Bluetooth LE GPS output implementation
 */

#include "RaceBoxBle.h"
#include "Config.h"
#include "Imu.h"
#include "RaceBoxPacket.h"
#include "RaceChronoBle.h"
#include "Settings.h"
#include <NimBLEDevice.h>

namespace RaceBoxBle {

// Nordic UART service, as used by the RaceBox Mini
static const char* SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // phone -> device
static const char* TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // device -> phone

static NimBLECharacteristic* tx = nullptr;
static volatile bool isConnected = false;
static String name;
static uint32_t sent = 0;
static bool started = false;  // begin() has run
static bool enabled = false;  // the Bluetooth setting is on

// A read-only text characteristic. As a std::string: NimBLE's setValue(const T&)
// would send a string literal with its terminating zero, and a char pointer
// as the pointer's own bytes.
static void addText(NimBLEService* svc, const char* uuid, const std::string& text) {
  svc->createCharacteristic(uuid, NIMBLE_PROPERTY::READ)->setValue(text);
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
    isConnected = true;
    // 88-byte packets need a bigger MTU than the default 23; ask for faster intervals too.
    server->updateConnParams(desc->conn_handle, 12, 24, 0, 400);  // 15-30 ms, 4 s timeout
    DEBUG_PRINTLN("BLE: phone connected");
  }
  void onDisconnect(NimBLEServer* server) override {
    isConnected = server->getConnectedCount() > 0;
    if (!isConnected) RaceChronoBle::onDisconnect();
    DEBUG_PRINTLN("BLE: phone disconnected");
  }
  void onMTUChange(uint16_t mtu, ble_gap_conn_desc*) override { DEBUG_PRINTF("BLE: MTU %u\n", mtu); }
};

// The Bluetooth profile, fixed at start-up (Settings > Bluetooth profile): RaceBox
// Mini, or RaceChrono's DIY device (RaceChronoBle.h). One at a time.
static bool raceChrono = false;

// Starts the Bluetooth stack under the profile's name, once. The OBD adapter's and
// the camera's central roles share it, so whichever starts first calls this.
void initRadio() {
  static bool radioOn = false;
  if (radioOn) return;
  radioOn = true;
  raceChrono = Settings::get().bleProfile == BleProfile::RaceChrono;
  // Stable per-board number from the chip ID; the RaceBox app only accepts
  // numbers below 4000000000.
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  if (raceChrono) snprintf(buf, sizeof(buf), "Track Rat %04u", (unsigned)(mac % 10000));
  else snprintf(buf, sizeof(buf), "RaceBox Mini %010lu", (unsigned long)(mac % 4000000000ULL));
  name = buf;

  NimBLEDevice::init(name.c_str());
  NimBLEDevice::setMTU(185);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
}

void begin() {
  initRadio();

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  server->advertiseOnDisconnect(true);
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();

  if (raceChrono) {
    RaceChronoBle::build(server);  // and nothing else: iOS drops DIY data beside a UART-style service
    adv->setScanResponse(true);
    adv->start();
    DEBUG_PRINTF("BLE: advertising as \"%s\" (RaceChrono DIY device)\n", name.c_str());
    started = true;
    enabled = true;
    return;
  }

  NimBLEService* svc = server->createService(SERVICE_UUID);
  tx = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  svc->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);  // commands ignored
  svc->start();

  // Device Information service, which RaceBox apps read when connecting
  NimBLEService* info = server->createService("180A");
  addText(info, "2A24", "RaceBox Mini");              // model
  addText(info, "2A25", name.substring(13).c_str());  // serial: the 10 digits
  addText(info, "2A26", "3.3");                       // firmware
  addText(info, "2A27", "1");                         // hardware
  addText(info, "2A29", "RaceBox");                   // manufacturer
  info->start();

  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  DEBUG_PRINTF("BLE: advertising as \"%s\"\n", name.c_str());
  started = true;
  enabled = true;
}

// Off disconnects the phone and stops advertising; on advertises again.
// NimBLE is never shut down: restarting it isn't reliable on this core.
void setEnabled(bool on) {
  if (on == enabled) return;
  if (on && !started) {
    begin();
    return;
  }
  if (!started) return;
  enabled = on;
  NimBLEServer* server = NimBLEDevice::getServer();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  server->advertiseOnDisconnect(on);
  if (on) {
    adv->start();
  } else {
    adv->stop();
    for (uint16_t handle : server->getPeerDevices()) server->disconnect(handle);
  }
  DEBUG_PRINTF("BLE: %s\n", on ? "on" : "off");
}

void sendFix(const GpsFix& fix, const GPSHandler& gps) {
  if (enabled && raceChrono) {
    RaceChronoBle::sendFix(fix, gps.hasFix(), isConnected);
    return;
  }
  if (!enabled || !tx || !isConnected) return;

  RaceBoxFix f = {};
  f.dateValid = fix.year != 0;
  f.timeValid = true;
  f.year = fix.year;
  f.month = fix.month;
  f.day = fix.day;
  f.hour = fix.gpsTimeMs / 3600000;
  f.minute = (fix.gpsTimeMs / 60000) % 60;
  f.second = (fix.gpsTimeMs / 1000) % 60;
  f.millis = fix.gpsTimeMs % 1000;
  f.hasFix = gps.hasFix();
  f.satellites = fix.sats;
  f.lat = fix.lat;
  f.lon = fix.lon;
  f.altMslM = fix.altM;
  f.speedMs = fix.speedKmh / 3.6f;
  f.headingDeg = fix.courseDeg;
  f.headingValid = fix.courseValid;
  f.hdop = fix.pdop;
  f.accuracyKnown = fix.accuracyKnown();  // u-blox: the GPS's own figures
  f.fixType = fix.fixType;
  f.hAccM = fix.hAccM;
  f.vAccM = fix.vAccM;
  f.speedAccMs = fix.speedAccMs;
  f.headingAccDeg = fix.headingAccDeg;
  imu::CarG g;
  imu::CarRates r;
  if (Imu::carG(g, r)) {  // the IMU, in the car's frame: x forward, y right, z up (untested with the apps)
    auto mg = [](float v) { return (int16_t)constrain(lroundf(v * 1000.0f), -32768L, 32767L); };
    auto cdps = [](float v) { return (int16_t)constrain(lroundf(v * 100.0f), -32768L, 32767L); };
    f.gX = mg(g.lon);
    f.gY = mg(g.lat);
    f.gZ = mg(g.vert);
    f.rX = cdps(r.roll);
    f.rY = cdps(r.pitch);
    f.rZ = cdps(r.yaw);
  } else {
    f.gZ = 1000;  // no IMU (or not learned yet): report a device at rest
  }
  f.batteryPercent = 100;

  uint8_t pkt[RACEBOX_PACKET_LEN];
  buildRaceBoxPacket(f, pkt);
  tx->setValue(pkt, sizeof(pkt));
  tx->notify();
  if (++sent % 600 == 1) DEBUG_PRINTF("BLE: streaming (%lu packets sent)\n", (unsigned long)sent);
}

bool connected() { return isConnected; }

static SemaphoreHandle_t radioBusy = nullptr;  // binary, not a mutex: given back from any task

bool radioTryLock(uint32_t waitMs) {
  static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&m);
  if (!radioBusy) {
    radioBusy = xSemaphoreCreateBinary();
    if (radioBusy) xSemaphoreGive(radioBusy);
  }
  portEXIT_CRITICAL(&m);
  return radioBusy && xSemaphoreTake(radioBusy, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void radioUnlock() {
  if (radioBusy) xSemaphoreGive(radioBusy);
}

// After the OBD adapter connects: advertising carries on if the phone isn't connected
void resumeAdvertising() {
  if (!started || !enabled || isConnected) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (!adv->isAdvertising()) adv->start();
}

}  // namespace RaceBoxBle
