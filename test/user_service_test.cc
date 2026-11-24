#include "src/user_service.h"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/user_paths.h"

// The RunsOnlyWhenOn, FileMatchesMenu, TurnOffStops and OnRuns tests stand
// for those properties of specs/UserService.tla.

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Not;
using ::testing::Return;

using Command = std::vector<std::string>;

constexpr std::string_view kPlist =
    "/Users/p/Library/LaunchAgents/com.mattyv.facepalm.daemon.plist";
constexpr std::string_view kUnit =
    "/home/p/.config/systemd/user/facepalm.service";

class MockServiceIo : public ServiceIo {
 public:
  MOCK_METHOD(bool, WriteFile, (const std::string&, std::string_view),
              (override));
  MOCK_METHOD(bool, RemoveFile, (const std::string&), (override));
  MOCK_METHOD(bool, Exists, (const std::string&), (override));
  MOCK_METHOD(int, Run, (const Command&), (override));
  MOCK_METHOD(uid_t, User, (), (override));
};

// The bundle's daemon and model.
ServiceProgram Program() {
  return {.daemon = "/App/MacOS/facepalm-daemon",
          .model = "/App/model/face_landmarker.task",
          .config = ""};
}

TEST(LaunchAgentPlistTest, RunsTheDaemonWithConsentAndRestartsOnFailure) {
  const std::string plist =
      LaunchAgentPlist(Program(), "/Users/p/Library/Logs/facepalm-daemon.log");

  EXPECT_THAT(plist, HasSubstr("<key>Label</key>\n  <string>"
                               "com.mattyv.facepalm.daemon</string>"));
  EXPECT_THAT(plist,
              HasSubstr("    <string>/App/MacOS/facepalm-daemon</string>\n"
                        "    <string>--consent</string>\n"
                        "    <string>--model</string>\n"
                        "    <string>/App/model/face_landmarker.task</string>\n"
                        "  </array>"));
  EXPECT_THAT(plist, HasSubstr("<key>RunAtLoad</key>\n  <true/>"));
  EXPECT_THAT(plist, HasSubstr("<key>KeepAlive</key>\n  <dict>\n"
                               "    <key>SuccessfulExit</key>\n    <false/>"));
  EXPECT_THAT(plist, HasSubstr("<key>AssociatedBundleIdentifiers</key>\n"
                               "  <string>com.mattyv.facepalm</string>"));
  EXPECT_THAT(plist, HasSubstr("<string>/Users/p/Library/Logs/"
                               "facepalm-daemon.log</string>"));
  EXPECT_THAT(plist, Not(HasSubstr("--config")));
  EXPECT_THAT(plist, HasSubstr("<!DOCTYPE plist"));
}

TEST(LaunchAgentPlistTest, EscapesItsStringsAndPassesTheConfig) {
  const std::string plist = LaunchAgentPlist(
      {.daemon = "/A&B/<d>", .model = "/m\"'", .config = "/c.conf"}, "/l");

  EXPECT_THAT(plist, HasSubstr("<string>/A&amp;B/&lt;d&gt;</string>"));
  EXPECT_THAT(plist, HasSubstr("<string>/m&quot;&apos;</string>"));
  EXPECT_THAT(plist, HasSubstr("    <string>--config</string>\n"
                               "    <string>/c.conf</string>\n"));
}

TEST(SystemdUnitTest, RunsTheDaemonWithConsentAndRestartsOnFailure) {
  const std::string unit = SystemdUnit(
      {.daemon = "/opt/fp/bin/facepalm-daemon", .model = "/m", .config = ""});

  EXPECT_THAT(unit, HasSubstr("[Service]\nExecStart=\"/opt/fp/bin/"
                              "facepalm-daemon\" \"--consent\" \"--model\" "
                              "\"/m\"\nRestart=on-failure\nRestartSec=5\n"));
  EXPECT_THAT(unit,
              HasSubstr("StartLimitIntervalSec=30\nStartLimitBurst=10\n"));
  EXPECT_THAT(unit, HasSubstr("[Install]\nWantedBy=default.target\n"));
  EXPECT_THAT(unit, Not(HasSubstr("--config")));
}

