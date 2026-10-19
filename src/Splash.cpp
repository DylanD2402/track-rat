/**
 * Track Rat - Start-up logo animation
 *
 * The logo fades in behind a soft, rippling edge that sweeps left to right,
 * holds, then fades out the same way. It runs on core 0 while setup() mounts
 * storage and finds the GPS on core 1, so it costs little or no boot time.
 *
 * Logo: include/logo_image.h if present (make it with tools/png_to_logo.py),
 * otherwise a placeholder drawn in code.
 */

#include "Config.h"
#include "DisplayManager.h"
#include "SplashSweep.h"

#if __has_include("logo_image.h")
#include "logo_image.h"  // LOGO_W, LOGO_H, LOGO_PIXELS (RGB565)
#define HAVE_LOGO_IMAGE 1
#else
#define HAVE_LOGO_IMAGE 0
#endif

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

static volatile bool splashDone = true;

// Draw the placeholder logo into a sprite.
static void drawPlaceholderLogo(TFT_eSprite& s) {
  const int w = s.width();
  s.fillSprite(TFT_BLACK);
  // Checkered flag strips
  const int sq = 10;
  for (int row = 0; row < 2; row++)
    for (int x = 0; x * sq < w; x++) {
      uint16_t c = ((x + row) & 1) ? TFT_WHITE : 0x2104;
      s.fillRect(x * sq, row * sq, sq, sq, c);
      s.fillRect(x * sq, s.height() - (2 - row) * sq, sq, sq, c);
    }
  s.setTextDatum(MC_DATUM);
  s.setTextColor(TFT_WHITE, TFT_BLACK);  // smooth fonts blend their edges with the background colour
  s.loadFont(DisplayManager::fontData(DisplayManager::Font::Hero));
  s.drawString("TRACK RAT", w / 2, s.height() / 2 - 8);
  s.loadFont(DisplayManager::fontData(DisplayManager::Font::Caption));
  s.setTextColor(0x9CD3, TFT_BLACK);  // grey
  s.drawString("GPS LAP TIMER", w / 2, s.height() / 2 + 24);
  s.unloadFont();
}

static bool bootPressed() { return digitalRead(BOOT_BUTTON_PIN) == LOW; }

static void splashTask(void* arg) {
  TFT_eSPI* tft = (TFT_eSPI*)arg;

  TFT_eSprite logo(tft), frame(tft);
  logo.setColorDepth(16);
  frame.setColorDepth(16);
#if HAVE_LOGO_IMAGE
  const int w = LOGO_W, h = LOGO_H;
#else
  const int w = 300, h = 110;
#endif
  if (!logo.createSprite(w, h) || !frame.createSprite(w, h)) {
    DEBUG_PRINTLN("Splash: not enough memory, skipped");
    splashDone = true;
    vTaskDelete(nullptr);
    return;
  }
#if HAVE_LOGO_IMAGE
  uint16_t* lp = (uint16_t*)logo.getPointer();
  for (int i = 0; i < w * h; i++) lp[i] = swap16(pgm_read_word(&LOGO_PIXELS[i]));
#else
  drawPlaceholderLogo(logo);
#endif

  const int x0 = (SCREEN_W - w) / 2, y0 = (SCREEN_H - h) / 2;
  const uint32_t FADE_MS = SPLASH_FADE_MS, HOLD_MS = SPLASH_HOLD_MS;
  const uint16_t* src = (const uint16_t*)logo.getPointer();
  uint16_t* dst = (uint16_t*)frame.getPointer();

  uint32_t start = millis();
  uint32_t frames = 0;
  bool skipped = false;
  for (;;) {
    uint32_t t = millis() - start;
    if (bootPressed()) skipped = true;
    if (skipped || t >= 2 * FADE_MS + HOLD_MS) break;

    float phase = t * 0.006f;  // ripple drifts while sweeping
    if (t < FADE_MS) {
      renderSweep(src, dst, w, h, (float)t / FADE_MS, true, phase);
    } else if (t < FADE_MS + HOLD_MS) {
      memcpy(dst, src, w * h * 2);
    } else {
      renderSweep(src, dst, w, h, (float)(t - FADE_MS - HOLD_MS) / FADE_MS, false, phase);
    }
    frame.pushSprite(x0, y0);
    frames++;
    vTaskDelay(1);  // let the BLE / system tasks on this core run
  }
  tft->fillRect(x0, y0, w, h, TFT_BLACK);
  uint32_t elapsed = millis() - start;
  DEBUG_PRINTF("Splash: %lu frames in %lu ms (%.0f fps)%s\n", (unsigned long)frames, (unsigned long)elapsed,
               elapsed ? frames * 1000.0f / elapsed : 0.0f, skipped ? ", skipped" : "");

  logo.deleteSprite();
  frame.deleteSprite();
  splashDone = true;
  vTaskDelete(nullptr);
}

void DisplayManager::startSplash() {
#if SPLASH_ENABLED
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  tft.fillScreen(TFT_BLACK);
  splashDone = false;
  if (xTaskCreatePinnedToCore(splashTask, "splash", 8192, &tft, 1, nullptr, 0) != pdPASS) splashDone = true;
#endif
}

void DisplayManager::finishSplash() {
  while (!splashDone) delay(10);
  while (bootPressed()) delay(10);  // a skip press shouldn't also act on the first screen
}

bool DisplayManager::splashRunning() const { return !splashDone; }
