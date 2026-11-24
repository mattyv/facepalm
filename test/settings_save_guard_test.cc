#include "src/settings_save_guard.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::IsFalse;
using ::testing::IsTrue;

TEST(SettingsFileTooLargeTest, AcceptsExactlyTheLimit) {
  EXPECT_THAT(SettingsFileTooLarge(kMaxSettingsFileBytes), IsFalse());
}

TEST(SettingsFileTooLargeTest, RefusesOneByteOverTheLimit) {
  EXPECT_THAT(SettingsFileTooLarge(kMaxSettingsFileBytes + 1), IsTrue());
}

TEST(SettingsFileTooLargeTest, AcceptsAnEmptyFile) {
  EXPECT_THAT(SettingsFileTooLarge(0), IsFalse());
}

TEST(SettingsFileUnchangedTest, TrueWhenBothAreMissing) {
  EXPECT_THAT(SettingsFileUnchanged("", true, "", true), IsTrue());
}

TEST(SettingsFileUnchangedTest, TrueWhenTheBytesStillMatch) {
  EXPECT_THAT(SettingsFileUnchanged("hello", false, "hello", false), IsTrue());
}

TEST(SettingsFileUnchangedTest, FalseWhenMissingnessDiffers) {
  EXPECT_THAT(SettingsFileUnchanged("", true, "hello", false), IsFalse());
  EXPECT_THAT(SettingsFileUnchanged("hello", false, "", true), IsFalse());
}

TEST(SettingsFileUnchangedTest, FalseWhenTheBytesDiffer) {
  EXPECT_THAT(SettingsFileUnchanged("hello", false, "goodbye", false),
              IsFalse());
}

}  // namespace
}  // namespace facepalm