TEST(SystemdUnitTest, QuotesItsArgumentsAndPassesTheConfig) {
  const std::string unit = SystemdUnit(
      {.daemon = "/a b/d", .model = "/50%\\\"$HOME", .config = "/c"});

  EXPECT_THAT(unit, HasSubstr("ExecStart=\"/a b/d\" \"--consent\" "
                              "\"--model\" \"/50%%\\\\\\\"$$HOME\" "
                              "\"--config\" \"/c\"\n"));
}

class UserServiceTest : public ::testing::Test {
 protected:
  UserService Service(Platform platform) {
    return {platform, platform == Platform::kMacOs ? "/Users/p" : "/home/p",
            approve_.AsStdFunction(), io_};
  }

  ::testing::MockFunction<std::optional<std::string>(std::string_view,
                                                     std::string&)>
      approve_;
  ::testing::StrictMock<MockServiceIo> io_;
};

using FileMatchesMenuTest = UserServiceTest;

TEST_F(FileMatchesMenuTest, FacepalmIsOnWhileItsFileIsThere) {
  EXPECT_CALL(io_, Exists(Eq(std::string(kPlist)))).WillOnce(Return(true));
  EXPECT_TRUE(Service(Platform::kMacOs).IsOn());
  EXPECT_CALL(io_, Exists(Eq(std::string(kUnit)))).WillOnce(Return(false));
  EXPECT_FALSE(Service(Platform::kLinux).IsOn());
}

TEST_F(FileMatchesMenuTest, AServiceThatWillNotLoadLeavesNoFile) {
  EXPECT_CALL(approve_, Call(Eq("mcp"), _)).WillOnce(Return("/token"));
  EXPECT_CALL(io_, User).WillRepeatedly(Return(501));
  EXPECT_CALL(io_, WriteFile(Eq(std::string(kPlist)), _))
      .WillOnce(Return(true));
  EXPECT_CALL(io_, Run).WillOnce(Return(3)).WillOnce(Return(5));
  EXPECT_CALL(io_, RemoveFile(Eq(std::string(kPlist)))).WillOnce(Return(true));
  std::string error;

  EXPECT_FALSE(Service(Platform::kMacOs).TurnOn(Program(), error));
  EXPECT_THAT(error, Eq("launchctl could not load " + std::string(kPlist)));
}

using OnRunsTest = UserServiceTest;

TEST_F(OnRunsTest, TurningOnApprovesTheAdapterWritesTheAgentAndLoadsIt) {
  EXPECT_CALL(io_, User).WillRepeatedly(Return(501));
  const InSequence order;
  EXPECT_CALL(approve_, Call(Eq("mcp"), _)).WillOnce(Return("/token"));
  EXPECT_CALL(
      io_, WriteFile(Eq(std::string(kPlist)), HasSubstr("/Users/p/Library/Logs/"
                                                        "facepalm-daemon.log")))
      .WillOnce(Return(true));
  EXPECT_CALL(io_, Run(ElementsAre("launchctl", "bootout",
                                   "gui/501/com.mattyv.facepalm.daemon")))
      .WillOnce(Return(3));
  EXPECT_CALL(io_, Run(ElementsAre("launchctl", "bootstrap", "gui/501",
                                   std::string(kPlist))))
      .WillOnce(Return(0));
  std::string error;

  EXPECT_TRUE(Service(Platform::kMacOs).TurnOn(Program(), error));
}

TEST_F(OnRunsTest, TurningOnWritesTheUnitAndEnablesIt) {
  const InSequence order;
  EXPECT_CALL(approve_, Call).WillOnce(Return("/token"));
  EXPECT_CALL(io_, WriteFile(Eq(std::string(kUnit)), HasSubstr("[Service]")))
      .WillOnce(Return(true));
  EXPECT_CALL(io_, Run(ElementsAre("systemctl", "--user", "daemon-reload")))
      .WillOnce(Return(0));
  EXPECT_CALL(io_, Run(ElementsAre("systemctl", "--user", "enable", "--now",
                                   "facepalm.service")))
      .WillOnce(Return(0));
  std::string error;

  EXPECT_TRUE(Service(Platform::kLinux).TurnOn(Program(), error));
}

TEST_F(OnRunsTest, TurningOnSaysWhichStepFailed) {
  std::string error;
  EXPECT_CALL(approve_, Call).WillOnce([](std::string_view, std::string& why) {
    why = "cannot make /c";
    return std::nullopt;
  });
  EXPECT_FALSE(Service(Platform::kLinux).TurnOn(Program(), error));
  EXPECT_THAT(error, Eq("cannot make /c"));

  EXPECT_CALL(approve_, Call).WillRepeatedly(Return("/token"));
  EXPECT_CALL(io_, WriteFile).WillOnce(Return(false));
  EXPECT_FALSE(Service(Platform::kLinux).TurnOn(Program(), error));
  EXPECT_THAT(error, Eq("cannot write " + std::string(kUnit)));
}

