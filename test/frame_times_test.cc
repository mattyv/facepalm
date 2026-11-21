#include "src/frame_times.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Optional;

// A 15 fps recording with a dropped frame between frames 2 and 3, as
// camera_probe writes it.
constexpr std::string_view kManifest =
    "1 1000\n"
    "2 1067\n"
    "3 1200\n";

TEST(FrameTimesTest, GivesEachFrameItsCaptureTime) {
  std::string error;

  EXPECT_THAT(FrameTimes(kManifest,
                         {"rec/frame_000001.png", "rec/frame_000002.png",
                          "rec/frame_000003.png"},
                         error),
              Optional(ElementsAre(1000, 1067, 1200)));
  EXPECT_THAT(error, IsEmpty());
}

TEST(FrameTimesTest, KeepsTheGapOfADroppedFrame) {
  std::string error;

  // 133 ms between them, not the 33 ms a fixed rate would give.
  EXPECT_THAT(
      FrameTimes(kManifest, {"frame_000002.png", "frame_000003.png"}, error),
      Optional(ElementsAre(1067, 1200)));
}

TEST(FrameTimesTest, ReadsAManifestWithoutAFinalNewline) {
  std::string error;

  EXPECT_THAT(FrameTimes("7 40", {"frame_000007.png"}, error),
              Optional(ElementsAre(40)));
}

TEST(FrameTimesTest, ReadsTheManifestInAnyOrder) {
  std::string error;

  EXPECT_THAT(FrameTimes("2 1067\n1 1000\n",
                         {"frame_000001.png", "frame_000002.png"}, error),
              Optional(ElementsAre(1000, 1067)));
}

TEST(FrameTimesTest, RefusesAnImageWithNoLine) {
  std::string error;

  EXPECT_THAT(FrameTimes(kManifest, {"frame_000004.png"}, error),
              Eq(std::nullopt));
  EXPECT_THAT(error, Eq("frame_000004.png has no line in the timestamps"));
}

TEST(FrameTimesTest, RefusesAnImageThatIsNotANumberedFrame) {
  for (const std::string image :
       {"portrait.png", "frame_.png", "frame_1x.png", "frame_000001.jpg",
        "frame_000001", "frame_-1.png"}) {
    std::string error;

    EXPECT_THAT(FrameTimes(kManifest, {image}, error), Eq(std::nullopt))
        << image;
    EXPECT_THAT(error, Eq(image + " is not named frame_<index>.png"));
  }
}

TEST(FrameTimesTest, RefusesATimeThatDoesNotIncrease) {
  std::string error;

  EXPECT_THAT(FrameTimes("1 1000\n2 1000\n",
                         {"frame_000001.png", "frame_000002.png"}, error),
              Eq(std::nullopt));
  EXPECT_THAT(error, Eq("frame_000002.png was captured at 1000 ms, not after "
                        "frame_000001.png at 1000 ms"));
}

TEST(FrameTimesTest, RefusesImagesOutOfCaptureOrder) {
  std::string error;

  EXPECT_THAT(
      FrameTimes(kManifest, {"frame_000002.png", "frame_000001.png"}, error),
      Eq(std::nullopt));
  EXPECT_THAT(error, Eq("frame_000001.png was captured at 1000 ms, not after "
                        "frame_000002.png at 1067 ms"));
}

TEST(FrameTimesTest, RefusesALineItCannotRead) {
  for (const std::string line : {"1", "1 ", "x 1000", "1 x", "1 1000 2",
                                 "1  1000", "1 1000ms", " 1 1"}) {
    std::string error;

    EXPECT_THAT(
        FrameTimes("2 900\n" + line + "\n", {"frame_000002.png"}, error),
        Eq(std::nullopt))
        << line;
    EXPECT_THAT(error,
                Eq("timestamps line 2 is not \"<index> <milliseconds>\""))
        << line;
  }
}

TEST(FrameTimesTest, RefusesAnIndexGivenTwice) {
  std::string error;

  EXPECT_THAT(FrameTimes("1 1000\n1 1067\n", {"frame_000001.png"}, error),
              Eq(std::nullopt));
  EXPECT_THAT(error, Eq("timestamps line 2 repeats frame 1"));
}

TEST(FrameTimesTest, GivesNoTimesForNoImages) {
  std::string error;

  EXPECT_THAT(FrameTimes(kManifest, {}, error), Optional(IsEmpty()));
}

}  // namespace
}  // namespace facepalm
