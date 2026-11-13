#pragma once

#include <string>
#include <vector>

#include "activities/CatalogActivity.h"
#include "util/DriveFolder.h"

// Lists the files of a public Google Drive shared folder and saves a chosen
// file to the SD root. The folder id comes from DriveShareConfig and is set on
// the web settings page. See docs/drive-share.md.
class DriveShareActivity final : public CatalogActivity {
 public:
  explicit DriveShareActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 private:
  std::string rootFolderId;
  std::string currentFolderId;
  std::vector<std::string> folderHistory;  // parent folder ids, root first
  std::vector<DriveFolder::Entry> entries;
  std::vector<freeink::ui::ListItem> rowItems;  // row 0 is Refresh, then one row per entry
  char lastSyncLabel[40]{};                     // "Last sync: DD/MM HH:MM", empty without a clock

  void beginSession();
  void rebuildRowItems();
  void stampLastSync();
  void releaseEntries();
  void fetchListing();
  void confirmSave(const DriveFolder::Entry& file);
  void saveFile(const DriveFolder::Entry& file);

  // + 1 for the leading Refresh row
  int listCount() const override { return state == State::BROWSING ? static_cast<int>(entries.size()) + 1 : 0; }
  bool hasSearch() const override { return false; }
  void performSearch(const std::string&) override {}
  bool handleCustomInput() override;
  void activateIndex(int index) override;
  void buildScreen(UiScreen& screen) override;
  void drawFooter() override;
  void startBrowse() override;
  void downloadFinished(bool cancelled) override;
  void onBackButton() override;
};
