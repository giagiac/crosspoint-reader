#pragma once
#include <string>

// Persistent Google Drive share setting, stored on the SD card and edited from
// the web settings page. Shared by DriveShareActivity and CrossPointWebServer.
namespace DriveShareConfig {

constexpr char DIR[] = "/.crosspoint/apps/drive-share";
constexpr char PATH[] = "/.crosspoint/apps/drive-share/config.json";

// Reads the folder id. False when the file is missing or holds no valid id.
bool load(std::string& folderId);

// Writes the share link for a validated folder id.
bool save(const std::string& folderId);

// Removes the setting. True when nothing is left configured.
bool clear();

}  // namespace DriveShareConfig
