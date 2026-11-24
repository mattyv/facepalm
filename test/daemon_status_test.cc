#include "src/daemon_status.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Each test stands for the specs/CameraStatus.tla action or property it is
// named for.

namespace facepalm {
namespace {

using ::testing::IsEmpty;
using ::testing::Not;

// A pid nothing uses any more (#184, ReadCameraStatus's own ESRCH case):
// forks a child that exits at once and reaps it, so the pid is free the
// moment this returns. The tiny window before the OS could reuse it is
// the same one any such check lives with (specs/CameraStatus.tla's
// header: pid reuse is the real reason the clean-shutdown write still
// matters too, not something this model goes as far as simulating).
std::int64_t DeadPid() {
  const pid_t child = ::fork();
  if (child == 0) {
    ::_exit(0);
  }
  int status = 0;
  ::waitpid(child, &status, 0);
  return child;
}

class DaemonStatusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() /
                 std::filesystem::path("facepalm-daemon-status-test-" +
                                       std::to_string(::getpid()));
    std::filesystem::create_directory(directory_);
    path_ = (directory_ / "status").string();
  }
  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::filesystem::path directory_;
  std::string path_;
};

using PublishTest = DaemonStatusTest;

// Also GuardReadChecksAlive's own TRUE case, by the branch that is
// almost always the one taken: the pid this test's own process just
// wrote is this very process, which is certainly still running.
TEST_F(PublishTest, WritesOn) {
  std::string error;
  EXPECT_TRUE(PublishCameraStatus(path_, true, error));
  EXPECT_THAT(error, IsEmpty());
  EXPECT_TRUE(ReadCameraStatus(path_));
}

TEST_F(PublishTest, WritesOff) {
  std::string error;
  EXPECT_TRUE(PublishCameraStatus(path_, false, error));
  EXPECT_FALSE(ReadCameraStatus(path_));
}

TEST_F(PublishTest, ReplacesWhatWasThereAtomically) {
  std::string error;
  ASSERT_TRUE(PublishCameraStatus(path_, true, error));
  ASSERT_TRUE(PublishCameraStatus(path_, false, error));

  EXPECT_FALSE(ReadCameraStatus(path_));
  EXPECT_FALSE(std::filesystem::exists(path_ + ".tmp"));
}

TEST_F(PublishTest, FailsWhenTheDirectoryIsGone) {
  std::string error;
  EXPECT_FALSE(PublishCameraStatus((directory_ / "missing" / "status").string(),
                                   true, error));
  EXPECT_THAT(error, Not(IsEmpty()));
}

TEST_F(PublishTest, FailsWhenThePathIsADirectory) {
  // The temporary file can be written, but renaming it over a directory
  // (EISDIR) cannot: a real rename failure, not a mocked one.
  ASSERT_EQ(::mkdir(path_.c_str(), 0700), 0);
  std::string error;
  EXPECT_FALSE(PublishCameraStatus(path_, true, error));
  EXPECT_THAT(error, Not(IsEmpty()));
}

void ExpectRetryHealth(const DaemonHealth& health) {
  EXPECT_EQ(health.phase, ServicePhase::kRetrying);
  EXPECT_EQ(health.reason, ServiceReason::kPermissionDenied);
  EXPECT_EQ(health.detail, "open denied /dev/video0");
  EXPECT_EQ(health.pid, ::getpid());
  EXPECT_TRUE(health.alive);
}

TEST_F(DaemonStatusTest, HealthRoundTripsAndPreservesLegacyCameraStatus) {
  std::string error;
  ASSERT_TRUE(PublishDaemonHealth(path_, true, ServicePhase::kRetrying,
                                  ServiceReason::kPermissionDenied,
                                  "open denied\n/dev/video0", error));
  EXPECT_TRUE(ReadCameraStatus(path_));
  const auto health = ReadDaemonHealth(path_);
  if (!health.has_value()) {
    FAIL() << "Expected health to be present";
  }
  ExpectRetryHealth(health.value());
}

