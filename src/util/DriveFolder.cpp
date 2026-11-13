#include "DriveFolder.h"

#include <cstdlib>
#include <cstring>

namespace DriveFolder {
namespace {
constexpr char ENTRY_MARKER[] = "id=\"entry-";
constexpr char TITLE_MARKER[] = "flip-entry-title\">";
constexpr char FOLDER_MARKER[] = "/drive/folders/";
constexpr size_t TAIL_BYTES = 24;  // longer than every marker above

bool isIdChar(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

bool isPlausibleId(const std::string& id) {
  if (id.size() < 10 || id.size() > ListingParser::MAX_ID_BYTES) return false;
  for (const char c : id) {
    if (!isIdChar(c)) return false;
  }
  return true;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}
}  // namespace

bool extractFolderId(const std::string& shareUrl, std::string& folderId) {
  std::string candidate;
  size_t pos = shareUrl.find("/folders/");
  if (pos != std::string::npos) {
    pos += strlen("/folders/");
  } else if ((pos = shareUrl.find("id=")) != std::string::npos) {
    pos += strlen("id=");
  } else if (shareUrl.find('/') == std::string::npos && shareUrl.find('?') == std::string::npos) {
    pos = 0;  // bare id
  } else {
    return false;
  }
  while (pos < shareUrl.size() && isIdChar(shareUrl[pos])) candidate += shareUrl[pos++];
  if (!isPlausibleId(candidate)) return false;
  folderId = std::move(candidate);
  return true;
}

std::string shareUrl(const std::string& folderId) {
  return "https://drive.google.com/drive/folders/" + folderId + "?usp=sharing";
}

std::string listingUrl(const std::string& folderId) {
  return "https://drive.google.com/embeddedfolderview?id=" + folderId;
}

std::string downloadUrl(const std::string& fileId) {
  // confirm=t skips the virus-scan interstitial Drive shows for larger files.
  return "https://drive.usercontent.google.com/download?id=" + fileId + "&export=download&confirm=t";
}

std::string decodeEntities(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '&') {
      out += text[i];
      continue;
    }
    const size_t semi = text.find(';', i);
    if (semi == std::string::npos || semi - i > 10) {
      out += text[i];
      continue;
    }
    const std::string name = text.substr(i + 1, semi - i - 1);
    if (name == "amp") {
      out += '&';
    } else if (name == "lt") {
      out += '<';
    } else if (name == "gt") {
      out += '>';
    } else if (name == "quot") {
      out += '"';
    } else if (name == "apos") {
      out += '\'';
    } else if (name == "nbsp") {
      out += ' ';
    } else if (name.size() > 1 && name[0] == '#') {
      const bool hex = name[1] == 'x' || name[1] == 'X';
      char* end = nullptr;
      const unsigned long cp = strtoul(name.c_str() + (hex ? 2 : 1), &end, hex ? 16 : 10);
      if (end == name.c_str() + name.size()) {
        appendUtf8(out, static_cast<uint32_t>(cp));
      } else {
        out += text.substr(i, semi - i + 1);
      }
    } else {
      out += text.substr(i, semi - i + 1);
    }
    i = semi;
  }
  return out;
}

ListingParser::ListingParser() {
  found.reserve(16);
  tail.reserve(TAIL_BYTES + 1);
  field.reserve(MAX_NAME_BYTES + 1);
}

bool ListingParser::tailEndsWith(const char* needle, const size_t needleLen) const {
  return tail.size() >= needleLen && memcmp(tail.data() + tail.size() - needleLen, needle, needleLen) == 0;
}

void ListingParser::feed(const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) onByte(static_cast<char>(data[i]));
}

void ListingParser::onByte(const char c) {
  switch (state) {
    case State::SEEK_ENTRY:
    case State::SEEK_TITLE:
      tail += c;
      if (tail.size() > TAIL_BYTES) tail.erase(0, tail.size() - TAIL_BYTES);
      if (state == State::SEEK_ENTRY) {
        if (tailEndsWith(ENTRY_MARKER, sizeof(ENTRY_MARKER) - 1)) {
          state = State::ID;
          field.clear();
          folderSeen = false;
          tail.clear();
        }
      } else if (tailEndsWith(FOLDER_MARKER, sizeof(FOLDER_MARKER) - 1)) {
        folderSeen = true;
      } else if (tailEndsWith(TITLE_MARKER, sizeof(TITLE_MARKER) - 1)) {
        state = State::TITLE;
        field.clear();
        tail.clear();
      }
      break;
    case State::ID:
      if (c == '"') {
        if (isPlausibleId(field)) {
          currentId = field;
          state = State::SEEK_TITLE;
        } else {
          state = State::SEEK_ENTRY;
        }
        tail.clear();
      } else if (field.size() < MAX_ID_BYTES + 1) {
        field += c;
      }
      break;
    case State::TITLE:
      if (c == '<') {
        if (found.size() < MAX_ENTRIES) {
          Entry entry;
          entry.id = std::move(currentId);
          entry.name = decodeEntities(field);
          entry.isFolder = folderSeen;
          if (!entry.name.empty()) found.push_back(std::move(entry));
        } else {
          overflow = true;
        }
        state = State::SEEK_ENTRY;
        tail.clear();
      } else if (field.size() < MAX_NAME_BYTES * 2) {
        field += c;  // entities are decoded afterwards, so allow headroom
      }
      break;
  }
}

}  // namespace DriveFolder
