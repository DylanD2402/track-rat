/**
 * Track Rat - LED delta bar implementation
 */

#include "LedBar.h"
#include "AddOns.h"
#include "Config.h"
#include "DeltaLeds.h"

#if LED_BAR_ENABLED || BOARD_LED_ENABLED

#include "driver/rmt.h"

namespace {

const uint32_t LED_UPDATE_MS = 25;     // 40 frames a second, twice the GPS rate
const uint32_t LED_REFRESH_MS = 500;   // resend an unchanged picture now and then

// One WS2812 output on its own RMT channel. 40 MHz RMT clock = 25 ns per tick.
struct Strip {
  rmt_channel_t channel;
  int pin = -1;
  int count = 0;
  uint8_t* grb = nullptr;  // must stay valid while the RMT interrupt reads it
  bool ready = false;

  bool begin(rmt_channel_t ch, int gpioPin, int leds) {
    channel = ch;
    pin = gpioPin;
    count = leds;
    grb = (uint8_t*)calloc(leds * 3, 1);
    if (!grb) return false;
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)gpioPin, ch);
    cfg.clk_div = 2;
    if (rmt_config(&cfg) != ESP_OK) return false;
    if (rmt_driver_install(ch, 0, 0) != ESP_OK) return false;
    if (rmt_translator_init(ch, translate) != ESP_OK) return false;
    ready = true;
    return true;
  }

  // Send the current buffer. Skips the frame if the last one is still going out.
  void show() {
    if (!ready) return;
    if (rmt_wait_tx_done(channel, 0) != ESP_OK) return;
    rmt_write_sample(channel, grb, count * 3, false);
  }

  void set(int i, uint8_t r, uint8_t g, uint8_t b) {
    grb[i * 3] = g;  // WS2812 wants green first
    grb[i * 3 + 1] = r;
    grb[i * 3 + 2] = b;
  }

  // Bytes -> RMT pulses: WS2812B timing of 0.35/0.9 us (a 0) and 0.9/0.35 us (a 1).
  static void IRAM_ATTR translate(const void* src, rmt_item32_t* dest, size_t srcSize, size_t wantedItems,
                                  size_t* translatedSize, size_t* itemsOut) {
    static const rmt_item32_t bit0 = {{{14, 1, 36, 0}}};
    static const rmt_item32_t bit1 = {{{36, 1, 14, 0}}};
    if (!src || !dest) {
      *translatedSize = 0;
      *itemsOut = 0;
      return;
    }
    size_t size = 0, num = 0;
    const uint8_t* p = (const uint8_t*)src;
    rmt_item32_t* item = dest;
    while (size < srcSize && num < wantedItems) {
      for (int bit = 7; bit >= 0; bit--) {
        *item++ = (*p & (1 << bit)) ? bit1 : bit0;
        num++;
      }
      size++;
      p++;
    }
    *translatedSize = size;
    *itemsOut = num;
  }
};

DeltaLedPattern pattern;
Strip bar, board;
Rgb frame[LED_MAX_COUNT];
Rgb lastFrame[LED_MAX_COUNT];
Rgb lastBoard = {0, 0, 0};
uint32_t lastUpdate = 0, lastSent = 0;
bool started = false;

uint8_t scale(uint8_t v, uint8_t brightness) { return (uint8_t)(((uint16_t)v * brightness + 127) / 255); }

}  // namespace

void LedBar::begin(int gpsRxPin, int gpsTxPin) {
  bool barOn = false, boardOn = false;
#if LED_BAR_ENABLED
  if (addOnPinTaken(LED_BAR_PIN, gpsRxPin, gpsTxPin)) {
    DEBUG_PRINTF("LedBar: LED bar disabled - the GPS is on IO%d\n", LED_BAR_PIN);
  } else {
    pattern.begin(LED_BAR_COUNT, LED_BAR_FULL_SCALE_MS, LED_BAR_REVERSED);
    barOn = bar.begin(RMT_CHANNEL_2, LED_BAR_PIN, pattern.ledCount());
    DEBUG_PRINTF("LedBar: %d LEDs on IO%d %s\n", pattern.ledCount(), LED_BAR_PIN, barOn ? "ready" : "FAILED to start");
  }
#endif
#if BOARD_LED_ENABLED
  if (addOnPinTaken(BOARD_LED_PIN, gpsRxPin, gpsTxPin)) {
    DEBUG_PRINTF("LedBar: board LED disabled - the GPS is on IO%d\n", BOARD_LED_PIN);
  } else {
    boardOn = board.begin(RMT_CHANNEL_3, BOARD_LED_PIN, 1);
    DEBUG_PRINTF("LedBar: board LED on IO%d %s\n", BOARD_LED_PIN, boardOn ? "ready" : "FAILED to start");
  }
#endif
  if (!barOn && !boardOn) return;
  if (!barOn) pattern.begin(2);  // pattern still needs a size to run
  started = true;
  bar.show();  // all off
  board.show();
}

void LedBar::update(bool timing, bool hasDelta, int32_t deltaMs) {
  if (!started) return;
  uint32_t now = millis();
  if (now - lastUpdate < LED_UPDATE_MS) return;
  lastUpdate = now;

  pattern.setDelta(timing, hasDelta, deltaMs);
  Rgb boardColour;
  pattern.render(now, frame, boardColour);

  int n = pattern.ledCount();
  bool changed = boardColour != lastBoard;
  for (int i = 0; i < n && !changed; i++) changed = frame[i] != lastFrame[i];
  if (!changed && now - lastSent < LED_REFRESH_MS) return;
  lastSent = now;
  lastBoard = boardColour;
  for (int i = 0; i < n; i++) lastFrame[i] = frame[i];

  if (bar.ready) {
    for (int i = 0; i < n; i++)
      bar.set(i, scale(frame[i].r, LED_BAR_BRIGHTNESS), scale(frame[i].g, LED_BAR_BRIGHTNESS),
              scale(frame[i].b, LED_BAR_BRIGHTNESS));
    bar.show();
  }
  if (board.ready) {
    board.set(0, scale(boardColour.r, BOARD_LED_BRIGHTNESS), scale(boardColour.g, BOARD_LED_BRIGHTNESS),
              scale(boardColour.b, BOARD_LED_BRIGHTNESS));
    board.show();
  }
}

void LedBar::onBestLap() {
  if (started) pattern.bestLap(millis());
}

void LedBar::onSplit() {
  if (started) pattern.split(millis());
}

#else  // both outputs compiled out

void LedBar::begin(int, int) {}
void LedBar::update(bool, bool, int32_t) {}
void LedBar::onBestLap() {}
void LedBar::onSplit() {}

#endif
