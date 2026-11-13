#include "DriveShareActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <ctime>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/CatalogScreens.h"
#include "components/UITheme.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/DriveShareConfig.h"
#include "util/StringUtils.h"

namespace fui = freeink::ui;

DriveShareActivity::DriveShareActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : CatalogActivity("DriveShare", renderer, mappedInput) {}

void DriveShareActivity::onEnter() {
  CatalogActivity::onEnter();
  beginSession();
}

void DriveShareActivity::onExit() {
  releaseEntries();
  folderHistory.clear();
  CatalogActivity::onExit();
}

// Re-reads the setting so a change made on the web page applies on Retry.
void DriveShareActivity::beginSession() {
  folderHistory.clear();
  if (!DriveShareConfig::load(rootFolderId)) {
    state = State::NO_TOKEN;  // not configured: nothing to fetch yet
    requestUpdate();
    return;
  }
  currentFolderId = rootFolderId;
  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  checkAndConnectWifi();
}

void DriveShareActivity::startBrowse() {
  nav.reset();
  beginLoading();
  fetchListing();
}

void DriveShareActivity::fetchListing() {
  releaseEntries();
  DriveFolder::ListingParser parser;
  const std::string url = DriveFolder::listingUrl(currentFolderId);
  LOG_DBG("DRIVE", "Fetching: %s", url.c_str());
  const bool ok = HttpDownloader::fetchUrl(url, [&parser](const uint8_t* data, size_t len) {
    parser.feed(data, len);
    return true;
  });
  if (!ok) {
    fail(StrId::STR_DRIVE_FETCH_FAILED);
    return;
  }
  if (parser.truncated()) LOG_INF("DRIVE", "Listing truncated to %u entries", (unsigned)parser.entries().size());

  entries = parser.entries();
  if (entries.empty()) {
    fail(StrId::STR_NO_ENTRIES);
    return;
  }
  stampLastSync();
  rebuildRowItems();
  // Start on the first file; Refresh is one step up.
  nav.selected = 1;
  nav.follow(listCount());
  state = State::BROWSING;
  requestUpdate();
}

// Records when the shown listing was fetched. The RTC is preferred; the
// persisted system clock covers boards without one. An unset clock (year < 2024)
// is treated as unavailable rather than shown as a bogus date.
void DriveShareActivity::stampLastSync() {
  constexpr int MIN_VALID_YEAR = 2024;
  struct tm local{};
  bool haveTime = halClock.localTime(local) && local.tm_year + 1900 >= MIN_VALID_YEAR;
  if (!haveTime) {
    const time_t now = static_cast<time_t>(trustedtime::trustedNow());
    haveTime = now > 0 && localtime_r(&now, &local) != nullptr && local.tm_year + 1900 >= MIN_VALID_YEAR;
  }
  if (!haveTime) {
    lastSyncLabel[0] = '\0';
    return;
  }
  char clock[12];
  if (SETTINGS.clockFormat == 1) {
    const int hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    snprintf(clock, sizeof(clock), "%d:%02d %s", hour12, local.tm_min, local.tm_hour >= 12 ? "PM" : "AM");
  } else {
    snprintf(clock, sizeof(clock), "%02d:%02d", local.tm_hour, local.tm_min);
  }
  snprintf(lastSyncLabel, sizeof(lastSyncLabel), "%s %02d/%02d %s", tr(STR_LAST_SYNC), local.tm_mday, local.tm_mon + 1,
           clock);
}

void DriveShareActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(entries.size() + 1);
  fui::ListItem refresh;
  refresh.label = tr(STR_REFRESH);
  if (lastSyncLabel[0]) refresh.subtitle = lastSyncLabel;
  refresh.actionValue = 0;
  rowItems.push_back(refresh);
  for (const auto& entry : entries) {
    fui::ListItem item;
    item.label = entry.name.c_str();
    if (entry.isFolder) item.value = ">";
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
}

void DriveShareActivity::releaseEntries() {
  // The interaction table holds row indices for the old entries.
  closeRouting();
  std::vector<DriveFolder::Entry>().swap(entries);
  std::vector<fui::ListItem>().swap(rowItems);
}

void DriveShareActivity::activateIndex(const int index) {
  if (state != State::BROWSING || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  if (index == 0) {
    checkAndConnectWifi();  // the link may have dropped in standby
    return;
  }
  const DriveFolder::Entry entry = entries[index - 1];
  if (entry.isFolder) {
    folderHistory.push_back(currentFolderId);
    currentFolderId = entry.id;
    startBrowse();
  } else {
    confirmSave(entry);
  }
}

void DriveShareActivity::confirmSave(const DriveFolder::Entry& file) {
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_SAVE_TO_SD_ROOT), file.name);
  if (!confirmation) {
    LOG_ERR("DRIVE", "OOM: save confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this, file](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    saveFile(file);
  });
}

void DriveShareActivity::saveFile(const DriveFolder::Entry& file) {
  const std::string filename = StringUtils::sanitizeFilenamePreservingExtension(file.name);
  if (filename.empty()) {
    fail(StrId::STR_DOWNLOAD_FAILED);
    return;
  }
  const std::string dest = "/" + filename;
  beginDownload(filename);
  // Entries are a few KB; they stay loaded so the list returns without another fetch.
  const auto result = downloadFile(DriveFolder::downloadUrl(file.id), dest);
  if (result == HttpDownloader::OK) {
    clearBookCache(dest);
    library::markLibraryIndexDirty();
  }
  finishDownload(result);
}

void DriveShareActivity::downloadFinished(const bool cancelled) {
  state = cancelled ? State::BROWSING : State::DONE;
  requestUpdate();
}

bool DriveShareActivity::handleCustomInput() {
  if (state == State::DONE) {
    int x = 0, y = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      state = State::BROWSING;
      requestUpdate();
    }
    return true;
  }
  if (state == State::NO_TOKEN) {
    int x = 0, y = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onBackButton();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      beginSession();
    }
    return true;
  }
  return CatalogActivity::handleCustomInput();
}

void DriveShareActivity::onBackButton() {
  if (!folderHistory.empty()) {
    currentFolderId = folderHistory.back();
    folderHistory.pop_back();
    startBrowse();
    return;
  }
  activityManager.goToLocalApps();
}

void DriveShareActivity::buildScreen(UiScreen& screen) {
  screenHeader(screen, tr(STR_DRIVE_SHARE));
  if (buildStatusScreen(screen, /*boldError=*/false, /*showDownloadTotal=*/true)) return;
  if (state == State::NO_TOKEN) {
    catalogCenteredBlock(
        screen, {{tr(STR_DRIVE_NOT_CONFIGURED), true}, {tr(STR_DRIVE_CONFIGURE_HINT)}, {tr(STR_DRIVE_CONFIGURE_PATH)}});
    return;
  }
  if (state == State::DONE) {
    catalogCenteredBlock(screen, {{tr(STR_FILE_SAVED), true}, {statusMessage.c_str()}});
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}

void DriveShareActivity::drawFooter() {
  MappedInputManager::Labels labels;
  switch (state) {
    case State::BROWSING: {
      const char* confirmLabel = tr(STR_REFRESH);
      if (nav.selected > 0 && nav.selected <= static_cast<int>(entries.size())) {
        confirmLabel = entries[nav.selected - 1].isFolder ? tr(STR_OPEN) : tr(STR_DOWNLOAD);
      }
      labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      break;
    }
    case State::DOWNLOADING:
      labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      break;
    case State::ERROR:
    case State::NO_TOKEN:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
      break;
    default:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
