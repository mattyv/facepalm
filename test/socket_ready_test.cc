#include "src/socket_ready.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cerrno>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class MockCalls {
 public:
  MOCK_METHOD(int, Socket, (int, int, int));
  MOCK_METHOD(int, Fcntl, (int, int));
  MOCK_METHOD(int, Connect, (int, const sockaddr*, socklen_t));
  MOCK_METHOD(int, Poll, (pollfd*, nfds_t, int));
  MOCK_METHOD(int, Option, (int, int, int, void*, socklen_t*));
  MOCK_METHOD(int, Close, (int));
};

// SocketReadyCalls mirrors POSIX function pointers, so these forward to the
// fixture's mock. Each test removes the pointer before its mock is destroyed.
MockCalls* active = nullptr;
int Socket(int domain, int type, int protocol) {
  return active->Socket(domain, type, protocol);
}
int Fcntl(int fd, int command, ...) { return active->Fcntl(fd, command); }
int Connect(int fd, const sockaddr* address, socklen_t length) {
  return active->Connect(fd, address, length);
}
int Poll(pollfd* fds, nfds_t count, int timeout) {
  return active->Poll(fds, count, timeout);
}
int Option(int fd, int level, int option, void* value, socklen_t* length) {
  return active->Option(fd, level, option, value, length);
}
int Close(int fd) { return active->Close(fd); }

class SocketReadyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    active = &mock_;
    ON_CALL(mock_, Socket).WillByDefault(Return(7));
    ON_CALL(mock_, Fcntl).WillByDefault(Return(0));
    ON_CALL(mock_, Connect).WillByDefault([](int, const sockaddr*, socklen_t) {
      errno = EINPROGRESS;
      return -1;
    });
    ON_CALL(mock_, Poll).WillByDefault([](pollfd* fd, nfds_t, int) {
      fd->revents = POLLOUT;
      return 1;
    });
    ON_CALL(mock_, Option)
        .WillByDefault([](int, int, int, void* value, socklen_t*) {
          *static_cast<int*>(value) = 0;
          return 0;
        });
  }
  void TearDown() override { active = nullptr; }
  SocketReadyCalls calls_{Socket, Fcntl, Connect, Poll, Option, Close};
  NiceMock<MockCalls> mock_;
};

TEST_F(SocketReadyTest, ImmediateAndPendingConnectionsSucceedAndClose) {
  EXPECT_CALL(mock_, Close(7)).Times(2);
  EXPECT_CALL(mock_, Poll(_, 1, 50)).Times(1);
  EXPECT_TRUE(SocketReady("socket", calls_));
  EXPECT_CALL(mock_, Connect).WillOnce(Return(0)).RetiresOnSaturation();
  EXPECT_TRUE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, OversizedPathAndSocketFailureDoNotClose) {
  EXPECT_CALL(mock_, Socket).WillOnce(Return(-1)).RetiresOnSaturation();
  EXPECT_CALL(mock_, Close).Times(0);
  EXPECT_FALSE(
      SocketReady(std::string(sizeof(sockaddr_un::sun_path), 'x'), calls_));
  EXPECT_FALSE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, FlagReadFailureClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, Fcntl(7, F_GETFL)).WillOnce(Return(-1));
  EXPECT_FALSE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, FlagWriteFailureClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, Fcntl(7, F_GETFL)).WillOnce(Return(0));
  EXPECT_CALL(mock_, Fcntl(7, F_SETFL)).WillOnce(Return(-1));
  EXPECT_FALSE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, ConnectionFailureClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, Connect).WillOnce([](int, const sockaddr*, socklen_t) {
    errno = ECONNREFUSED;
    return -1;
  });
  EXPECT_FALSE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, TimeoutClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, Poll(_, 1, 50)).WillOnce(Return(0));
  EXPECT_FALSE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, HangupClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, Poll(_, 1, 50)).WillOnce([](pollfd* fd, nfds_t, int) {
    fd->revents = POLLHUP;
    return 1;
  });
  EXPECT_FALSE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, OptionReadFailureClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, Option).WillOnce(Return(-1));
  EXPECT_FALSE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, SocketErrorClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, Option)
      .WillOnce([](int, int, int, void* value, socklen_t*) {
        *static_cast<int*>(value) = ECONNREFUSED;
        return 0;
      });
  EXPECT_FALSE(SocketReady("socket", calls_));
}

}  // namespace
}  // namespace facepalm
