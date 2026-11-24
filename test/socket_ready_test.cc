#include "src/socket_ready.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

class MockCalls {
 public:
  MOCK_METHOD(int, Socket, (int, int, int));
  MOCK_METHOD(int, GetFlags, (int));
  MOCK_METHOD(int, SetFlags, (int, int));
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
int GetFlags(int fd) { return active->GetFlags(fd); }
int SetFlags(int fd, int flags) { return active->SetFlags(fd, flags); }
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
    ON_CALL(mock_, GetFlags).WillByDefault(Return(0));
    ON_CALL(mock_, SetFlags).WillByDefault(Return(0));
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
  SocketReadyCalls calls_{.socket = Socket,
                          .get_flags = GetFlags,
                          .set_flags = SetFlags,
                          .connect = Connect,
                          .poll = Poll,
                          .getsockopt = Option,
                          .close = Close};
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

TEST_F(SocketReadyTest, PreservesFlagsAndEnablesNonblocking) {
  EXPECT_CALL(mock_, GetFlags(7)).WillOnce(Return(O_APPEND));
  EXPECT_CALL(mock_, SetFlags(7, O_APPEND + O_NONBLOCK)).WillOnce(Return(0));
  EXPECT_CALL(mock_, Close(7));
  EXPECT_TRUE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, FlagReadFailureClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, GetFlags(7)).WillOnce(Return(-1));
  EXPECT_FALSE(SocketReady("socket", calls_));
}

TEST_F(SocketReadyTest, FlagWriteFailureClosesDescriptor) {
  EXPECT_CALL(mock_, Close(7));
  EXPECT_CALL(mock_, GetFlags(7)).WillOnce(Return(0));
  EXPECT_CALL(mock_, SetFlags(7, O_NONBLOCK)).WillOnce(Return(-1));
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

TEST_F(SocketReadyTest, CapturesPeerPidBeforeClosingTheConnectedSocket) {
  InSequence order;
  EXPECT_CALL(mock_, Connect).WillOnce(Return(0));
#ifdef __linux__
  EXPECT_CALL(mock_, Option(7, SOL_SOCKET, SO_PEERCRED, _, _))
      .WillOnce([](int, int, int, void* value, socklen_t* length) {
        auto* credentials = static_cast<ucred*>(value);
        credentials->pid = 123;
        *length = sizeof(*credentials);
        return 0;
      });
#elif defined(__APPLE__)
  EXPECT_CALL(mock_, Option(7, SOL_LOCAL, LOCAL_PEERPID, _, _))
      .WillOnce([](int, int, int, void* value, socklen_t* length) {
        *static_cast<pid_t*>(value) = 123;
        *length = sizeof(pid_t);
        return 0;
      });
#endif
  EXPECT_CALL(mock_, Close(7));
  const auto result = ProbeSocketReady("socket", calls_);
  EXPECT_TRUE(result.ready);
  EXPECT_EQ(result.peer_pid, 123);
}

#if defined(__linux__) || defined(__APPLE__)
TEST_F(SocketReadyTest, MalformedPeerCredentialsRemainUnknown) {
  EXPECT_CALL(mock_, Connect).Times(3).WillRepeatedly(Return(0));
  EXPECT_CALL(mock_, Close(7)).Times(3);
  int call = 0;
  EXPECT_CALL(mock_, Option)
      .Times(3)
      .WillRepeatedly([&call](int, int, int, void* value, socklen_t* length) {
#ifdef __linux__
        constexpr std::array<pid_t, 3> kPids = {123, 0, -1};
        auto* credentials = static_cast<ucred*>(value);
        credentials->pid = kPids.at(static_cast<std::size_t>(call));
#else
        constexpr std::array<pid_t, 3> kPids = {123, 0, -1};
        auto* pid = static_cast<pid_t*>(value);
        *pid = kPids.at(static_cast<std::size_t>(call));
#endif
        if (call == 0) {
          *length = 1;
        }
        ++call;
        return 0;
      });
  for (int attempt = 0; attempt < 3; ++attempt) {
    const auto result = ProbeSocketReady("socket", calls_);
    EXPECT_TRUE(result.ready);
    EXPECT_FALSE(result.peer_pid.has_value());
  }
}
#endif

TEST_F(SocketReadyTest, MissingPeerCredentialsDoNotNegateConnection) {
  EXPECT_CALL(mock_, Connect).WillOnce(Return(0));
  EXPECT_CALL(mock_, Option).WillOnce(Return(-1));
  const auto result = ProbeSocketReady("socket", calls_);
  EXPECT_TRUE(result.ready);
  EXPECT_FALSE(result.peer_pid.has_value());
}

}  // namespace
}  // namespace facepalm
