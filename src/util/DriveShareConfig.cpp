#include "DriveShareConfig.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include "util/DriveFolder.h"

namespace {
constexpr size_t MAX_CONFIG_SIZE = 1024;
}

namespace DriveShareConfig {

bool load(std::string& folderId) {
  if (!Storage.exists(PATH)) return false;

  std::string raw;
  if (!Storage.readFileToString("DRIVE", PATH, MAX_CONFIG_SIZE, raw)) {
    LOG_ERR("DRIVE", "Cannot read config: %s", PATH);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) {
    LOG_ERR("DRIVE", "Invalid config JSON: %s", PATH);
    return false;
  }
  const char* shareUrl = doc["share_url"] | "";
  return DriveFolder::extractFolderId(shareUrl, folderId);
}

bool save(const std::string& folderId) {
  JsonDocument doc;
  doc["share_url"] = DriveFolder::shareUrl(folderId);
  String out;
  serializeJsonPretty(doc, out);
  if (!Storage.ensureDirectoryExists(DIR) || !Storage.writeFile(PATH, out)) {
    LOG_ERR("DRIVE", "Cannot write config: %s", PATH);
    return false;
  }
  return true;
}

bool clear() {
  if (!Storage.exists(PATH)) return true;
  if (!Storage.remove(PATH)) {
    LOG_ERR("DRIVE", "Cannot remove config: %s", PATH);
    return false;
  }
  return true;
}

}  // namespace DriveShareConfig
