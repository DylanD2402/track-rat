/**
 * Track Rat - Bluetooth LE GPS output implementation
 */

#include "RaceBoxBle.h"
#include "Config.h"
#include "RaceBoxPacket.h"
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

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
    isConnected = true;
    // 88-byte packets need a bigger MTU than the default 23; ask for faster intervals too.
    server->updateConnParams(desc->conn_handle, 12, 24, 0, 400);  // 15-30 ms, 4 s timeout
    DEBUG_PRINTLN("BLE: phone connected");
  }
  void onDisconnect(NimBLEServer* server) override {
    isConnected = server->getConnectedCount() > 0;
    DEBUG_PRINTLN("BLE: phone disconnected");
  }
  void onMTUChange(uint16_t mtu, ble_gap_conn_desc*) override { DEBUG_PRINTF("BLE: MTU %u\n", mtu); }
};

void begin() {
  // Stable per-board number from the chip ID; the RaceBox app only accepts
  // numbers below 4000000000.
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "RaceBox Mini %010lu", (unsigned long)(mac % 4000000000ULL));
  name = buf;

  NimBLEDevice::init(name.c_str());
  NimBLEDevice::setMTU(185);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  server->advertiseOnDisconnect(true);

  NimBLEService* svc = server->createService(SERVICE_UUID);
  tx = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  svc->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);  // commands ignored
  svc->start();

  // Device Information service, which RaceBox apps read when connecting
  NimBLEService* info = server->createService("180A");
  info->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)->setValue("RaceBox Mini");     // model
  info->createCharacteristic("2A25", NIMBLE_PROPERTY::READ)->setValue(name.substring(13).c_str());  // serial
  info->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue("3.3");              // firmware
  info->createCharacteristic("2A27", NIMBLE_PROPERTY::READ)->setValue("1");                // hardware
  info->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)->setValue("RaceBox");          // manufacturer
  info->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  DEBUG_PRINTF("BLE: advertising as \"%s\"\n", name.c_str());
}

void sendFix(const GpsFix& fix, const GPSHandler& gps) {
  if (!tx || !isConnected) return;

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
  f.satellites = gps.satellites();
  f.lat = fix.lat;
  f.lon = fix.lon;
  f.altMslM = fix.altM;
  f.speedMs = fix.speedKmh / 3.6f;
  f.headingDeg = fix.courseDeg;
  f.headingValid = fix.courseValid;
  f.hdop = gps.hdop();
  f.gZ = 1000;  // no IMU: report a device at rest
  f.batteryPercent = 100;

  uint8_t pkt[RACEBOX_PACKET_LEN];
  buildRaceBoxPacket(f, pkt);
  tx->setValue(pkt, sizeof(pkt));
  tx->notify();
  if (++sent % 600 == 1) DEBUG_PRINTF("BLE: streaming (%lu packets sent)\n", (unsigned long)sent);
}

bool connected() { return isConnected; }
const String& deviceName() { return name; }

}  // namespace RaceBoxBle
