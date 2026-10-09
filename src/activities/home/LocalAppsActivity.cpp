#include "LocalAppsActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace {
constexpr char APPS_ROOT[] = "/.crosspoint/apps";
constexpr char APP_MANIFEST[] = "app.json";
constexpr char DEFAULT_DATA_FILE[] = "dati.txt";
constexpr int DEFAULT_PAGE_SIZE = 6;
}  // namespace

LocalAppsActivity::LocalAppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("LocalApps", renderer, mappedInput) {
  apps.reserve(MAX_APPS);
}

void LocalAppsActivity::onEnter() {
  RenderLock lock(*this);
  UiListActivity::onEnter();
  scanApps();
}

void LocalAppsActivity::onExit() {
  if (dataFile.isOpen()) dataFile.close();
  Activity::onExit();
}

bool LocalAppsActivity::isSafeDataFilename(const char* filename) {
  if (!filename || filename[0] == '\0' || strlen(filename) > 64) return false;
  const size_t length = strlen(filename);
  if (length < 5 || strcmp(filename + length - 4, ".txt") != 0 || strstr(filename, "..")) return false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(filename); *p; ++p) {
    const bool valid = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' ||
                       *p == '-' || *p == '.';
    if (!valid) return false;
  }
  return true;
}

void LocalAppsActivity::scanApps() {
  apps.clear();
  if (!Storage.exists(APPS_ROOT)) return;

  HalFile root = Storage.open(APPS_ROOT);
  if (!root || !root.isDirectory()) {
    LOG_ERR("LAPP", "Cannot open apps directory");
    return;
  }

  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory() || apps.size() >= MAX_APPS) continue;
    char name[65];
    if (entry.getName(name, sizeof(name)) == 0 || name[0] == '.' || strlen(name) > 64) continue;

    const std::string directory = std::string(APPS_ROOT) + "/" + name;
    const std::string manifestPath = directory + "/" + APP_MANIFEST;
    if (!Storage.exists(manifestPath.c_str())) continue;

    std::string raw;
    if (!Storage.readFileToString("LAPP", manifestPath, MAX_MANIFEST_SIZE, raw)) {
      LOG_ERR("LAPP", "Cannot read app manifest: %s", manifestPath.c_str());
      continue;
    }

    JsonDocument doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) {
      LOG_ERR("LAPP", "Invalid app manifest: %s", manifestPath.c_str());
      continue;
    }

    const char* manifestTitle = doc["title"] | name;
    const char* dataFileName = doc["data"] | DEFAULT_DATA_FILE;
    if (!manifestTitle[0] || strlen(manifestTitle) > 80 || !isSafeDataFilename(dataFileName)) {
      LOG_ERR("LAPP", "Invalid app metadata: %s", manifestPath.c_str());
      continue;
    }

    LocalApp app;
    app.name = name;
    app.title = manifestTitle;
    app.dataPath = directory + "/" + dataFileName;
    const int requestedPageSize = doc["page_size"].is<int>() ? doc["page_size"].as<int>() : DEFAULT_PAGE_SIZE;
    app.pageSize = static_cast<uint8_t>(std::clamp(requestedPageSize, 1, static_cast<int>(MAX_PAGE_SIZE)));
    if (!Storage.exists(app.dataPath.c_str())) {
      LOG_ERR("LAPP", "Missing app data file: %s", app.dataPath.c_str());
      continue;
    }
    apps.push_back(std::move(app));
  }

  if (apps.size() == MAX_APPS) LOG_ERR("LAPP", "App list limited to %u entries", static_cast<unsigned>(MAX_APPS));
}

int LocalAppsActivity::listCount() const {
  if (!showingApp) return static_cast<int>(apps.size()) + 1;  // + built-in Drive row
  if (invalidData || lineCount == 0) return 1;
  int count = pageLineCount;
  if (pageIndex > 0) ++count;
  if (pageIndex + 1 < pageCount) ++count;
  return count;
}

