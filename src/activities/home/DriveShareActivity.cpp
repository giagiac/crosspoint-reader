#include "DriveShareActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/CatalogScreens.h"
#include "components/UITheme.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr char DEFAULT_CONFIG[] =
    "{\n  \"share_url\": \"https://drive.google.com/drive/folders/199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb?usp=sharing\"\n}\n";
}  // namespace

DriveShareActivity::DriveShareActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : CatalogActivity("DriveShare", renderer, mappedInput) {}

void DriveShareActivity::onEnter() {
  CatalogActivity::onEnter();
  configValid = loadConfig();
  if (!configValid) {
    fail(StrId::STR_DRIVE_CONFIG_INVALID);
    return;
  }
  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  checkAndConnectWifi();
}

void DriveShareActivity::onExit() {
  releaseEntries();
  folderHistory.clear();
  CatalogActivity::onExit();
}

// Creates the config with the example share on first use, then reads the folder id from it.
bool DriveShareActivity::loadConfig() {
  if (!Storage.exists(CONFIG_PATH)) {
    if (!Storage.ensureDirectoryExists(CONFIG_DIR) || !Storage.writeFile(CONFIG_PATH, DEFAULT_CONFIG)) {
      LOG_ERR("DRIVE", "Cannot create config: %s", CONFIG_PATH);
      return false;
    }
  }

  std::string raw;
  if (!Storage.readFileToString("DRIVE", CONFIG_PATH, MAX_CONFIG_SIZE, raw)) {
    LOG_ERR("DRIVE", "Cannot read config: %s", CONFIG_PATH);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) {
    LOG_ERR("DRIVE", "Invalid config JSON: %s", CONFIG_PATH);
    return false;
  }
  const char* shareUrl = doc["share_url"] | "";
  if (!DriveFolder::extractFolderId(shareUrl, rootFolderId)) {
    LOG_ERR("DRIVE", "No Drive folder id in share_url");
    return false;
  }
  currentFolderId = rootFolderId;
  return true;
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
  rebuildRowItems();
  state = State::BROWSING;
  requestUpdate();
}

void DriveShareActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(entries.size());
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
  if (state != State::BROWSING || index < 0 || index >= static_cast<int>(entries.size())) return;
  app.clearTapFlash();
  const DriveFolder::Entry entry = entries[index];
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
  if (state == State::ERROR && !configValid) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) onBackButton();
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
  syncListViewport(screen, props);
  screen.list(props);
}

void DriveShareActivity::drawFooter() {
  MappedInputManager::Labels labels;
  switch (state) {
    case State::BROWSING: {
      const bool folder = !entries.empty() && entries[nav.selected].isFolder;
      labels = mappedInput.mapLabels(tr(STR_BACK), folder ? tr(STR_OPEN) : tr(STR_DOWNLOAD), tr(STR_DIR_UP),
                                     tr(STR_DIR_DOWN));
      break;
    }
    case State::DOWNLOADING:
      labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      break;
    case State::ERROR:
      labels = mappedInput.mapLabels(tr(STR_BACK), configValid ? tr(STR_RETRY) : "", "", "");
      break;
    default:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
