#include "src/daemon_options.h"

#include <optional>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/user_paths.h"

namespace facepalm {
namespace {

using ::testing::Eq;
using Command = DaemonOptions::Command;

// The person's paths, for the defaults.
std::optional<UserPaths> Home() {
  return UserPaths{.directory = "/h/fp",
                   .socket = "/h/fp/service.sock",
                   .clients = "/h/fp/c",
                   .status = "/h/fp/status",
                   .calibration = "/h/fp/calibration",
                   .preview_request = "/h/fp/preview_request",
                   .preview_frame = "/h/fp/preview_frame.jpg"};
}

std::optional<DaemonOptions> Parse(const std::vector<const char*>& arguments,
                                   const std::optional<UserPaths>& defaults) {
  return ParseDaemonOptions(arguments, defaults);
}

std::optional<DaemonOptions> Parse(const std::vector<const char*>& arguments) {
  return Parse(arguments, Home());
}

// The options `arguments` give, failing the test when they give none.
DaemonOptions Parsed(const std::vector<const char*>& arguments,
                     const std::optional<UserPaths>& defaults) {
  const std::optional<DaemonOptions> options = Parse(arguments, defaults);
  EXPECT_TRUE(options.has_value());
  return options.value_or(DaemonOptions{});
}

TEST(DaemonOptionsTest, RunsWithTheDefaultPaths) {
  const DaemonOptions options =
      Parsed({"--consent", "--model", "m.task"}, Home());

  EXPECT_THAT(options.command, Eq(Command::kRun));
  EXPECT_TRUE(options.consent);
  EXPECT_THAT(options.model, Eq("m.task"));
  EXPECT_THAT(options.socket, Eq("/h/fp/service.sock"));
  EXPECT_THAT(options.clients, Eq("/h/fp/c"));
  EXPECT_THAT(options.config, Eq(""));
  EXPECT_THAT(options.feelings_file, Eq(""));
  EXPECT_THAT(options.status, Eq("/h/fp/status"));
}

TEST(DaemonOptionsTest, RunsWithEveryPathGiven) {
  const DaemonOptions options = Parsed(
      {"--model", "m", "--socket", "s", "--clients", "c", "--config", "f.conf",
       "--feelings-file", "f.counts", "--status", "u", "--consent"},
      std::nullopt);

  EXPECT_THAT(options.socket, Eq("s"));
  EXPECT_THAT(options.clients, Eq("c"));
  EXPECT_THAT(options.config, Eq("f.conf"));
  EXPECT_THAT(options.feelings_file, Eq("f.counts"));
  EXPECT_THAT(options.status, Eq("u"));
}

TEST(DaemonOptionsTest, AGivenSocketOverridesTheDefault) {
  EXPECT_THAT(
      Parsed({"--consent", "--model", "m", "--socket", "s"}, Home()).socket,
      Eq("s"));
}

TEST(DaemonOptionsTest, AGivenStatusPathOverridesTheDefault) {
  EXPECT_THAT(
      Parsed({"--consent", "--model", "m", "--status", "u"}, Home()).status,
      Eq("u"));
}

TEST(DaemonOptionsTest, RunningWithNoHomeHasNoStatusFileByDefault) {
  EXPECT_THAT(
      Parsed({"--consent", "--model", "m", "--socket", "s", "--clients", "c"},
             std::nullopt)
          .status,
      Eq(""));
}

TEST(DaemonOptionsTest, RunningNeedsConsentAModelAndPaths) {
  EXPECT_FALSE(Parse({"--model", "m"}));
  EXPECT_FALSE(Parse({"--consent"}));
  EXPECT_FALSE(Parse({"--consent", "--model", "m"}, std::nullopt));
  EXPECT_FALSE(
      Parse({"--consent", "--model", "m", "--socket", "s"}, std::nullopt));
  EXPECT_FALSE(
      Parse({"--consent", "--model", "m", "--clients", "c"}, std::nullopt));
}

TEST(DaemonOptionsTest, RefusesRepeatedUnknownAndValuelessOptions) {
  EXPECT_FALSE(Parse({"--consent", "--consent", "--model", "m"}));
  EXPECT_FALSE(Parse({"--consent", "--model", "m", "--model", "n"}));
  EXPECT_FALSE(Parse({"--consent", "--model", "m", "--camera", "x"}));
  EXPECT_FALSE(Parse({"--consent", "--model"}));
  EXPECT_FALSE(Parse({"--consent", "--model", ""}));
}

TEST(DaemonOptionsTest, ApprovesAndRevokesAClient) {
  const DaemonOptions approve = Parsed({"approve", "mcp"}, Home());
  EXPECT_THAT(approve.command, Eq(Command::kApprove));
  EXPECT_THAT(approve.client, Eq("mcp"));
  EXPECT_THAT(approve.clients, Eq("/h/fp/c"));

  const DaemonOptions revoke =
      Parsed({"revoke", "ai-pair", "--clients", "d"}, Home());
  EXPECT_THAT(revoke.command, Eq(Command::kRevoke));
  EXPECT_THAT(revoke.client, Eq("ai-pair"));
  EXPECT_THAT(revoke.clients, Eq("d"));
}

TEST(DaemonOptionsTest, ApprovingTakesANameAndOnlyTheClientsDirectory) {
  EXPECT_FALSE(Parse({"approve"}));
  EXPECT_FALSE(Parse({"approve", ""}));
  EXPECT_FALSE(Parse({"approve", "mcp", "--consent"}));
  EXPECT_FALSE(Parse({"revoke", "mcp", "--model", "m"}));
  EXPECT_FALSE(Parse({"approve", "mcp"}, std::nullopt));
  EXPECT_FALSE(Parse({}));
}

}  // namespace
}  // namespace facepalm