bool LocalAppsActivity::countLines(uint32_t& count) {
  count = 0;
  uint8_t buffer[READ_BUFFER_SIZE];
  size_t lineLength = 0;
  bool hasPendingLine = false;
  for (;;) {
    const int bytesRead = dataFile.read(buffer, sizeof(buffer));
    if (bytesRead < 0) return false;
    if (bytesRead == 0) break;
    for (int i = 0; i < bytesRead; ++i) {
      const uint8_t value = buffer[i];
      if (value == '\n') {
        if (count == UINT32_MAX) return false;
        ++count;
        lineLength = 0;
        hasPendingLine = false;
      } else {
        hasPendingLine = true;
        if (value == '\0') return false;
        if (value != '\r' && ++lineLength > MAX_LINE_BYTES) return false;
      }
    }
  }
  if (hasPendingLine) {
    if (count == UINT32_MAX) return false;
    ++count;
  }
  return dataFile.seek(0);
}

int LocalAppsActivity::readNextByte() {
  if (readBufferPosition >= readBufferLength) {
    const int bytesRead = dataFile.read(pageReadBuffer, sizeof(pageReadBuffer));
    if (bytesRead <= 0) return -1;
    readBufferLength = static_cast<size_t>(bytesRead);
    readBufferPosition = 0;
  }
  return pageReadBuffer[readBufferPosition++];
}

bool LocalAppsActivity::readLine(size_t& lineLength) {
  lineLength = 0;
  bool hasBytes = false;
  bool tooLong = false;
  for (;;) {
    const int value = readNextByte();
    if (value < 0) {
      if (!hasBytes) return false;
      break;
    }
    if (value == '\n') break;
    hasBytes = true;
    if (value == '\r') continue;
    if (lineLength == MAX_LINE_BYTES) {
      tooLong = true;
      continue;
    }
    lineBuffer[lineLength++] = static_cast<char>(value);
  }
  if (tooLong) return false;
  lineBuffer[lineLength] = '\0';
  return true;
}

bool LocalAppsActivity::loadPage(const uint32_t page) {
  if (selectedApp < 0 || static_cast<size_t>(selectedApp) >= apps.size() || page >= pageCount) return false;
  const bool canContinue = !invalidData && page == pageIndex + 1;
  if (!canContinue) {
    if (!dataFile.seek(0)) {
      invalidData = true;
      return false;
    }
    readBufferPosition = readBufferLength = 0;
  }

  const uint32_t firstLine = canContinue ? 0 : page * apps[selectedApp].pageSize;
  for (uint32_t i = 0; i < firstLine; ++i) {
    size_t skippedLineLength;
    if (!readLine(skippedLineLength)) {
      invalidData = true;
      pageLineCount = 0;
      return false;
    }
  }

  pageLineCount = 0;
  for (uint8_t i = 0; i < apps[selectedApp].pageSize && firstLine + i < lineCount; ++i) {
    size_t lineLength;
    if (!readLine(lineLength)) {
      invalidData = true;
      pageLineCount = 0;
      return false;
    }
    memcpy(pageLines[pageLineCount].data(), lineBuffer, lineLength + 1);
    ++pageLineCount;
  }
  pageIndex = page;
  invalidData = false;
  updatePageTitle();
  return true;
}

bool LocalAppsActivity::openApp(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= apps.size()) return false;
  if (dataFile.isOpen()) dataFile.close();
  selectedApp = index;
  showingApp = true;
  invalidData = false;
  lineCount = pageCount = pageIndex = 0;
  pageLineCount = 0;
  readBufferPosition = readBufferLength = 0;
  if (!Storage.openFileForRead("LAPP", apps[index].dataPath.c_str(), dataFile) || !dataFile) {
    LOG_ERR("LAPP", "Cannot open app data file: %s", apps[index].dataPath.c_str());
    invalidData = true;
    updatePageTitle();
    return false;
  }
  if (!countLines(lineCount)) {
    LOG_ERR("LAPP", "Invalid app data file: %s", apps[index].dataPath.c_str());
    invalidData = true;
    lineCount = 0;
    updatePageTitle();
    return false;
  }
  pageCount = lineCount == 0 ? 0 : (lineCount - 1) / apps[index].pageSize + 1;
  if (pageCount > 0 && !loadPage(0)) {
    LOG_ERR("LAPP", "Cannot read app data page: %s", apps[index].dataPath.c_str());
    invalidData = true;
    pageLineCount = 0;
    updatePageTitle();
    return false;
  }
  updatePageTitle();
  return true;
}