TEST_F(DaemonStatusTest, DeadFailedHealthRemainsAvailableAsHistorical) {
  std::ofstream file(path_);
  file << "off\n" << DeadPid() << "\nhealth-v1\n4\n5\nfailed\n";
  file.close();
  const auto health = ReadDaemonHealth(path_);
  if (!health.has_value()) {
    FAIL() << "Expected health to be present";
  }
  EXPECT_EQ(health.value().phase, ServicePhase::kFailed);
  EXPECT_EQ(health.value().reason, ServiceReason::kStartFailed);
  EXPECT_FALSE(health.value().alive);
}

TEST_F(DaemonStatusTest, HealthRejectsUnknownVersionAndMalformedRecords) {
  for (const char* record :
       {"off\n1\nhealth-v2\n0\n0\n\n", "off\n1\nhealth-v1\n99\n0\n\n",
        "off\n1\nhealth-v1\n0\n7\n\n", "off\n1x\nhealth-v1\n0\n0\n\n",
        "off\n1\nhealth-v1\n0\n0\n"}) {
    std::ofstream file(path_);
    file << record;
    file.close();
    EXPECT_FALSE(ReadDaemonHealth(path_).has_value()) << record;
  }
}

TEST_F(DaemonStatusTest, HealthBoundsDetailsAndRejectsOversizedRecords) {
  std::string error;
  ASSERT_TRUE(PublishDaemonHealth(path_, false, ServicePhase::kFailed,
                                  ServiceReason::kQueryFailed,
                                  std::string(300, 'a'), error));
  const auto health = ReadDaemonHealth(path_);
  if (!health.has_value()) {
    FAIL() << "Expected health to be present";
  }
  EXPECT_EQ(health.value().detail, std::string(256, 'a'));
  std::ofstream file(path_);
  file << "off\n1\nhealth-v1\n4\n3\n" << std::string(2000, 'a');
  file.close();
  EXPECT_FALSE(ReadDaemonHealth(path_).has_value());
}

TEST_F(DaemonStatusTest, HealthRejectsMalformedFieldsAndExtraData) {
  const std::string prefix = "off\n1\nhealth-v1\n";
  const std::vector<std::string> records = {
      "sideways\n1\nhealth-v1\n0\n0\n\n",
      prefix + "0\n0\n\nextra",
      "off\n0\nhealth-v1\n0\n0\n\n",
      "off\n999999999999\nhealth-v1\n0\n0\n\n",
      "off\ninvalid\nhealth-v1\n0\n0\n\n",
      prefix + "bad\n0\n\n",
      prefix + "-1\n0\n\n",
      prefix + "0\nbad\n\n",
      prefix + "0\n-1\n\n",
      prefix + "0\n0\n" + std::string(257, 'x') + "\n",
      prefix + "0\n0\nunsafe\ttext\n"};
  for (const auto& record : records) {
    std::ofstream file(path_);
    file << record;
    file.close();
    EXPECT_FALSE(ReadDaemonHealth(path_).has_value()) << record;
  }
}

TEST_F(DaemonStatusTest, HealthRejectsInvalidPublishStates) {
  std::string error;
  EXPECT_FALSE(PublishDaemonHealth(path_, false, static_cast<ServicePhase>(99),
                                   ServiceReason::kNone, "", error));
  EXPECT_FALSE(PublishDaemonHealth(path_, false, ServicePhase::kIdle,
                                   static_cast<ServiceReason>(99), "", error));
  EXPECT_FALSE(error.empty());
}

TEST_F(DaemonStatusTest, LivePidAloneDoesNotAssertDaemonIdentity) {
  std::ofstream file(path_);
  file << "off\n1\nhealth-v1\n4\n5\nold failure\n";
  file.close();
  const auto health = ReadDaemonHealth(path_);
  if (!health.has_value()) {
    FAIL() << "Expected health to be present";
  }
  EXPECT_EQ(health.value().pid, 1);
  EXPECT_TRUE(health.value().alive);
  EXPECT_EQ(health.value().phase, ServicePhase::kFailed);
}

TEST_F(DaemonStatusTest, HealthMissingOrUnreadableRecordIsAbsent) {
  EXPECT_FALSE(ReadDaemonHealth(path_).has_value());
  ASSERT_EQ(::mkdir(path_.c_str(), 0700), 0);
  EXPECT_FALSE(ReadDaemonHealth(path_).has_value());
}

