#include "src/settings_document.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// GoogleTest assertions check each optional before access.
// NOLINTBEGIN(bugprone-unchecked-optional-access)
namespace facepalm {
namespace {
TEST(SettingsDocumentTest, PreservesOtherSettingsAndComments) {
  std::string error;
  const auto updated = UpdateSettingsDocument(
      "# Personal settings\n camera.frames_per_second = 12 # camera\n"
      "gestures.enabled = false # hands off\nexpressions.spreads = 5\n",
      {.frames_per_second = 20,
       .gestures_enabled = true,
       .expressions_enabled = false,
       .expression_spreads = "5"},
      error);
  ASSERT_TRUE(updated.has_value());
  EXPECT_THAT(updated.value(), testing::HasSubstr("# Personal settings\n"));
  EXPECT_THAT(updated.value(), testing::HasSubstr("# camera"));
  EXPECT_THAT(updated.value(), testing::HasSubstr("# hands off"));
  EXPECT_THAT(updated.value(), testing::HasSubstr("expressions.spreads = 5\n"));
  const auto config = ParseConfig(updated.value(), error);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config.value().frames_per_second, 20);
  EXPECT_TRUE(config.value().gestures_enabled);
  EXPECT_FALSE(config.value().expressions_enabled);
  EXPECT_EQ(config.value().expressions_spreads, 5);
}

TEST(SettingsDocumentTest, ReplacesUncommentedSettings) {
  std::string error;
  const auto updated = UpdateSettingsDocument(
      "head_pose.enabled = false\ngestures.enabled = true\nexpressions.enabled "
      "= false\n",
      {.frames_per_second = 15,
       .gestures_enabled = false,
       .expressions_enabled = true},
      error);
  ASSERT_TRUE(updated.has_value());
  const auto config = ParseConfig(updated.value(), error);
  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config.value().gestures_enabled);
  EXPECT_TRUE(config.value().expressions_enabled);
}

TEST(SettingsDocumentTest, UpdatesAndValidatesAdvancedTuning) {
  std::string error;
  MenuSettings settings;
  settings.gesture_confidence = "0.75";
  settings.expression_rise = "0.08";
  settings.expression_spreads = "4.2";
  settings.expression_hold_ms = "350";
  settings.expression_pose_degrees = "12";
  settings.expression_pose_range = "2.5";
  settings.expression_settle_ms = "900";
  const auto updated = UpdateSettingsDocument("# tuning\n", settings, error);
  ASSERT_TRUE(updated.has_value());
  const auto config = ParseConfig(updated.value(), error);
  ASSERT_TRUE(config.has_value());
  EXPECT_FLOAT_EQ(config.value().gesture_confidence_threshold, 0.75F);
  EXPECT_FLOAT_EQ(config.value().expressions_smallest_rise, 0.08F);
  EXPECT_FLOAT_EQ(config.value().expressions_spreads, 4.2F);
  EXPECT_EQ(config.value().expressions_hold_ms, 350);
  EXPECT_FLOAT_EQ(config.value().expressions_pose_degrees, 12);
  EXPECT_FLOAT_EQ(config.value().expressions_pose_range, 2.5F);
  EXPECT_EQ(config.value().expressions_settle_ms, 900);
  settings.gesture_confidence = "1.1";
  EXPECT_FALSE(UpdateSettingsDocument("", settings, error));
  settings.gesture_confidence = "0.5\nhead_pose.enabled = false";
  EXPECT_FALSE(UpdateSettingsDocument("", settings, error));
}

TEST(SettingsDocumentTest, RejectsInvalidExistingDocumentAndFrameRate) {
  std::string error;
  EXPECT_FALSE(UpdateSettingsDocument("unknown.setting = true\n", {}, error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(UpdateSettingsDocument("", {.frames_per_second = 0}, error));
  EXPECT_FALSE(UpdateSettingsDocument("", {.frames_per_second = 61}, error));
}

TEST(SettingsDocumentTest, RejectsEmbeddedNulWithoutDroppingTrailingContent) {
  std::string error;
  std::string original = "# valid comment";
  original.push_back('\0');
  original += " hidden comment\ngestures.enabled = false\n";
  EXPECT_FALSE(UpdateSettingsDocument(original, {}, error));
  EXPECT_FALSE(error.empty());
}

TEST(SettingsDocumentTest, SupportsEmptyFileAndMissingFinalNewline) {
  std::string error;
  const auto empty = UpdateSettingsDocument("", {}, error);
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(ParseConfig(empty.value(), error));
  const auto without_newline = UpdateSettingsDocument("# comment", {}, error);
  ASSERT_TRUE(without_newline.has_value());
  EXPECT_THAT(without_newline.value(), testing::StartsWith("# comment\n"));
  EXPECT_TRUE(ParseConfig(without_newline.value(), error));
}
}  // namespace
}  // namespace facepalm
// NOLINTEND(bugprone-unchecked-optional-access)