void LocalAppsActivity::updatePageTitle() {
  if (!showingApp || selectedApp < 0 || static_cast<size_t>(selectedApp) >= apps.size()) {
    pageTitle[0] = '\0';
    return;
  }
  if (pageCount == 0) {
    snprintf(pageTitle, sizeof(pageTitle), "%s", apps[selectedApp].title.c_str());
    return;
  }
  snprintf(pageTitle, sizeof(pageTitle), "%s (%u/%u)", apps[selectedApp].title.c_str(),
           static_cast<unsigned>(pageIndex + 1), static_cast<unsigned>(pageCount));
}

const char* LocalAppsActivity::headerTitle() const { return showingApp ? pageTitle : tr(STR_LOCAL_APPS); }

void LocalAppsActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<LocalAppsActivity*>(ctx);
  item.actionValue = static_cast<int16_t>(index);
  if (!self->showingApp) {
    item.label = index < self->apps.size() ? self->apps[index].title.c_str() : tr(STR_DRIVE_SHARE);
    return;
  }
  if (self->invalidData) {
    item.label = tr(STR_INVALID_APP_DATA);
    return;
  }
  if (self->lineCount == 0) {
    item.label = tr(STR_EMPTY_APP);
    return;
  }

  const size_t lineRows = self->pageLineCount;
  if (index < lineRows) {
    item.label = self->pageLines[index].data();
    return;
  }
  const size_t previousRow = lineRows;
  if (self->pageIndex > 0) {
    if (index == previousRow) {
      item.label = tr(STR_PREV_PAGE);
      return;
    }
  }
  item.label = tr(STR_NEXT_PAGE);
}

void LocalAppsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.rowProvider = &LocalAppsActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}

void LocalAppsActivity::activateIndex(const int index) {
  RenderLock lock(*this);
  app.clearTapFlash();
  if (!showingApp) {
    if (index == static_cast<int>(apps.size())) {
      activityManager.goToDriveShare();
      return;
    }
    openApp(index);
    requestUpdate();
    return;
  }
  if (invalidData || lineCount == 0) return;

  const int lineRows = pageLineCount;
  int actionIndex = lineRows;
  if (pageIndex > 0) {
    if (index == actionIndex) {
      if (!loadPage(pageIndex - 1)) {
        LOG_ERR("LAPP", "Cannot read previous app page");
        invalidData = true;
        pageLineCount = 0;
        updatePageTitle();
      }
      requestUpdate();
      return;
    }
    ++actionIndex;
  }
  if (pageIndex + 1 < pageCount && index == actionIndex) {
    if (!loadPage(pageIndex + 1)) {
      LOG_ERR("LAPP", "Cannot read next app page");
      invalidData = true;
      pageLineCount = 0;
      updatePageTitle();
    }
    requestUpdate();
  }
}

void LocalAppsActivity::onBackButton() {
  if (showingApp) {
    RenderLock lock(*this);
    if (dataFile.isOpen()) dataFile.close();
    showingApp = false;
    selectedApp = -1;
    invalidData = false;
    lineCount = pageCount = pageIndex = 0;
    pageLineCount = 0;
    readBufferPosition = readBufferLength = 0;
    app.clearTapFlash();
    requestUpdate();
    return;
  }
  UiListActivity::onBackButton();
}