TEST_F(OnRunsTest, TurningOnSaysWhichCommandFailed) {
  std::string error;
  EXPECT_CALL(approve_, Call).WillRepeatedly(Return("/token"));
  EXPECT_CALL(io_, WriteFile).WillRepeatedly(Return(true));
  EXPECT_CALL(io_, Run(ElementsAre("systemctl", "--user", "daemon-reload")))
      .WillOnce(Return(1))
      .WillOnce(Return(0));
  EXPECT_CALL(io_, RemoveFile(Eq(std::string(kUnit))))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(Service(Platform::kLinux).TurnOn(Program(), error));
  EXPECT_THAT(error, Eq("systemctl could not load " + std::string(kUnit)));

  EXPECT_CALL(io_, Run(ElementsAre("systemctl", "--user", "enable", "--now",
                                   "facepalm.service")))
      .WillOnce(Return(1));
  EXPECT_FALSE(Service(Platform::kLinux).TurnOn(Program(), error));
  EXPECT_THAT(error, Eq("systemctl could not load " + std::string(kUnit)));
}

using TurnOffStopsTest = UserServiceTest;

TEST_F(TurnOffStopsTest, TurningOffUnloadsTheAgentThenRemovesIt) {
  EXPECT_CALL(io_, User).WillRepeatedly(Return(501));
  const InSequence order;
  EXPECT_CALL(io_, Run(ElementsAre("launchctl", "bootout",
                                   "gui/501/com.mattyv.facepalm.daemon")))
      .WillOnce(Return(0));
  EXPECT_CALL(io_, RemoveFile(Eq(std::string(kPlist)))).WillOnce(Return(true));
  std::string error;

  EXPECT_TRUE(Service(Platform::kMacOs).TurnOff(error));
}

TEST_F(TurnOffStopsTest, TurningOffDisablesTheUnitThenRemovesIt) {
  const InSequence order;
  EXPECT_CALL(io_, Run(ElementsAre("systemctl", "--user", "disable", "--now",
                                   "facepalm.service")))
      .WillOnce(Return(0));
  EXPECT_CALL(io_, RemoveFile(Eq(std::string(kUnit)))).WillOnce(Return(true));
  EXPECT_CALL(io_, Run(ElementsAre("systemctl", "--user", "daemon-reload")))
      .WillOnce(Return(0));
  std::string error;

  EXPECT_TRUE(Service(Platform::kLinux).TurnOff(error));
}

TEST_F(TurnOffStopsTest, AnAgentNotLoadedIsAlreadyOff) {
  EXPECT_CALL(io_, User).WillRepeatedly(Return(501));
  EXPECT_CALL(io_, Run(ElementsAre("launchctl", "bootout", _)))
      .WillOnce(Return(3));
  EXPECT_CALL(io_, Run(ElementsAre("launchctl", "print",
                                   "gui/501/com.mattyv.facepalm.daemon")))
      .WillOnce(Return(113));
  EXPECT_CALL(io_, RemoveFile).WillOnce(Return(true));
  std::string error;

  EXPECT_TRUE(Service(Platform::kMacOs).TurnOff(error));
}

using RunsOnlyWhenOnTest = UserServiceTest;

TEST_F(RunsOnlyWhenOnTest, AServiceThatWillNotStopStaysOn) {
  EXPECT_CALL(io_, Run(ElementsAre("systemctl", "--user", "disable", "--now",
                                   "facepalm.service")))
      .WillOnce(Return(1));
  EXPECT_CALL(io_, Run(ElementsAre("systemctl", "--user", "is-active",
                                   "--quiet", "facepalm.service")))
      .WillOnce(Return(0));
  std::string error;

  EXPECT_FALSE(Service(Platform::kLinux).TurnOff(error));
  EXPECT_THAT(error, Eq("systemctl could not stop facepalm"));
}

