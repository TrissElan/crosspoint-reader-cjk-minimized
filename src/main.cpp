#include <Arduino.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <EmbeddedFont.h>
#include <EmbeddedFontFamily.h>
#include <builtinFonts/all.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);

// Pretendard 10/12/14pt + KoPub Dotum 10/12/14pt (2-bit) embedded in Flash via .incbin assembly.
// Zero-copy: intervals read directly from Flash, no RAM allocation for font data.
extern const uint8_t _binary_pretendard_10_epdfont_start[];
extern const uint8_t _binary_pretendard_10_epdfont_end[];
extern const uint8_t _binary_pretendard_12_epdfont_start[];
extern const uint8_t _binary_pretendard_12_epdfont_end[];
extern const uint8_t _binary_pretendard_14_epdfont_start[];
extern const uint8_t _binary_pretendard_14_epdfont_end[];

extern const uint8_t _binary_kopub_10_epdfont_start[];
extern const uint8_t _binary_kopub_10_epdfont_end[];
extern const uint8_t _binary_kopub_12_epdfont_start[];
extern const uint8_t _binary_kopub_12_epdfont_end[];
extern const uint8_t _binary_kopub_14_epdfont_start[];
extern const uint8_t _binary_kopub_14_epdfont_end[];

// Global EmbeddedFont / EmbeddedFontFamily for each embedded size.
EmbeddedFont* gCjkFont10 = nullptr;
EmbeddedFontFamily* gCjkFontFamily10 = nullptr;
EmbeddedFont* gCjkFont12 = nullptr;
EmbeddedFontFamily* gCjkFontFamily12 = nullptr;
EmbeddedFont* gCjkFont14 = nullptr;
EmbeddedFontFamily* gCjkFontFamily14 = nullptr;
EmbeddedFont* gKopubFont10 = nullptr;
EmbeddedFontFamily* gKopubFontFamily10 = nullptr;
EmbeddedFont* gKopubFont12 = nullptr;
EmbeddedFontFamily* gKopubFontFamily12 = nullptr;
EmbeddedFont* gKopubFont14 = nullptr;
EmbeddedFontFamily* gKopubFontFamily14 = nullptr;

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();
  APP_STATE.saveToFile();

  activityManager.goToSleep();

  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts() {
  display.begin();
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Load embedded fonts (2-bit) from Flash — zero-copy, no interval RAM allocation
  auto loadEmbeddedFont = [](const uint8_t* start, const uint8_t* end,
                              EmbeddedFont*& font, EmbeddedFontFamily*& family,
                              int fontId, const char* label) {
    size_t sz = end - start;
    font = new EmbeddedFont(start, sz);
    if (!font->load()) {
      LOG_ERR("MAIN", "Failed to load embedded %s", label);
      return;
    }
    family = new EmbeddedFontFamily(font);
    renderer.insertEmbeddedFont(fontId, family);
  };

  loadEmbeddedFont(_binary_pretendard_10_epdfont_start, _binary_pretendard_10_epdfont_end,
                   gCjkFont10, gCjkFontFamily10, PRETENDARD_10_FONT_ID, "Pretendard 10pt");
  loadEmbeddedFont(_binary_pretendard_12_epdfont_start, _binary_pretendard_12_epdfont_end,
                   gCjkFont12, gCjkFontFamily12, PRETENDARD_12_FONT_ID, "Pretendard 12pt");
  loadEmbeddedFont(_binary_pretendard_14_epdfont_start, _binary_pretendard_14_epdfont_end,
                   gCjkFont14, gCjkFontFamily14, PRETENDARD_14_FONT_ID, "Pretendard 14pt");

  loadEmbeddedFont(_binary_kopub_10_epdfont_start, _binary_kopub_10_epdfont_end,
                   gKopubFont10, gKopubFontFamily10, KOPUB_10_FONT_ID, "KoPub Dotum 10pt");
  loadEmbeddedFont(_binary_kopub_12_epdfont_start, _binary_kopub_12_epdfont_end,
                   gKopubFont12, gKopubFontFamily12, KOPUB_12_FONT_ID, "KoPub Dotum 12pt");
  loadEmbeddedFont(_binary_kopub_14_epdfont_start, _binary_kopub_14_epdfont_end,
                   gKopubFont14, gKopubFontFamily14, KOPUB_14_FONT_ID, "KoPub Dotum 14pt");

  LOG_DBG("MAIN", "Fonts setup complete");
}

void setup() {
  HalSystem::begin();
  gpio.begin();
  powerManager.begin();

#ifdef ENABLE_SERIAL_LOG
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    const unsigned long start = millis();
    while (!Serial && (millis() - start) < 500) {
      delay(10);
    }
  }
#endif

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts();
    activityManager.goToFullScreenMessage("SD card error");
    return;
  }

  HalSystem::checkPanic();

  SETTINGS.loadFromFile();
  I18N.loadSettings();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button press duration");
      gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                   SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  setupDisplayAndFonts();

  activityManager.goToBoot();

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path);
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
