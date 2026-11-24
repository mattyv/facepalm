#include "src/settings_config_path.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::Eq;

TEST(SettingsConfigPathTest, UsesXdgConfigHomeWhenGiven) {
  EXPECT_THAT(SettingsConfigPath("/custom/config", "/home/user"),
              Eq("/custom/config/facepalm/facepalm.conf"));
}

TEST(SettingsConfigPathTest, FallsBackToHomeDotConfigWhenXdgIsEmpty) {
  EXPECT_THAT(SettingsConfigPath("", "/home/user"),
              Eq("/home/user/.config/facepalm/facepalm.conf"));
}

TEST(SettingsConfigPathTest, StripsATrailingSlashFromXdgConfigHome) {
  EXPECT_THAT(SettingsConfigPath("/custom/config/", "/home/user"),
              Eq("/custom/config/facepalm/facepalm.conf"));
}

TEST(SettingsConfigPathTest, StripsMultipleTrailingSlashes) {
  EXPECT_THAT(SettingsConfigPath("/custom/config///", "/home/user"),
              Eq("/custom/config/facepalm/facepalm.conf"));
}

TEST(SettingsConfigPathTest, StripsDownToAnEmptyBaseWhenXdgIsJustASlash) {
  EXPECT_THAT(SettingsConfigPath("/", "/home/user"),
              Eq("/facepalm/facepalm.conf"));
}

}  // namespace
}  // namespace facepalm
