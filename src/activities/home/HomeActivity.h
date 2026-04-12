#pragma once
#include <functional>
#include <vector>

#include "../Activity.h"
#include "./FileBrowserActivity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  std::vector<RecentBook> recentBooks;
  void onFileBrowserOpen();
  void onRecentsOpen();
  void onFileTransferOpen();
  void onSettingsOpen();

  int getMenuItemCount() const;
  void loadRecentBooks(int maxBooks);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