TEST_F(DaemonStatusTest, HealthSanitizesNonPrintableBytes) {
  std::string error;
  ASSERT_TRUE(PublishDaemonHealth(path_, false, ServicePhase::kIdle,
                                  ServiceReason::kNone,
                                  std::string("x\x01\xff", 3), error));
  const auto health = ReadDaemonHealth(path_);
  if (!health.has_value()) {
    FAIL() << "Expected health to be present";
  }
  EXPECT_EQ(health.value().detail, "x  ");
}

TEST_F(DaemonStatusTest, CameraStatusRejectsPidOutsideOperatingSystemRange) {
  std::ofstream file(path_);
  file << "on\n999999999999\n";
  file.close();
  EXPECT_FALSE(ReadCameraStatus(path_));
}

using ReadTest = DaemonStatusTest;

TEST_F(ReadTest, AMissingFileIsOff) { EXPECT_FALSE(ReadCameraStatus(path_)); }

// The menu's own timer polls at an interval under this (tools/menu/main.mm
// and tools/menu/main_linux.cc both use one second), so a poller using a
// real clock, not a mock, must see a change made from another thread well
// inside this acceptance budget (#37 item 8, #61).
TEST_F(ReadTest, APublishFromAnotherThreadIsSeenWithinTwoSeconds) {
  std::string error;
  ASSERT_TRUE(PublishCameraStatus(path_, false, error));
  std::thread publisher([this] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string publish_error;
    PublishCameraStatus(path_, true, publish_error);
  });

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  bool seen = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (ReadCameraStatus(path_)) {
      seen = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  publisher.join();

  EXPECT_TRUE(seen);
}

TEST_F(ReadTest, UnreadableContentIsOff) {
  std::ofstream file(path_);
  file << "sideways\n";
  file.close();

  EXPECT_FALSE(ReadCameraStatus(path_));
}

TEST_F(ReadTest, AFileThatIsADirectoryIsOff) {
  ASSERT_EQ(::mkdir(path_.c_str(), 0700), 0);
  EXPECT_FALSE(ReadCameraStatus(path_));
}

// #184, GuardReadChecksAlive's own FALSE case: a daemon that died with
// the camera on, with nothing after to correct the file (CameraStatus!
// Crash), must not leave this reading on forever.
TEST_F(ReadTest, APublishedPidThatIsDeadIsOff) {
  const std::int64_t pid = DeadPid();
  std::ofstream file(path_);
  file << "on\n" << pid << "\n";
  file.close();

  EXPECT_FALSE(ReadCameraStatus(path_));
}

// GuardReadChecksAlive's own TRUE case by the other branch that reaches
// it: pid 1 (launchd or init) always exists but, run as anyone but
// root (as CI does), belongs to someone else (EPERM), which still
// counts as alive.
TEST_F(ReadTest, APidOwnedBySomeoneElseIsStillOn) {
  std::ofstream file(path_);
  file << "on\n1\n";
  file.close();

  EXPECT_TRUE(ReadCameraStatus(path_));
}

TEST_F(ReadTest, AMissingPidLineIsOff) {
  std::ofstream file(path_);
  file << "on\n";
  file.close();

  EXPECT_FALSE(ReadCameraStatus(path_));
}

TEST_F(ReadTest, APidLineThatIsNotANumberIsOff) {
  std::ofstream file(path_);
  file << "on\nsideways\n";
  file.close();

  EXPECT_FALSE(ReadCameraStatus(path_));
}

TEST_F(ReadTest, APidLineWithTrailingCharactersIsOff) {
  std::ofstream file(path_);
  file << "on\n1abc\n";
  file.close();

  EXPECT_FALSE(ReadCameraStatus(path_));
}

TEST_F(ReadTest, AZeroOrNegativePidIsOff) {
  std::ofstream file(path_);
  file << "on\n0\n";
  file.close();

  EXPECT_FALSE(ReadCameraStatus(path_));
}

}  // namespace
}  // namespace facepalm
