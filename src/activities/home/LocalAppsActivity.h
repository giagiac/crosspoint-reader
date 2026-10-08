#pragma once

#include <HalStorage.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class LocalAppsActivity final : public UiListActivity {
  struct LocalApp {
    std::string name;
    std::string title;
    std::string dataPath;
    uint8_t pageSize = 6;
  };

  static constexpr size_t MAX_APPS = 16;
  static constexpr size_t MAX_MANIFEST_SIZE = 2048;
  static constexpr size_t MAX_LINE_BYTES = 160;
  static constexpr size_t READ_BUFFER_SIZE = 128;
  static constexpr size_t MAX_PAGE_SIZE = 8;

  std::vector<LocalApp> apps;
  std::array<std::array<char, MAX_LINE_BYTES + 1>, MAX_PAGE_SIZE> pageLines{};
  HalFile dataFile;
  uint32_t lineCount = 0;
  uint32_t pageCount = 0;
  uint32_t pageIndex = 0;
  uint8_t pageLineCount = 0;
  int selectedApp = -1;
  size_t readBufferPosition = 0;
  size_t readBufferLength = 0;
  uint8_t pageReadBuffer[READ_BUFFER_SIZE]{};
  char lineBuffer[MAX_LINE_BYTES + 1]{};
  char pageTitle[128]{};
  bool showingApp = false;
  bool invalidData = false;

  void scanApps();
  bool openApp(int index);
  bool countLines(uint32_t& count);
  bool readLine(size_t& length);
  int readNextByte();
  bool loadPage(uint32_t page);
  void updatePageTitle();
  static bool isSafeDataFilename(const char* filename);
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

 public:
  explicit LocalAppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
};
