#include "src/atomic_private_file.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::Return;

class MockAtomicPrivateFileIo : public AtomicPrivateFileIo {
 public:
  MOCK_METHOD(int, OpenTemp, (const std::string&), (override));
  MOCK_METHOD(bool, Chmod, (int), (override));
  MOCK_METHOD(ssize_t, Write, (int, const void*, std::size_t), (override));
  MOCK_METHOD(void, Close, (int), (override));
  MOCK_METHOD(bool, Rename, (const std::string&), (override));
  MOCK_METHOD(void, Remove, (const std::string&), (override));
};

class WriteFileAtomicallyIoTest : public ::testing::Test {
 protected:
  std::shared_ptr<NiceMock<MockAtomicPrivateFileIo>> io_ =
      std::make_shared<NiceMock<MockAtomicPrivateFileIo>>();
};

TEST_F(WriteFileAtomicallyIoTest, WritesChmodsAndRenames) {
  EXPECT_CALL(*io_, OpenTemp("/private/frame")).WillOnce(Return(7));
  EXPECT_CALL(*io_, Chmod(7)).WillOnce(Return(true));
  EXPECT_CALL(*io_, Write(7, testing::_, 5))
      .WillOnce([](int, const void*, std::size_t size) {
        return static_cast<ssize_t>(size);
      });
  EXPECT_CALL(*io_, Close(7));
  EXPECT_CALL(*io_, Rename("/private/frame")).WillOnce(Return(true));
  EXPECT_CALL(*io_, Remove).Times(0);

  EXPECT_TRUE(WriteFileAtomically("/private/frame", "hello", io_));
}

TEST_F(WriteFileAtomicallyIoTest, RefusesWhenTheFileCannotBeOpened) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(-1));
  EXPECT_CALL(*io_, Chmod).Times(0);
  EXPECT_CALL(*io_, Remove).Times(0);

  EXPECT_FALSE(WriteFileAtomically("/private/frame", "hello", io_));
}

TEST_F(WriteFileAtomicallyIoTest, CleansUpWhenChmodFails) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Chmod(7)).WillOnce(Return(false));
  EXPECT_CALL(*io_, Write).Times(0);
  EXPECT_CALL(*io_, Close(7));
  EXPECT_CALL(*io_, Remove("/private/frame"));

  EXPECT_FALSE(WriteFileAtomically("/private/frame", "hello", io_));
}

TEST_F(WriteFileAtomicallyIoTest, CleansUpWhenTheWriteIsShort) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Chmod(7)).WillOnce(Return(true));
  EXPECT_CALL(*io_, Write).WillOnce(Return(2));
  EXPECT_CALL(*io_, Close(7));
  EXPECT_CALL(*io_, Rename).Times(0);
  EXPECT_CALL(*io_, Remove("/private/frame"));

  EXPECT_FALSE(WriteFileAtomically("/private/frame", "hello", io_));
}

TEST_F(WriteFileAtomicallyIoTest, CleansUpWhenTheWriteFails) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Chmod(7)).WillOnce(Return(true));
  EXPECT_CALL(*io_, Write).WillOnce(Return(-1));
  EXPECT_CALL(*io_, Close(7));
  EXPECT_CALL(*io_, Remove("/private/frame"));

  EXPECT_FALSE(WriteFileAtomically("/private/frame", "hello", io_));
}

TEST_F(WriteFileAtomicallyIoTest, CleansUpWhenTheRenameFails) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Chmod(7)).WillOnce(Return(true));
  EXPECT_CALL(*io_, Write).WillOnce([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  EXPECT_CALL(*io_, Rename).WillOnce(Return(false));
  EXPECT_CALL(*io_, Remove("/private/frame"));

  EXPECT_FALSE(WriteFileAtomically("/private/frame", "hello", io_));
}

// Round-trips through real files, exercising the real POSIX Io.
class WriteFileAtomicallyFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() /
                 std::filesystem::path("facepalm-atomic-write-test-" +
                                       std::to_string(::getpid()));
    std::filesystem::create_directory(directory_);
    path_ = (directory_ / "frame").string();
  }
  void TearDown() override { std::filesystem::remove_all(directory_); }

  [[nodiscard]] std::string ReadFile() const {
    std::ifstream file(path_, std::ios::binary);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
  }

  std::filesystem::path directory_;
  std::string path_;
};

TEST_F(WriteFileAtomicallyFileTest, WritesPrivatelyAndLeavesNoTempFile) {
  EXPECT_TRUE(WriteFileAtomically(path_, "hello"));
  EXPECT_THAT(ReadFile(), Eq("hello"));

  struct stat info{};
  ASSERT_EQ(stat(path_.c_str(), &info), 0);
  EXPECT_THAT(info.st_mode & 0777U, Eq(0600U));

  struct stat temp_info{};
  EXPECT_NE(stat((path_ + ".tmp").c_str(), &temp_info), 0);
}

TEST_F(WriteFileAtomicallyFileTest, ReplacesWhatWasThereAtomically) {
  ASSERT_TRUE(WriteFileAtomically(path_, "first"));
  EXPECT_TRUE(WriteFileAtomically(path_, "second"));
  EXPECT_THAT(ReadFile(), Eq("second"));
}

TEST_F(WriteFileAtomicallyFileTest, RefusesWhenTheDirectoryDoesNotExist) {
  EXPECT_FALSE(
      WriteFileAtomically((directory_ / "missing" / "frame").string(), "x"));
}

TEST_F(WriteFileAtomicallyFileTest, CleansUpTheTemporaryFileWhenRenameFails) {
  // A directory already at `path_`: renaming a plain file over it fails
  // (EISDIR), after the temporary file was genuinely opened and written.
  std::filesystem::create_directory(path_);

  EXPECT_FALSE(WriteFileAtomically(path_, "x"));

  struct stat temp_info{};
  EXPECT_NE(stat((path_ + ".tmp").c_str(), &temp_info), 0);
}

}  // namespace
}  // namespace facepalm
