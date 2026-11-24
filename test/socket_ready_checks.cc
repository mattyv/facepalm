#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/socket_ready.h"

namespace facepalm {
namespace {

class RealSocketTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char pattern[] = "/tmp/facepalm-probe-XXXXXX";
    char* dir = mkdtemp(pattern);
    ASSERT_NE(dir, nullptr);
    directory_ = dir;
    path_ = directory_ + "/socket";
  }
  void TearDown() override {
    for (int fd : fds_) close(fd);
    if (!directory_.empty()) std::filesystem::remove_all(directory_);
  }
  int Socket() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) fds_.push_back(fd);
    return fd;
  }
  sockaddr_un Address() const {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, path_.c_str());
    return address;
  }
  std::string directory_;
  std::string path_;
  std::vector<int> fds_;
};

TEST_F(RealSocketTest, MissingAndRefusedAreNotReady) {
  EXPECT_FALSE(SocketReady(path_));
  const int fd = Socket();
  ASSERT_GE(fd, 0);
  auto address = Address();
  ASSERT_EQ(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)),
            0);
  EXPECT_FALSE(SocketReady(path_));
}

TEST_F(RealSocketTest, FullBacklogIsBoundedAndProbeSendsNoBytes) {
  const int listener = Socket();
  ASSERT_GE(listener, 0);
  auto address = Address();
  ASSERT_EQ(
      bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)),
      0);
  ASSERT_EQ(listen(listener, 1), 0);
  ASSERT_TRUE(SocketReady(path_));
  const int probe = accept(listener, nullptr, nullptr);
  ASSERT_GE(probe, 0);
  fds_.push_back(probe);
  char byte = 0;
  EXPECT_EQ(read(probe, &byte, 1), 0);
  bool full = false;
  for (int i = 0; i < 32; ++i) {
    int fd = Socket();
    ASSERT_GE(fd, 0);
    ASSERT_EQ(fcntl(fd, F_SETFL, O_NONBLOCK), 0);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
        0) {
      full = true;
      break;
    }
  }
  ASSERT_TRUE(full);
  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(SocketReady(path_));
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
}

}  // namespace
}  // namespace facepalm
