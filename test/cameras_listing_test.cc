#include "src/cameras_listing.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

MATCHER_P3(IsEntry, id, name, built_in, "") {
  return arg.id == id && arg.name == name && arg.built_in == built_in;
}

TEST(CamerasListingTest, ParsesEachTabSeparatedLine) {
  EXPECT_THAT(ParseCamerasListing("abc\tFaceTime HD Camera\t1\n"
                                  "def\tContinuity Camera\t0\n"),
              ElementsAre(IsEntry("abc", "FaceTime HD Camera", true),
                          IsEntry("def", "Continuity Camera", false)));
}

TEST(CamerasListingTest, EmptyTextListsNoCamera) {
  EXPECT_THAT(ParseCamerasListing(""), IsEmpty());
}

TEST(CamerasListingTest, SkipsALineWithoutExactlyThreeFields) {
  EXPECT_THAT(ParseCamerasListing("abc\tonly two\n"), IsEmpty());
  EXPECT_THAT(ParseCamerasListing("abc\ttoo\tmany\tfields\n"), IsEmpty());
  EXPECT_THAT(ParseCamerasListing("no tabs at all\n"), IsEmpty());
}

TEST(CamerasListingTest, OnlyExactlyOneReadsAsBuiltIn) {
  EXPECT_THAT(ParseCamerasListing("abc\tname\t10\n"),
              ElementsAre(IsEntry("abc", "name", false)));
  EXPECT_THAT(ParseCamerasListing("abc\tname\t\n"),
              ElementsAre(IsEntry("abc", "name", false)));
}

TEST(CamerasListingTest, ReadsALastLineWithNoTrailingNewline) {
  EXPECT_THAT(ParseCamerasListing("abc\tname\t1"),
              ElementsAre(IsEntry("abc", "name", true)));
}

TEST(CamerasListingTest, AnEmptyFieldIsKept) {
  EXPECT_THAT(ParseCamerasListing("\t\t1\n"),
              ElementsAre(IsEntry("", "", true)));
}

}  // namespace
}  // namespace facepalm
