#include "src/daemon_status.h"

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Each test stands for the specs/CameraStatus.tla action or property it is
// named for.

namespace facepalm {
namespace {

using ::testing::IsEmpty;
using ::testing::Not;

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

}  // namespace
}  // namespace facepalm
