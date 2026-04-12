#include "CrossPointSettings.h"

#include <HalStorage.h>
#include "JsonSettingsIO.h"
#include <Logging.h>

#include <cstring>

#include "fontIds.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

namespace {
constexpr char SETTINGS_FILE_JSON[] = "/.crosspoint/settings.json";
}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

bool CrossPointSettings::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveSettings(*this, SETTINGS_FILE_JSON);
}

bool CrossPointSettings::loadFromFile() {
  if (Storage.exists(SETTINGS_FILE_JSON)) {
    String json = Storage.readFile(SETTINGS_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadSettings(*this, json.c_str(), &resave);
      if (result && resave) {
        if (saveToFile()) {
          LOG_DBG("CPS", "Resaved settings to update format");
        } else {
          LOG_ERR("CPS", "Failed to resave settings after format update");
        }
      }
      return result;
    }
  }

  return false;
}

float CrossPointSettings::getReaderLineCompression() const {
  switch (lineSpacing) {
    case TIGHT:
      return 0.95f;
    case NORMAL:
    default:
      return 1.0f;
    case WIDE:
      return 1.1f;
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  switch (sleepTimeout) {
    case SLEEP_1_MIN:
      return 1UL * 60 * 1000;
    case SLEEP_5_MIN:
      return 5UL * 60 * 1000;
    case SLEEP_10_MIN:
    default:
      return 10UL * 60 * 1000;
    case SLEEP_15_MIN:
      return 15UL * 60 * 1000;
    case SLEEP_30_MIN:
      return 30UL * 60 * 1000;
  }
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
}

int CrossPointSettings::getReaderFontId() const {
  if (strcmp(customFontPath, "builtin:10") == 0) return PRETENDARD_10_FONT_ID;
  if (strcmp(customFontPath, "builtin:14") == 0) return PRETENDARD_14_FONT_ID;
  if (strcmp(customFontPath, "builtin:kopub10") == 0) return KOPUB_10_FONT_ID;
  if (strcmp(customFontPath, "builtin:kopub12") == 0) return KOPUB_12_FONT_ID;
  if (strcmp(customFontPath, "builtin:kopub14") == 0) return KOPUB_14_FONT_ID;
  return PRETENDARD_12_FONT_ID;
}
