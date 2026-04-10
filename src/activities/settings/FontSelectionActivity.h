#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"

/**
 * Activity for selecting the built-in font size.
 * Lists Pretendard 12/14/16pt options.
 */
class FontSelectionActivity final : public Activity {
 public:
  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FontSelection", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t displayMutex = nullptr;
  bool updateRequired = false;

  int selectedIndex = 0;
  std::vector<std::string> fontFiles;
  std::vector<std::string> fontNames;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void doRender();
  void loadFontList();
  void handleSelection();
};
