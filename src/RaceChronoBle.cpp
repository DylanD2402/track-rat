/**
 * Track Rat - the RaceChrono Bluetooth profile implementation (see RaceChronoBle.h).
 * Untested with RaceChrono.
 */

#include "RaceChronoBle.h"
#include "Config.h"
#include "EngineData.h"
#include "Imu.h"
#include "RaceChrono.h"

namespace RaceChronoBle {

static NimBLECharacteristic *canMain = nullptr, *gpsMain = nullptr, *gpsTime = nullptr;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;  // the filter: written from NimBLE's task
static rc::Filter filter;
static rc::Sync sync;
static uint32_t gpsSent = 0, canSent = 0;

// What the app writes to 0x0002: which packets it wants, how often
class FilterCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    const std::string v = c->getValue();
    portENTER_CRITICAL(&lock);
    const bool ok = filter.apply((const uint8_t*)v.data(), (int)v.size());
    portEXIT_CRITICAL(&lock);
    DEBUG_PRINTF("RaceChrono: CAN filter command %d (%s)\n", v.empty() ? -1 : v[0], ok ? "ok" : "not understood");
  }
};

void build(NimBLEServer* server) {
  NimBLEService* svc = server->createService("1FF8");
  canMain = svc->createCharacteristic((uint16_t)0x0001, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  // Must be there, or RaceChrono disconnects
  svc->createCharacteristic((uint16_t)0x0002, NIMBLE_PROPERTY::WRITE)->setCallbacks(new FilterCallbacks());
  gpsMain = svc->createCharacteristic((uint16_t)0x0003, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  gpsTime = svc->createCharacteristic((uint16_t)0x0004, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEDevice::getAdvertising()->addServiceUUID("1FF8");
}

void onDisconnect() {
  portENTER_CRITICAL(&lock);
  filter.reset();
  portEXIT_CRITICAL(&lock);
}

static bool due(uint32_t id, uint32_t now) {
  portENTER_CRITICAL(&lock);
  const bool d = filter.due(id, now);
  portEXIT_CRITICAL(&lock);
  return d;
}

static void sendValue(uint32_t id, float value, float perUnit, uint32_t now) {
  if (!due(id, now)) return;
  uint8_t pkt[20];
  const int n = rc::valuePacket(id, value, perUnit, pkt);
  canMain->setValue(pkt, n);
  canMain->notify();
  canSent++;
}

void sendFix(const GpsFix& fix, bool hasFix, bool connected) {
  if (!gpsMain) return;
  rc::Fix f;
  f.valid = hasFix;
  f.year = fix.year ? fix.year : 2000;
  f.month = fix.year ? fix.month : 1;
  f.day = fix.year ? fix.day : 1;
  f.utcMs = fix.gpsTimeMs;
  f.sats = fix.sats;
  f.lat = fix.lat, f.lon = fix.lon;
  f.altM = fix.altM;
  f.speedKmh = fix.speedKmh;
  f.courseDeg = fix.courseDeg;
  f.courseValid = fix.courseValid;
  f.hdop = fix.pdop;  // u-blox gives PDOP; RaceChrono only shows it
  const uint8_t s = sync.update(rc::dateHour(f.year, f.month, f.day, (uint8_t)(f.utcMs / 3600000)));
  uint8_t main[20], time[3];
  rc::gpsMain(f, s, main);
  rc::gpsTime(f, s, time);
  gpsTime->setValue(time, 3);  // the app reads it when the sync bits change
  gpsMain->setValue(main, 20);
  if (!connected) return;
  gpsTime->notify();
  gpsMain->notify();
  if (++gpsSent % 1200 == 1) DEBUG_PRINTF("RaceChrono: streaming (%lu fixes, %lu engine packets)\n", (unsigned long)gpsSent, (unsigned long)canSent);

  // Engine values and g, when the app has asked for them
  const uint32_t now = millis();
  const obd::Values& e = EngineData::values();
  if (e.rpmKnown(now)) sendValue(rc::ID_RPM, e.rpm, 1, now);
  if (e.throttleKnown(now)) sendValue(rc::ID_THROTTLE, e.throttlePct, 10, now);
  if (e.coolantKnown(now)) sendValue(rc::ID_COOLANT, e.coolantC, 10, now);
  if (e.intakeKnown(now)) sendValue(rc::ID_INTAKE, e.intakeC, 10, now);
  imu::CarG g;
  imu::CarRates r;
  if (Imu::carG(g, r)) {
    sendValue(rc::ID_LAT_G, g.lat, 1000, now);
    sendValue(rc::ID_LONG_G, g.lon, 1000, now);
  }
}

}  // namespace RaceChronoBle
