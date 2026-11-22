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
  EXPECT_THAT(config.camera_device_id, Eq(""));
  EXPECT_TRUE(config.head_pose_enabled);
  EXPECT_TRUE(config.gestures_enabled);
  EXPECT_THAT(config.gesture_confidence_threshold, FloatEq(0.5F));
  EXPECT_TRUE(config.expressions_enabled);
  EXPECT_THAT(config.expressions_smallest_rise, FloatEq(0.06F));
  EXPECT_THAT(config.expressions_spreads, FloatEq(3.0F));
  EXPECT_THAT(config.expressions_hold_ms, Eq(200));
  EXPECT_THAT(config.expressions_pose_degrees, FloatEq(10.0F));
  EXPECT_THAT(config.expressions_pose_range, FloatEq(3.0F));
  EXPECT_THAT(config.expressions_settle_ms, Eq(600));
  EXPECT_TRUE(config.looking_away_enabled);
  EXPECT_THAT(config.looking_away_degrees, FloatEq(15.0F));
  EXPECT_THAT(config.looking_away_hold_ms, Eq(1000));
  EXPECT_TRUE(config.screen_context_enabled);
  EXPECT_FALSE(config.screen_context_window_titles_enabled);
  EXPECT_THAT(config.screen_context_other_screen_degrees, FloatEq(7.5F));
  EXPECT_THAT(config.eye_gaze_horizontal_gain, FloatEq(30.0F));
  EXPECT_THAT(config.eye_gaze_vertical_gain, FloatEq(30.0F));
  EXPECT_THAT(config.eye_gaze_blink_threshold, FloatEq(0.5F));
  EXPECT_THAT(config.eye_gaze_median_ms, Eq(250));
  EXPECT_THAT(config.eye_gaze_resting_ms, Eq(3000));
  EXPECT_THAT(config.eye_gaze_gap_ms, Eq(2000));
  EXPECT_THAT(config.eye_gaze_head_only_yaw_degrees, FloatEq(35.0F));
  EXPECT_THAT(config.calibration_min_head_travel_degrees, FloatEq(10.0F));
  EXPECT_THAT(config.calibration_min_axis_degrees, FloatEq(4.0F));
  EXPECT_THAT(config.calibration_min_spread_degrees, FloatEq(3.0F));
}