TEST_F(RunsOnlyWhenOnTest, AFileThatWillNotGoIsReported) {
  EXPECT_CALL(io_, Run).WillOnce(Return(0));
  EXPECT_CALL(io_, RemoveFile).WillOnce(Return(false));
  std::string error;

  EXPECT_FALSE(Service(Platform::kLinux).TurnOff(error));
  EXPECT_THAT(error, Eq("cannot remove " + std::string(kUnit)));
}

TEST(FindProgramTest, TakesTheFirstPrefixWithTheDaemonAndTheModel) {
  ::testing::NiceMock<MockServiceIo> io;
  ON_CALL(io, Exists).WillByDefault(Return(false));
  ON_CALL(io, Exists(Eq("/b/bin/facepalm-daemon"))).WillByDefault(Return(true));
  ON_CALL(io, Exists(Eq("/b/share/facepalm/model/face_landmarker.task")))
      .WillByDefault(Return(true));
  ON_CALL(io, Exists(Eq("/a/bin/facepalm-daemon"))).WillByDefault(Return(true));
  ON_CALL(io, Exists(Eq("/c.conf"))).WillByDefault(Return(true));

  const std::optional<ServiceProgram> found =
      FindProgram({"/z", "/a", "/b"}, "/c.conf", io);

  ASSERT_TRUE(found.has_value());
  const ServiceProgram program = found.value_or(ServiceProgram{});
  EXPECT_THAT(program.daemon, Eq("/b/bin/facepalm-daemon"));
  EXPECT_THAT(program.model,
              Eq("/b/share/facepalm/model/face_landmarker.task"));
  EXPECT_THAT(program.config, Eq("/c.conf"));
  EXPECT_THAT(FindProgram({"/b"}, "/missing.conf", io)
                  .value_or(ServiceProgram{})
                  .config,
              Eq(""));
  EXPECT_THAT(FindProgram({"/a"}, "", io), Eq(std::nullopt));
}

// The OS's calls, on a temporary directory the test removes.
class PosixServiceIoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::array<char, 32> path = {"/tmp/fp-service-XXXXXX"};
    ASSERT_NE(mkdtemp(path.data()), nullptr);
    root_ = path.data();
  }
  void TearDown() override {
    std::filesystem::permissions(root_, std::filesystem::perms::owner_all);
    std::filesystem::remove_all(root_);
  }

  std::string root_;
  std::unique_ptr<ServiceIo> io_ = PosixServiceIo();
};

TEST_F(PosixServiceIoTest, WritesReplacesAndRemovesAFile) {
  const std::string file = root_ + "/a/b/unit";
  ASSERT_TRUE(io_->WriteFile(file, "one"));
  ASSERT_TRUE(io_->WriteFile(file, "two"));
  std::stringstream contents;
  contents << std::ifstream(file).rdbuf();
  EXPECT_THAT(contents.str(), Eq("two"));
  EXPECT_TRUE(io_->Exists(file));

  EXPECT_TRUE(io_->RemoveFile(file));
  EXPECT_TRUE(io_->RemoveFile(file));
  EXPECT_FALSE(io_->Exists(file));
}

TEST_F(PosixServiceIoTest, ReportsWhatItCannotDo) {
  ASSERT_TRUE(io_->WriteFile(root_ + "/file", ""));
  EXPECT_FALSE(io_->WriteFile(root_ + "/file/below", ""));
  std::filesystem::create_directories(root_ + "/dir/full");
  EXPECT_FALSE(io_->WriteFile(root_ + "/dir", ""));
  EXPECT_FALSE(io_->RemoveFile(root_ + "/dir"));
  std::filesystem::create_directories(root_ + "/locked");
  std::filesystem::permissions(
      root_ + "/locked",
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
  EXPECT_FALSE(io_->WriteFile(root_ + "/locked/unit", ""));
  std::filesystem::permissions(root_ + "/locked",
                               std::filesystem::perms::owner_all);
}

TEST_F(PosixServiceIoTest, RunsACommandAndGivesItsExitStatus) {
  EXPECT_THAT(io_->Run({"true"}), Eq(0));
  EXPECT_THAT(io_->Run({"sh", "-c", "exit 3"}), Eq(3));
  EXPECT_THAT(io_->Run({"sh", "-c", "kill -9 $$"}), Eq(-1));
  EXPECT_THAT(io_->Run({root_ + "/no-such-command"}), Eq(-1));
  EXPECT_THAT(io_->User(), Eq(getuid()));
}

}  // namespace
}  // namespace facepalm
