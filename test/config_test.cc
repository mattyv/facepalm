#include "src/config.h"

#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::FloatEq;

TEST(ParseConfigTest, AnEmptyFileGivesTheDefaults) {
  std::string error;
  const std::optional<Config> parsed = ParseConfig("", error);
  ASSERT_TRUE(parsed.has_value());
  const Config config = parsed.value_or(Config{});

  EXPECT_THAT(config.frames_per_second, Eq(15));
  EXPECT_TRUE(config.head_pose_enabled);
  EXPECT_TRUE(config.gestures_enabled);
  EXPECT_THAT(config.motion_threshold, FloatEq(0.5F));
  EXPECT_THAT(config.gesture_threshold, FloatEq(0.9F));
}

TEST(ParseConfigTest, ReadsEverySetting) {
  std::string error;
  const std::optional<Config> parsed = ParseConfig(
      "camera.frames_per_second = 30\n"
      "head_pose.enabled = false\n"
      "gestures.enabled=false\n"
      "  gestures.motion_threshold =\t0.25  \n"
      "gestures.gesture_threshold = 1",
      error);
  ASSERT_TRUE(parsed.has_value()) << error;
  const Config config = parsed.value_or(Config{});

  EXPECT_THAT(config.frames_per_second, Eq(30));
  EXPECT_FALSE(config.head_pose_enabled);
  EXPECT_FALSE(config.gestures_enabled);
  EXPECT_THAT(config.motion_threshold, FloatEq(0.25F));
  EXPECT_THAT(config.gesture_threshold, FloatEq(1));
}

TEST(ParseConfigTest, SkipsCommentsAndBlankLines) {
  std::string error;
  const std::optional<Config> parsed = ParseConfig(
      "# Faster, for quick nods.\n\n   \ncamera.frames_per_second = 30 # was "
      "15\n",
      error);
  ASSERT_TRUE(parsed.has_value()) << error;

  EXPECT_THAT(parsed.value_or(Config{}).frames_per_second, Eq(30));
}

// Parses `text`, which must fail, and returns the error.
std::string ErrorFor(const std::string& text) {
  std::string error;
  EXPECT_THAT(ParseConfig(text, error), Eq(std::nullopt)) << text;
  return error;
}

TEST(ParseConfigTest, RefusesAnUnknownSettingNamingTheLine) {
  EXPECT_THAT(ErrorFor("# ok\ncamera.fps = 30\n"),
              Eq("line 2: unknown setting camera.fps"));
}

TEST(ParseConfigTest, RefusesALineWithoutAnEqualsSign) {
  EXPECT_THAT(ErrorFor("gestures.enabled\n"),
              Eq("line 1: expected name = value"));
}

TEST(ParseConfigTest, RefusesARepeatedSetting) {
  EXPECT_THAT(ErrorFor("gestures.enabled = true\ngestures.enabled = false\n"),
              Eq("line 2: repeats gestures.enabled"));
}

TEST(ParseConfigTest, RefusesAFrameRateOutsideOneToSixty) {
  EXPECT_THAT(ErrorFor("camera.frames_per_second = 0"),
              Eq("line 1: expected a whole number from 1 to 60"));
  EXPECT_THAT(ErrorFor("camera.frames_per_second = 61"),
              Eq("line 1: expected a whole number from 1 to 60"));
  EXPECT_THAT(ErrorFor("camera.frames_per_second = 15fps"),
              Eq("line 1: expected a whole number from 1 to 60"));
  EXPECT_THAT(ErrorFor("camera.frames_per_second = fast"),
              Eq("line 1: expected a whole number from 1 to 60"));
}

TEST(ParseConfigTest, RefusesASwitchOtherThanTrueOrFalse) {
  EXPECT_THAT(ErrorFor("head_pose.enabled = yes"),
              Eq("line 1: expected true or false"));
}

TEST(ParseConfigTest, RefusesAThresholdOutsideZeroToOne) {
  EXPECT_THAT(ErrorFor("gestures.motion_threshold = -0.1"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("gestures.gesture_threshold = 1.5"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("gestures.gesture_threshold = high"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("gestures.gesture_threshold = 0.9x"),
              Eq("line 1: expected a number from 0 to 1"));
}

}  // namespace
}  // namespace facepalm
