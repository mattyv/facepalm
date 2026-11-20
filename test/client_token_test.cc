#include "src/client_token.h"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {
class ClientTokenTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::array<char, 32> path = {"/tmp/fp-token-XXXXXX"};
    const int fd = mkstemp(path.data());
    ASSERT_GE(fd, 0);
    close(fd);
    path_ = path.data();
  }
  void TearDown() override { std::remove(path_.c_str()); }
  void Write(const std::string& value) { std::ofstream(path_) << value; }
  std::string path_;
};

class MockTokenIo : public ClientTokenIo {
 public:
  MOCK_METHOD(int, Open, (const std::string&), (override));
  MOCK_METHOD(int, Stat, (int, struct stat&), (override));
  MOCK_METHOD(ssize_t, Read, (int, void*, std::size_t), (override));
  MOCK_METHOD(void, Close, (int), (override));
  MOCK_METHOD(uid_t, User, (), (override));
};

TEST(ClientTokenIoTest, RejectsStatAndOwnershipFailures) {
  MockTokenIo io;
  EXPECT_CALL(io, Open).Times(2).WillRepeatedly(testing::Return(7));
  EXPECT_CALL(io, Stat)
      .WillOnce(testing::Return(-1))
      .WillOnce([](int, struct stat& status) {
        status.st_mode = S_IFREG | 0600;
        status.st_uid = 42;
        return 0;
      });
  EXPECT_CALL(io, User).WillOnce(testing::Return(43));
  EXPECT_CALL(io, Close(7)).Times(2);
  EXPECT_FALSE(LoadClientToken("path", io));
  EXPECT_FALSE(LoadClientToken("path", io));
}

TEST(ClientTokenIoTest, RejectsNonRegularFileAfterOpening) {
  MockTokenIo io;
  EXPECT_CALL(io, Open).WillOnce(testing::Return(7));
  EXPECT_CALL(io, Stat).WillOnce([](int, struct stat& status) {
    status.st_mode = S_IFDIR | 0700;
    return 0;
  });
  EXPECT_CALL(io, Close(7));
  EXPECT_FALSE(LoadClientToken("path", io));
}

TEST(ClientTokenIoTest, RetriesInterruptedReadAndRejectsReadFailure) {
  MockTokenIo io;
  EXPECT_CALL(io, Open).Times(2).WillRepeatedly(testing::Return(7));
  EXPECT_CALL(io, Stat).Times(2).WillRepeatedly([](int, struct stat& status) {
    status.st_mode = S_IFREG | 0600;
    status.st_uid = 42;
    return 0;
  });
  EXPECT_CALL(io, User).Times(2).WillRepeatedly(testing::Return(42));
  EXPECT_CALL(io, Close(7)).Times(2);
  EXPECT_CALL(io, Read)
      .WillOnce([](int, void*, std::size_t) {
        errno = EINTR;
        return -1;
      })
      .WillOnce([](int, void* out, std::size_t) {
        std::memset(out, 'a', 64);
        return 64;
      })
      .WillOnce(testing::Return(0))
      .WillOnce([](int, void*, std::size_t) {
        errno = EIO;
        return -1;
      });
  EXPECT_EQ(LoadClientToken("path", io), std::string(64, 'a'));
  EXPECT_FALSE(LoadClientToken("path", io));
}

TEST_F(ClientTokenTest, AcceptsPrivateHexWithOptionalNewline) {
  Write(std::string(64, 'a'));
  EXPECT_EQ(LoadClientToken(path_), std::string(64, 'a'));
  Write(std::string(64, '0') + "\n");
  EXPECT_EQ(LoadClientToken(path_), std::string(64, '0'));
}

TEST_F(ClientTokenTest, RejectsMissingMalformedAndReadableByOthers) {
  EXPECT_FALSE(LoadClientToken(path_ + ".missing"));
  Write(std::string(64, 'A'));
  EXPECT_FALSE(LoadClientToken(path_));
  Write(std::string(64, '-'));
  EXPECT_FALSE(LoadClientToken(path_));
  Write(std::string(64, 'z'));
  EXPECT_FALSE(LoadClientToken(path_));
  Write(std::string(65, 'a'));
  EXPECT_FALSE(LoadClientToken(path_));
  Write(std::string(63, 'a'));
  EXPECT_FALSE(LoadClientToken(path_));
  Write(std::string(64, 'a') + "\n\n");
  EXPECT_FALSE(LoadClientToken(path_));
  Write(std::string(64, 'a'));
  ASSERT_EQ(chmod(path_.c_str(), 0644), 0);
  EXPECT_FALSE(LoadClientToken(path_));
}

TEST_F(ClientTokenTest, RejectsSymbolicLinksAndDirectories) {
  const std::string link = path_ + ".link";
  ASSERT_EQ(symlink(path_.c_str(), link.c_str()), 0);
  EXPECT_FALSE(LoadClientToken(link));
  EXPECT_FALSE(LoadClientToken("/tmp"));
  std::remove(link.c_str());
}
}  // namespace
}  // namespace facepalm
