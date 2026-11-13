#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "DriveFolder.h"

namespace {

constexpr char SAMPLE[] =
    "<div class=\"flip-entries\">"
    "<div class=\"flip-entry\" id=\"entry-1AbCdEfGhIjKlMnOpQrStUvWxYz012\" tabindex=\"0\">"
    "<a href=\"https://drive.google.com/file/d/1AbCdEfGhIjKlMnOpQrStUvWxYz012/view?usp=drive_web\">"
    "<div class=\"flip-entry-info\"><div class=\"flip-entry-title\">Tom &amp; Jerry&#39;s "
    "book.epub</div></div></a></div>"
    "<div class=\"flip-entry\" id=\"entry-2ZyXwVuTsRqPoNmLkJiHgFeDcBa987\" tabindex=\"0\">"
    "<a href=\"https://drive.google.com/drive/folders/2ZyXwVuTsRqPoNmLkJiHgFeDcBa987\">"
    "<div class=\"flip-entry-info\"><div class=\"flip-entry-title\">Sub folder</div></div></a></div>"
    "</div>";

TEST(DriveFolder, ExtractsIdFromShareUrl) {
  std::string id;
  EXPECT_TRUE(DriveFolder::extractFolderId(
      "https://drive.google.com/drive/folders/199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb?usp=sharing", id));
  EXPECT_EQ(id, "199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb");
}

TEST(DriveFolder, ExtractsIdFromQueryAndBareId) {
  std::string id;
  EXPECT_TRUE(DriveFolder::extractFolderId("https://drive.google.com/open?id=199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb", id));
  EXPECT_EQ(id, "199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb");
  EXPECT_TRUE(DriveFolder::extractFolderId("199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb", id));
}

TEST(DriveFolder, RejectsInvalidUrls) {
  std::string id;
  EXPECT_FALSE(DriveFolder::extractFolderId("", id));
  EXPECT_FALSE(DriveFolder::extractFolderId("https://example.com/page", id));
  EXPECT_FALSE(DriveFolder::extractFolderId("https://drive.google.com/drive/folders/", id));
  EXPECT_FALSE(DriveFolder::extractFolderId("https://drive.google.com/drive/folders/short", id));
}

TEST(DriveFolder, ShareUrlRoundTripsThroughExtraction) {
  std::string id;
  ASSERT_TRUE(DriveFolder::extractFolderId(DriveFolder::shareUrl("199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb"), id));
  EXPECT_EQ(id, "199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb");
}

TEST(DriveFolder, ParsesFilesAndFolders) {
  DriveFolder::ListingParser parser;
  parser.feed(reinterpret_cast<const uint8_t*>(SAMPLE), strlen(SAMPLE));
  ASSERT_EQ(parser.entries().size(), 2u);
  EXPECT_EQ(parser.entries()[0].id, "1AbCdEfGhIjKlMnOpQrStUvWxYz012");
  EXPECT_EQ(parser.entries()[0].name, "Tom & Jerry's book.epub");
  EXPECT_FALSE(parser.entries()[0].isFolder);
  EXPECT_EQ(parser.entries()[1].name, "Sub folder");
  EXPECT_TRUE(parser.entries()[1].isFolder);
}

TEST(DriveFolder, ParsesAcrossArbitraryChunkBoundaries) {
  DriveFolder::ListingParser parser;
  const size_t len = strlen(SAMPLE);
  for (size_t i = 0; i < len; i += 3) {
    parser.feed(reinterpret_cast<const uint8_t*>(SAMPLE) + i, std::min<size_t>(3, len - i));
  }
  EXPECT_EQ(parser.entries().size(), 2u);
}

TEST(DriveFolder, EmptyListingYieldsNoEntries) {
  const char html[] = "<div class=\"flip-entries\"></div>";
  DriveFolder::ListingParser parser;
  parser.feed(reinterpret_cast<const uint8_t*>(html), strlen(html));
  EXPECT_TRUE(parser.entries().empty());
}

TEST(DriveFolder, DecodesNumericEntities) {
  EXPECT_EQ(DriveFolder::decodeEntities("caff&#xE8; &#233; &unknown; &"), "caff\xC3\xA8 \xC3\xA9 &unknown; &");
}

}  // namespace
