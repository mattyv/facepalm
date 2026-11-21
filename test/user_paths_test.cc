#include "src/user_paths.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::Eq;

// The paths under `home`, failing the test when there are none.
UserPaths Paths(const char* home, Platform platform) {
  const std::optional<UserPaths> paths = PathsFor(home, platform);
  EXPECT_TRUE(paths.has_value());
  return paths.value_or(UserPaths{});
}

TEST(UserPathsTest, MacOsKeepsThemInApplicationSupport) {
  const UserPaths paths = Paths("/Users/person", Platform::kMacOs);

  EXPECT_THAT(paths.directory,
              Eq("/Users/person/Library/Application Support/Facepalm"));
  EXPECT_THAT(
      paths.socket,
      Eq("/Users/person/Library/Application Support/Facepalm/service.sock"));
  EXPECT_THAT(paths.clients,
              Eq("/Users/person/Library/Application Support/Facepalm/clients"));
  EXPECT_THAT(paths.status,
              Eq("/Users/person/Library/Application Support/Facepalm/status"));
  EXPECT_THAT(
      paths.calibration,
      Eq("/Users/person/Library/Application Support/Facepalm/calibration"));
}

TEST(UserPathsTest, LinuxKeepsThemInTheStateDirectory) {
  const UserPaths paths = Paths("/home/person/", Platform::kLinux);

  EXPECT_THAT(paths.directory, Eq("/home/person/.local/state/facepalm"));
  EXPECT_THAT(paths.socket,
              Eq("/home/person/.local/state/facepalm/service.sock"));
  EXPECT_THAT(paths.clients, Eq("/home/person/.local/state/facepalm/clients"));
  EXPECT_THAT(paths.status, Eq("/home/person/.local/state/facepalm/status"));
  EXPECT_THAT(paths.calibration,
              Eq("/home/person/.local/state/facepalm/calibration"));
}

TEST(UserPathsTest, TheRootCanBeAHome) {
  EXPECT_THAT(Paths("/", Platform::kLinux).directory,
              Eq("/.local/state/facepalm"));
}

TEST(UserPathsTest, NeedsAnAbsoluteHome) {
  EXPECT_THAT(PathsFor("", Platform::kLinux), Eq(std::nullopt));
  EXPECT_THAT(PathsFor("home/person", Platform::kMacOs), Eq(std::nullopt));
}

TEST(UserPathsTest, AClientsTokenIsNamedForIt) {
  EXPECT_THAT(ClientTokenPath(Paths("/h", Platform::kLinux), "mcp"),
              Eq("/h/.local/state/facepalm/clients/mcp"));
}

}  // namespace
}  // namespace facepalm
