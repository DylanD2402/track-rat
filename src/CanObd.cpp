/**
 * Track Rat - OBD-II over CAN implementation (see CanObd.h). Untested on hardware.
 */

#include "CanObd.h"
#include "AddOns.h"
#include "Config.h"
#include <Arduino.h>

#if CAN_ENABLED
#include "driver/twai.h"
#endif

namespace CanObd {

static obd::Values vals;

#if CAN_ENABLED

static bool on = false;
static obd::Poller poller;
static uint32_t lastHealth = 0;
static bool saidQuiet = false, saidAnswer = false;

void begin(int gpsRxPin, int gpsTxPin) {
  if (addOnPinTaken(CAN_TX_PIN, gpsRxPin, gpsTxPin) || addOnPinTaken(CAN_RX_PIN, gpsRxPin, gpsTxPin)) {
    DEBUG_PRINTF("CAN: off - the GPS is on IO%d/IO%d\n", gpsRxPin, gpsTxPin);
    return;
  }
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  g.rx_queue_len = 16;
  g.tx_queue_len = 4;
  twai_timing_config_t t = CAN_BITRATE_KBPS == 250 ? (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS()
                                                   : (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) {
    DEBUG_PRINTLN("CAN: driver didn't start");
    return;
  }
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    DEBUG_PRINTLN("CAN: controller didn't start");
    return;
  }
  on = true;
  DEBUG_PRINTF("CAN: OBD-II at %d kbit/s on IO%d (TX) / IO%d (RX)\n", CAN_BITRATE_KBPS, CAN_TX_PIN, CAN_RX_PIN);
}

bool active() { return on; }

void poll() {
  if (!on) return;
  const uint32_t now = millis();

  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) {
    if (m.extd || m.rtr) continue;
    const int pid = obd::parse(m.identifier, m.data, m.data_length_code, vals, now);
    if (pid < 0) continue;
    poller.replied(pid);
    if (!saidAnswer) {
      saidAnswer = true;
      saidQuiet = false;
      DEBUG_PRINTF("CAN: the engine answers (0x%03lX)\n", (unsigned long)m.identifier);
    }
  }

  const int pid = poller.due(now);
  if (pid >= 0) {
    twai_message_t q = {};
    q.identifier = obd::REQUEST_ID;
    q.data_length_code = 8;
    q.ss = 1;  // single shot: with nothing listening, don't keep resending
    obd::request((uint8_t)pid, q.data);
    twai_transmit(&q, 0);
    poller.sent(now);  // sent or not, the next one waits its turn
  }
  if (poller.quietCount() == 25 && !saidQuiet) {
    saidQuiet = true;
    saidAnswer = false;
    DEBUG_PRINTLN("CAN: no answer from the engine (ignition off, or another bit rate?)");
  }

  // With nothing on the bus the controller can go bus-off: start it again
  if (now - lastHealth >= 1000) {
    lastHealth = now;
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
      if (st.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
      else if (st.state == TWAI_STATE_STOPPED) twai_start();
    }
  }
}

#else  // CAN_ENABLED 0

void begin(int, int) {}
bool active() { return false; }
void poll() {}

#endif

const obd::Values& values() { return vals; }

}  // namespace CanObd
