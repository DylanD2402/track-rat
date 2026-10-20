/**
 * Track Rat - the engine's temperatures on screen (6.5)
 *
 * The coolant tag in the top bar takes a colour from the limits in
 * Settings > Coolant: blue while the engine is cold, plain once warm enough,
 * yellow from "Warm at", blinking red from "Hot at". At "Hot at" (or oil at
 * "Oil hot at", for cars that report it) a full-screen card shows for
 * TEMP_ALERT_MS. It shows again only once the temperature has dropped
 * TEMP_REARM_DROP_C below the limit and come back, or after TEMP_REPEAT_MS
 * still over it. A temperature the engine hasn't sent in the last second
 * (obd::STALE_MS) counts as not known: no tag colour, no card.
 *
 * Temperatures are kept in degrees C; with Speed units in mph they are shown
 * in degrees F. Pure C++, tested on a PC in test/test_tempalert.cpp.
 */

#ifndef TEMP_ALERT_H
#define TEMP_ALERT_H

#include <math.h>
#include <stdint.h>
#include "Config.h"

namespace tempalert {

enum class Band : uint8_t { Cold, Normal, Warm, Hot };

struct Limits {
  int coldBelowC = COOLANT_COLD_C, warmAtC = COOLANT_WARM_C, hotAtC = COOLANT_WARN_C, oilHotAtC = OIL_HOT_C;
};

inline Band coolantBand(float c, const Limits& l) {
  if (c >= l.hotAtC) return Band::Hot;
  if (c >= l.warmAtC) return Band::Warm;
  if (c < l.coldBelowC) return Band::Cold;
  return Band::Normal;
}
// Oil has only the one limit
inline Band oilBand(float c, const Limits& l) { return c >= l.oilHotAtC ? Band::Hot : Band::Normal; }

// A temperature in whole degrees, F when fahrenheit (Speed units in mph)
inline int inUnits(float c, bool fahrenheit) { return (int)lroundf(fahrenheit ? c * 9.0f / 5.0f + 32.0f : c); }

// When the card shows for one temperature against its limit
class Watch {
public:
  // Every reading (known or not), so a drop below the limit re-arms it
  void update(bool known, float c, int limitC) {
    if (known && c <= limitC - TEMP_REARM_DROP_C) armed = true;
  }
  bool due(bool known, float c, int limitC, uint32_t nowMs) const {
    return known && c >= limitC && (armed || nowMs - shownAt >= TEMP_REPEAT_MS);
  }
  void shown(uint32_t nowMs) {
    armed = false;
    shownAt = nowMs;
  }

private:
  bool armed = true;
  uint32_t shownAt = 0;
};

// The full-screen card, for coolant and oil. Coolant first when both are over.
class Alert {
public:
  struct Card {
    bool oil = false;
    float c = 0;  // the temperature, kept up to date while it shows
  };

  // Call every loop with the latest temperatures. True when a card starts now.
  bool update(uint32_t nowMs, bool coolantKnown, float coolantC, bool oilKnown, float oilC, const Limits& l) {
    coolant.update(coolantKnown, coolantC, l.hotAtC);
    oil.update(oilKnown, oilC, l.oilHotAtC);
    if (showing(nowMs)) {
      if (card.oil ? oilKnown : coolantKnown) card.c = card.oil ? oilC : coolantC;
      return false;
    }
    active = false;
    if (coolant.due(coolantKnown, coolantC, l.hotAtC, nowMs)) return start(false, coolantC, nowMs, coolant);
    if (oil.due(oilKnown, oilC, l.oilHotAtC, nowMs)) return start(true, oilC, nowMs, oil);
    return false;
  }
  bool showing(uint32_t nowMs) const { return active && nowMs - startedAt < TEMP_ALERT_MS; }
  void dismiss() { active = false; }  // a tap
  const Card& current() const { return card; }
  // The LED bar flashes red while the card shows, on and off like the camera's REC tag
  bool ledRed(uint32_t nowMs) const { return showing(nowMs) && (nowMs - startedAt) / CAM_REC_BLINK_MS % 2 == 0; }

private:
  bool start(bool isOil, float c, uint32_t nowMs, Watch& w) {
    w.shown(nowMs);
    card.oil = isOil;
    card.c = c;
    active = true;
    startedAt = nowMs;
    return true;
  }
  Watch coolant, oil;
  Card card;
  bool active = false;
  uint32_t startedAt = 0;
};

}  // namespace tempalert

#endif  // TEMP_ALERT_H
