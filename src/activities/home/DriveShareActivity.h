#pragma once

#include <string>
#include <vector>

#include "activities/CatalogActivity.h"
#include "util/DriveFolder.h"

// Lists the files of a public Google Drive shared folder and saves a chosen
// file to the SD root. The share URL lives in DriveShareActivity::CONFIG_PATH.
// See docs/drive-share.md.
class DriveShareActivity final : public CatalogActivity {
 public:
  static constexpr char CONFIG_DIR[] = "/.crosspoint/apps/drive-share";
  static constexpr char CONFIG_PATH[] = "/.crosspoint/apps/drive-share/config.json";

  explicit DriveShareActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 private:
  static constexpr size_t MAX_CONFIG_SIZE = 1024;

  std::string rootFolderId;
  std::string currentFolderId;
  std::vector<std::string> folderHistory;  // parent folder ids, root first
  std::vector<DriveFolder::Entry> entries;
  std::vector<freeink::ui::ListItem> rowItems;  // views into entries; rebuilt with them
  bool configValid = false;

  bool loadConfig();
  void rebuildRowItems();
  void releaseEntries();
  void fetchListing();
  void confirmSave(const DriveFolder::Entry& file);
  void saveFile(const DriveFolder::Entry& file);

  int listCount() const override { return state == State::BROWSING ? static_cast<int>(entries.size()) : 0; }
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
