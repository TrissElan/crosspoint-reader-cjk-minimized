#include "HomeActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <vector>

#include "settings/CrossPointSettings.h"
#include "state/CrossPointState.h"
#include "state/MappedInputManager.h"
#include "state/RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File Transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  selectorIndex = 0;

  loadRecentBooks(3);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Calculate dynamic indices based on which options are available
    int idx = 0;
    int menuSelectedIndex = selectorIndex - static_cast<int>(recentBooks.size());
    const int fileBrowserIdx = idx++;
    const int recentsIdx = idx++;
    const int fileTransferIdx = idx++;
    const int settingsIdx = idx;

    if (selectorIndex < recentBooks.size()) {
      activityManager.goToReader(recentBooks[selectorIndex].path);
    } else if (menuSelectedIndex == fileBrowserIdx) {
      onFileBrowserOpen();
    } else if (menuSelectedIndex == recentsIdx) {
      onRecentsOpen();
    } else if (menuSelectedIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (menuSelectedIndex == settingsIdx) {
      onSettingsOpen();
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  auto drawContent = [&]() {
    renderer.clearScreen();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

    GUI.drawRecentBooks(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                        recentBooks, selectorIndex);

    // Build menu items dynamically
    std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                           tr(STR_SETTINGS_TITLE)};
    std::vector<UIIcon> menuIcons = {Folder, Recent, Wifi, Settings};

    GUI.drawButtonMenu(
        renderer,
        Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
             pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing * 2 +
                           metrics.buttonHintsHeight)},
        static_cast<int>(menuItems.size()), selectorIndex - recentBooks.size(),
        [&menuItems](int index) { return std::string(menuItems[index]); },
        [&menuIcons](int index) { return menuIcons[index]; });

    const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  };
  drawContent();
  renderer.displayBufferWithAA(drawContent);
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }
