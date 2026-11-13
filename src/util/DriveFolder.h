#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Helpers for browsing a public Google Drive shared folder through its
// "embeddedfolderview" listing. Pure: no I/O, no globals.
namespace DriveFolder {

struct Entry {
  std::string id;
  std::string name;
  bool isFolder = false;
};

// Extracts the folder id from a share URL (".../folders/<id>?usp=sharing" or "...?id=<id>").
// A bare id is accepted too. Returns false unless the id is a plausible Drive id.
bool extractFolderId(const std::string& shareUrl, std::string& folderId);

// Canonical share link for a folder id (what the config file stores).
std::string shareUrl(const std::string& folderId);
std::string listingUrl(const std::string& folderId);
std::string downloadUrl(const std::string& fileId);

// Streaming parser for the embedded folder view. Feed body chunks as they arrive;
// memory is bounded by MAX_ENTRIES and the per-field caps.
class ListingParser {
 public:
  static constexpr size_t MAX_ENTRIES = 64;
  static constexpr size_t MAX_ID_BYTES = 64;
  static constexpr size_t MAX_NAME_BYTES = 160;

  ListingParser();
  void feed(const uint8_t* data, size_t len);
  const std::vector<Entry>& entries() const { return found; }
  bool truncated() const { return overflow; }

 private:
  enum class State : uint8_t { SEEK_ENTRY, ID, SEEK_TITLE, TITLE };
  State state = State::SEEK_ENTRY;
  std::vector<Entry> found;
  std::string tail;  // last bytes seen while seeking a marker
  std::string field;
  std::string currentId;
  bool folderSeen = false;
  bool overflow = false;

  void onByte(char c);
  bool tailEndsWith(const char* needle, size_t needleLen) const;
};

// Decodes the HTML entities Drive emits in titles (&amp; &#39; &#x27; ...) to UTF-8.
std::string decodeEntities(const std::string& text);

}  // namespace DriveFolder