TEST(ParseConfigTest, ReadsEverySetting) {
  std::string error;
  const std::optional<Config> parsed = ParseConfig(
      "camera.frames_per_second = 30\n"
      "camera.device_id = continuity-iphone-15\n"
      "head_pose.enabled = false\n"
      "gestures.enabled=false\n"
      "  gestures.confidence_threshold =\t0.25  \n"
      "expressions.enabled = false\n"
      "expressions.smallest_rise = 0.1\n"
      "expressions.spreads = 4\n"
      "expressions.hold_ms = 150\n"
      "expressions.pose_degrees = 12\n"
      "expressions.pose_range = 5\n"
      "expressions.settle_ms = 500\n"
      "looking_away.enabled = false\n"
      "looking_away.degrees = 20\n"
      "looking_away.hold_ms = 1500\n"
      "screen_context.enabled = false\n"
      "screen_context.window_titles_enabled = true\n"
      "screen_context.other_screen_degrees = 5\n"
      "eye_gaze.horizontal_gain = 25\n"
      "eye_gaze.vertical_gain = 28\n"
      "eye_gaze.blink_threshold = 0.4\n"
      "eye_gaze.median_ms = 300\n"
      "eye_gaze.resting_ms = 2500\n"
      "eye_gaze.gap_ms = 1800\n"
      "eye_gaze.head_only_yaw_degrees = 40\n"
      "calibration.min_head_travel_degrees = 12\n"
      "calibration.min_axis_degrees = 5\n"
      "calibration.min_spread_degrees = 4\n",
      error);
  ASSERT_TRUE(parsed.has_value()) << error;
  const Config config = parsed.value_or(Config{});

  EXPECT_THAT(config.frames_per_second, Eq(30));
  EXPECT_THAT(config.camera_device_id, Eq("continuity-iphone-15"));
  EXPECT_FALSE(config.head_pose_enabled);
  EXPECT_FALSE(config.gestures_enabled);
  EXPECT_THAT(config.gesture_confidence_threshold, FloatEq(0.25F));
  EXPECT_FALSE(config.expressions_enabled);
  EXPECT_THAT(config.expressions_smallest_rise, FloatEq(0.1F));
  EXPECT_THAT(config.expressions_spreads, FloatEq(4.0F));
  EXPECT_THAT(config.expressions_hold_ms, Eq(150));
  EXPECT_THAT(config.expressions_pose_degrees, FloatEq(12.0F));
  EXPECT_THAT(config.expressions_pose_range, FloatEq(5.0F));
  EXPECT_THAT(config.expressions_settle_ms, Eq(500));
  EXPECT_FALSE(config.looking_away_enabled);
  EXPECT_THAT(config.looking_away_degrees, FloatEq(20.0F));
  EXPECT_THAT(config.looking_away_hold_ms, Eq(1500));
  EXPECT_FALSE(config.screen_context_enabled);
  EXPECT_TRUE(config.screen_context_window_titles_enabled);
  EXPECT_THAT(config.screen_context_other_screen_degrees, FloatEq(5.0F));
  EXPECT_THAT(config.eye_gaze_horizontal_gain, FloatEq(25.0F));
  EXPECT_THAT(config.eye_gaze_vertical_gain, FloatEq(28.0F));
  EXPECT_THAT(config.eye_gaze_blink_threshold, FloatEq(0.4F));
  EXPECT_THAT(config.eye_gaze_median_ms, Eq(300));
  EXPECT_THAT(config.eye_gaze_resting_ms, Eq(2500));
  EXPECT_THAT(config.eye_gaze_gap_ms, Eq(1800));
  EXPECT_THAT(config.eye_gaze_head_only_yaw_degrees, FloatEq(40.0F));
  EXPECT_THAT(config.calibration_min_head_travel_degrees, FloatEq(12.0F));
  EXPECT_THAT(config.calibration_min_axis_degrees, FloatEq(5.0F));
  EXPECT_THAT(config.calibration_min_spread_degrees, FloatEq(4.0F));
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

TEST(ParseConfigTest, ABlankCameraDeviceIdChoosesTheDefault) {
  std::string error;
  const std::optional<Config> parsed =
      ParseConfig("camera.device_id = \n", error);
  ASSERT_TRUE(parsed.has_value()) << error;
  const Config config = parsed.value_or(Config{});
  EXPECT_THAT(config.camera_device_id, Eq(""));
}

TEST(ParseConfigTest, RefusesASwitchOtherThanTrueOrFalse) {
  EXPECT_THAT(ErrorFor("head_pose.enabled = yes"),
              Eq("line 1: expected true or false"));
  EXPECT_THAT(ErrorFor("gestures.enabled = yes"),
              Eq("line 1: expected true or false"));
  EXPECT_THAT(ErrorFor("expressions.enabled = yes"),
              Eq("line 1: expected true or false"));
}

TEST(ParseConfigTest, RefusesAThresholdOutsideZeroToOne) {
  EXPECT_THAT(ErrorFor("gestures.confidence_threshold = -0.1"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("gestures.confidence_threshold = 1.5"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("gestures.confidence_threshold = high"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("gestures.confidence_threshold = 0.9x"),
              Eq("line 1: expected a number from 0 to 1"));
  // from_chars reads "nan", and NaN passes a < 0 || > 1 check.
  EXPECT_THAT(ErrorFor("gestures.confidence_threshold = nan"),
              Eq("line 1: expected a number from 0 to 1"));
}

TEST(ParseConfigTest, RefusesASmallestRiseOutsideZeroToOne) {
  EXPECT_THAT(ErrorFor("expressions.smallest_rise = -0.1"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("expressions.smallest_rise = 1.5"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("expressions.smallest_rise = small"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("expressions.smallest_rise = 0.1x"),
              Eq("line 1: expected a number from 0 to 1"));
  // from_chars reads "nan", and NaN passes a < 0 || > 1 check.
  EXPECT_THAT(ErrorFor("expressions.smallest_rise = nan"),
              Eq("line 1: expected a number from 0 to 1"));
}

TEST(ParseConfigTest, RefusesSpreadsOutsideZeroToOneHundred) {
  EXPECT_THAT(ErrorFor("expressions.spreads = -0.1"),
              Eq("line 1: expected a number from 0 to 100"));
  EXPECT_THAT(ErrorFor("expressions.spreads = 100.1"),
              Eq("line 1: expected a number from 0 to 100"));
  EXPECT_THAT(ErrorFor("expressions.spreads = wide"),
              Eq("line 1: expected a number from 0 to 100"));
  EXPECT_THAT(ErrorFor("expressions.spreads = 3x"),
              Eq("line 1: expected a number from 0 to 100"));
  // from_chars reads "nan", and NaN passes a < 0 || > 100 check.
  EXPECT_THAT(ErrorFor("expressions.spreads = nan"),
              Eq("line 1: expected a number from 0 to 100"));
}

TEST(ParseConfigTest, RefusesAHoldTimeOutsideZeroTo5000Ms) {
  EXPECT_THAT(ErrorFor("expressions.hold_ms = -1"),
              Eq("line 1: expected a whole number from 0 to 5000"));
  EXPECT_THAT(ErrorFor("expressions.hold_ms = 5001"),
              Eq("line 1: expected a whole number from 0 to 5000"));
  EXPECT_THAT(ErrorFor("expressions.hold_ms = 200.5"),
              Eq("line 1: expected a whole number from 0 to 5000"));
  EXPECT_THAT(ErrorFor("expressions.hold_ms = slow"),
              Eq("line 1: expected a whole number from 0 to 5000"));
}

TEST(ParseConfigTest, RefusesAPoseDegreesOutsideZeroToOneEighty) {
  EXPECT_THAT(ErrorFor("expressions.pose_degrees = -0.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("expressions.pose_degrees = 180.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("expressions.pose_degrees = wide"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("expressions.pose_degrees = 10x"),
              Eq("line 1: expected a number from 0 to 180"));
  // from_chars reads "nan", and NaN passes a < 0 || > 180 check.
  EXPECT_THAT(ErrorFor("expressions.pose_degrees = nan"),
              Eq("line 1: expected a number from 0 to 180"));
}

TEST(ParseConfigTest, RefusesAPoseRangeOutsideZeroToOneEighty) {
  EXPECT_THAT(ErrorFor("expressions.pose_range = -0.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("expressions.pose_range = 180.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("expressions.pose_range = wide"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("expressions.pose_range = 3x"),
              Eq("line 1: expected a number from 0 to 180"));
  // from_chars reads "nan", and NaN passes a < 0 || > 180 check.
  EXPECT_THAT(ErrorFor("expressions.pose_range = nan"),
              Eq("line 1: expected a number from 0 to 180"));
}

TEST(ParseConfigTest, RefusesASettleTimeOutsideZeroTo5000Ms) {
  EXPECT_THAT(ErrorFor("expressions.settle_ms = -1"),
              Eq("line 1: expected a whole number from 0 to 5000"));
  EXPECT_THAT(ErrorFor("expressions.settle_ms = 5001"),
              Eq("line 1: expected a whole number from 0 to 5000"));
  EXPECT_THAT(ErrorFor("expressions.settle_ms = 600.5"),
              Eq("line 1: expected a whole number from 0 to 5000"));
  EXPECT_THAT(ErrorFor("expressions.settle_ms = slow"),
              Eq("line 1: expected a whole number from 0 to 5000"));
}

TEST(ParseConfigTest, RefusesALookAwaySwitchThatIsNotTrueOrFalse) {
  EXPECT_THAT(ErrorFor("looking_away.enabled = 1"),
              Eq("line 1: expected true or false"));
}

TEST(ParseConfigTest, RefusesLookAwayDegreesOutsideZeroToOneEighty) {
  EXPECT_THAT(ErrorFor("looking_away.degrees = -0.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("looking_away.degrees = 180.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("looking_away.degrees = far"),
              Eq("line 1: expected a number from 0 to 180"));
  // from_chars reads "nan", and NaN passes a < 0 || > 180 check.
  EXPECT_THAT(ErrorFor("looking_away.degrees = nan"),
              Eq("line 1: expected a number from 0 to 180"));
}

TEST(ParseConfigTest, RefusesALookAwayHoldOutsideZeroTo10000Ms) {
  EXPECT_THAT(ErrorFor("looking_away.hold_ms = -1"),
              Eq("line 1: expected a whole number from 0 to 10000"));
  EXPECT_THAT(ErrorFor("looking_away.hold_ms = 10001"),
              Eq("line 1: expected a whole number from 0 to 10000"));
  EXPECT_THAT(ErrorFor("looking_away.hold_ms = 1.5"),
              Eq("line 1: expected a whole number from 0 to 10000"));
}

TEST(ParseConfigTest, RefusesAScreenContextSwitchThatIsNotTrueOrFalse) {
  EXPECT_THAT(ErrorFor("screen_context.enabled = 1"),
              Eq("line 1: expected true or false"));
  EXPECT_THAT(ErrorFor("screen_context.window_titles_enabled = 1"),
              Eq("line 1: expected true or false"));
}

TEST(ParseConfigTest, RefusesOtherScreenDegreesOutsideZeroToOneEighty) {
  EXPECT_THAT(ErrorFor("screen_context.other_screen_degrees = -0.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("screen_context.other_screen_degrees = 180.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("screen_context.other_screen_degrees = near"),
              Eq("line 1: expected a number from 0 to 180"));
  // from_chars reads "nan", and NaN passes a < 0 || > 180 check.
  EXPECT_THAT(ErrorFor("screen_context.other_screen_degrees = nan"),
              Eq("line 1: expected a number from 0 to 180"));
}

TEST(ParseConfigTest, RefusesAnEyeGazeGainOutsideZeroTo200) {
  EXPECT_THAT(ErrorFor("eye_gaze.horizontal_gain = -1"),
              Eq("line 1: expected a number from 0 to 200"));
  EXPECT_THAT(ErrorFor("eye_gaze.horizontal_gain = 200.1"),
              Eq("line 1: expected a number from 0 to 200"));
  EXPECT_THAT(ErrorFor("eye_gaze.vertical_gain = strong"),
              Eq("line 1: expected a number from 0 to 200"));
  // from_chars reads "nan", and NaN passes a < 0 || > 200 check.
  EXPECT_THAT(ErrorFor("eye_gaze.vertical_gain = nan"),
              Eq("line 1: expected a number from 0 to 200"));
}

TEST(ParseConfigTest, RefusesAnEyeGazeBlinkThresholdOutsideZeroToOne) {
  EXPECT_THAT(ErrorFor("eye_gaze.blink_threshold = -0.1"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("eye_gaze.blink_threshold = 1.1"),
              Eq("line 1: expected a number from 0 to 1"));
  EXPECT_THAT(ErrorFor("eye_gaze.blink_threshold = high"),
              Eq("line 1: expected a number from 0 to 1"));
  // from_chars reads "nan", and NaN passes a < 0 || > 1 check.
  EXPECT_THAT(ErrorFor("eye_gaze.blink_threshold = nan"),
              Eq("line 1: expected a number from 0 to 1"));
}

TEST(ParseConfigTest, RefusesAnEyeGazeMillisecondsOutsideZeroTo10000) {
  EXPECT_THAT(ErrorFor("eye_gaze.median_ms = -1"),
              Eq("line 1: expected a whole number from 0 to 10000"));
  EXPECT_THAT(ErrorFor("eye_gaze.resting_ms = 10001"),
              Eq("line 1: expected a whole number from 0 to 10000"));
  EXPECT_THAT(ErrorFor("eye_gaze.gap_ms = 1.5"),
              Eq("line 1: expected a whole number from 0 to 10000"));
}

TEST(ParseConfigTest, RefusesADegreesSettingOutsideZeroToOneEighty) {
  EXPECT_THAT(ErrorFor("eye_gaze.head_only_yaw_degrees = -0.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("calibration.min_head_travel_degrees = 180.1"),
              Eq("line 1: expected a number from 0 to 180"));
  EXPECT_THAT(ErrorFor("calibration.min_axis_degrees = far"),
              Eq("line 1: expected a number from 0 to 180"));
  // from_chars reads "nan", and NaN passes a < 0 || > 180 check.
  EXPECT_THAT(ErrorFor("calibration.min_spread_degrees = nan"),
              Eq("line 1: expected a number from 0 to 180"));
}

}  // namespace
}  // namespace facepalm
