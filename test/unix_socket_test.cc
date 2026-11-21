#include "src/unix_socket.h"

#include <pthread.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// UnixListener and UnixStream are the POSIX calls alone, so these tests run
// them on real sockets, in a temporary directory each test removes.

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NotNull;

constexpr auto kPrompt = std::chrono::seconds(5);
// Long enough for another thread to reach a blocking call.
constexpr auto kSettle = std::chrono::milliseconds(50);

// A client end, connected to `path`; -1 when it cannot connect.
int Dial(const std::string& path) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (connect(fd, reinterpret_cast<const sockaddr*>(&address),
              sizeof(address)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

void Send(int fd, const std::string& text) {
  ASSERT_THAT(send(fd, text.data(), text.size(), 0),
              Eq(static_cast<ssize_t>(text.size())));
}

std::string Receive(int fd) {
  std::array<char, 256> buffer{};
  const ssize_t got = recv(fd, buffer.data(), buffer.size(), 0);
  return got > 0 ? std::string(buffer.data(), got) : "";
}

class UnixSocketTest : public ::testing::Test {
 protected:
  UnixSocketTest() {
    std::string pattern =
        (std::filesystem::temp_directory_path() / "fp.XXXXXX").string();
    directory_ = mkdtemp(pattern.data());
    path_ = directory_ + "/s";
  }
  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::unique_ptr<UnixListener> Listen() {
    std::string error;
    auto listener = UnixListener::Create(path_, error);
    EXPECT_THAT(error, Eq(""));
    return listener;
  }

  std::string directory_;
  std::string path_;
};

TEST_F(UnixSocketTest, AClientsLinesArriveAndRepliesReachIt) {
  auto listener = Listen();
  ASSERT_THAT(listener, NotNull());
  const int client = Dial(path_);
  ASSERT_GE(client, 0);
  auto stream = listener->Accept();
  ASSERT_THAT(stream, NotNull());

  Send(client, "hello agent\nre");
  Send(client, "ad\n");

  EXPECT_THAT(stream->ReadLine(), Eq("hello agent"));
  EXPECT_THAT(stream->ReadLine(), Eq("read"));
  stream->WriteLine(R"({"ok":true})");
  EXPECT_THAT(Receive(client), Eq("{\"ok\":true}\n"));
  close(client);
}

TEST_F(UnixSocketTest, OnlyItsUserCanOpenTheSocket) {
  const auto listener = Listen();
  struct stat info{};
  ASSERT_THAT(stat(path_.c_str(), &info), Eq(0));

  EXPECT_THAT(info.st_mode & 0777U, Eq(0600U));
}

TEST_F(UnixSocketTest, AHangUpEndsReadLine) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();

  close(client);

  EXPECT_THAT(stream->ReadLine(), Eq(std::nullopt));
  stream->WriteLine("to no one");  // Neither raises SIGPIPE nor blocks.
}

// Service!Noticed's watcher, on a real socket (specs/Service.tla's
// HangUpEnds).
TEST_F(UnixSocketTest, AHangUpEndsAnAwaitInAnotherThread) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();
  auto hung =
      std::async(std::launch::async, [&] { return stream->AwaitHangUp(); });
  ASSERT_THAT(hung.wait_for(std::chrono::milliseconds(50)),
              Eq(std::future_status::timeout));

  close(client);

  ASSERT_THAT(hung.wait_for(std::chrono::seconds(5)),
              Eq(std::future_status::ready));
  EXPECT_TRUE(hung.get());
  stream->EndAwaitHangUp();
}

TEST_F(UnixSocketTest, EndingAnAwaitReturnsFalseAndTheNextOneWatchesAgain) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();
  auto first =
      std::async(std::launch::async, [&] { return stream->AwaitHangUp(); });
  ASSERT_THAT(first.wait_for(std::chrono::milliseconds(50)),
              Eq(std::future_status::timeout));
  stream->EndAwaitHangUp();
  ASSERT_THAT(first.wait_for(std::chrono::seconds(5)),
              Eq(std::future_status::ready));
  EXPECT_FALSE(first.get());

  auto second =
      std::async(std::launch::async, [&] { return stream->AwaitHangUp(); });
  ASSERT_THAT(second.wait_for(std::chrono::milliseconds(50)),
              Eq(std::future_status::timeout));
  close(client);
  ASSERT_THAT(second.wait_for(std::chrono::seconds(5)),
              Eq(std::future_status::ready));
  EXPECT_TRUE(second.get());
  stream->EndAwaitHangUp();
}

TEST_F(UnixSocketTest, AnEndBeforeTheAwaitStartsEndsIt) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();

  stream->EndAwaitHangUp();

  EXPECT_FALSE(stream->AwaitHangUp());
  close(client);
}

TEST_F(UnixSocketTest, ALineArrivingEndsAnAwaitAndIsStillRead) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();
  Send(client, "read\n");

  EXPECT_FALSE(stream->AwaitHangUp());
  stream->EndAwaitHangUp();
  EXPECT_THAT(stream->ReadLine(), Eq("read"));
  close(client);
}

TEST(UnixStreamTest, WithoutAWakePipeNothingIsWatched) {
  std::array<int, 2> pair{-1, -1};
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()), Eq(0));
  rlimit open_files{};
  ASSERT_THAT(getrlimit(RLIMIT_NOFILE, &open_files), Eq(0));
  // With no descriptor free under the limit, the stream's pipe fails.
  rlimit none_free = open_files;
  none_free.rlim_cur = 3;
  ASSERT_THAT(setrlimit(RLIMIT_NOFILE, &none_free), Eq(0));
  auto stream = std::make_unique<UnixStream>(pair[0]);
  ASSERT_THAT(setrlimit(RLIMIT_NOFILE, &open_files), Eq(0));

  EXPECT_FALSE(stream->AwaitHangUp());
  stream->EndAwaitHangUp();
  close(pair[1]);
}

TEST_F(UnixSocketTest, ShutdownEndsAReadLineInAnotherThread) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();
  auto read =
      std::async(std::launch::async, [&] { return stream->ReadLine(); });

  stream->Shutdown();

  ASSERT_THAT(read.wait_for(kPrompt), Eq(std::future_status::ready));
  EXPECT_THAT(read.get(), Eq(std::nullopt));
  close(client);
}

TEST_F(UnixSocketTest, ALineTooLongEndsTheConnection) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();

  Send(client, std::string(UnixStream::kLongestLine + 1, 'x'));

  EXPECT_THAT(stream->ReadLine(), Eq(std::nullopt));
  close(client);
}

TEST_F(UnixSocketTest, ALineTooLongEndsTheConnectionEvenOnceItsNewlineComes) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();

  Send(client, std::string(UnixStream::kLongestLine + 1, 'x') + "\n");

  EXPECT_THAT(stream->ReadLine(), Eq(std::nullopt));
  close(client);
}

TEST_F(UnixSocketTest, ALineExactlyAtTheLimitArrives) {
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();
  const std::string longest(UnixStream::kLongestLine, 'x');

  Send(client, longest + "\n");

  EXPECT_THAT(stream->ReadLine(), Eq(longest));
  close(client);
}

// A signal delivered to the accepting thread interrupts its poll, which then
// reports nothing readable. Accept must poll again, not fall into accept(2),
// where Close could no longer wake it.
TEST_F(UnixSocketTest, ASignalDoesNotKeepCloseFromEndingAnAccept) {
  struct sigaction action{};
  action.sa_handler = [](int) {};  // No SA_RESTART: poll fails with EINTR.
  struct sigaction before{};
  ASSERT_THAT(sigaction(SIGUSR1, &action, &before), Eq(0));
  auto listener = Listen();
  std::promise<pthread_t> accepting;
  auto accepted = std::async(std::launch::async, [&] {
    accepting.set_value(pthread_self());
    return listener->Accept();
  });
  const pthread_t thread = accepting.get_future().get();
  std::this_thread::sleep_for(kSettle);  // Let the thread reach its poll.
  ASSERT_THAT(pthread_kill(thread, SIGUSR1), Eq(0));
  std::this_thread::sleep_for(kSettle);  // Let the signal land first.

  listener->Close();

  if (accepted.wait_for(kPrompt) != std::future_status::ready) {
    ADD_FAILURE() << "Close left Accept blocked";
    pthread_kill(thread, SIGUSR1);  // Unstick the thread, so the test ends.
  }
  EXPECT_THAT(accepted.get(), IsNull());
  ASSERT_THAT(sigaction(SIGUSR1, &before, nullptr), Eq(0));
}

TEST_F(UnixSocketTest, ASignalDoesNotKeepAHangUpFromEndingAnAwait) {
  struct sigaction action{};
  action.sa_handler = [](int) {};  // No SA_RESTART: poll fails with EINTR.
  struct sigaction before{};
  ASSERT_THAT(sigaction(SIGUSR1, &action, &before), Eq(0));
  auto listener = Listen();
  const int client = Dial(path_);
  auto stream = listener->Accept();
  std::promise<pthread_t> awaiting;
  auto hung = std::async(std::launch::async, [&] {
    awaiting.set_value(pthread_self());
    return stream->AwaitHangUp();
  });
  const pthread_t thread = awaiting.get_future().get();
  std::this_thread::sleep_for(kSettle);  // Let the thread reach its poll.
  ASSERT_THAT(pthread_kill(thread, SIGUSR1), Eq(0));
  std::this_thread::sleep_for(kSettle);  // Let the signal land first.

  close(client);

  if (hung.wait_for(kPrompt) != std::future_status::ready) {
    ADD_FAILURE() << "A hang-up left AwaitHangUp blocked";
    stream->EndAwaitHangUp();  // Unstick the thread, so the test ends.
  }
  EXPECT_TRUE(hung.get());
  stream->EndAwaitHangUp();
  ASSERT_THAT(sigaction(SIGUSR1, &before, nullptr), Eq(0));
}

TEST_F(UnixSocketTest, CloseEndsAnAcceptAndRemovesTheSocketFile) {
  auto listener = Listen();
  auto accepted =
      std::async(std::launch::async, [&] { return listener->Accept(); });

  listener->Close();

  ASSERT_THAT(accepted.wait_for(kPrompt), Eq(std::future_status::ready));
  EXPECT_THAT(accepted.get(), IsNull());
  EXPECT_THAT(listener->Accept(), IsNull());
  EXPECT_FALSE(std::filesystem::exists(path_));
  EXPECT_THAT(Dial(path_), Eq(-1));
}

TEST_F(UnixSocketTest, ASecondFacepalmIsRefused) {
  const auto first = Listen();
  std::string error;

  EXPECT_THAT(UnixListener::Create(path_, error), IsNull());
  EXPECT_THAT(error, HasSubstr("facepalm is already running at"));
  EXPECT_GE(Dial(path_), 0);  // The first still listens.
}

TEST_F(UnixSocketTest, ASocketFileLeftBehindIsReplaced) {
  {
    // A listener that died without closing leaves its file behind.
    const int dead = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path_.c_str(), sizeof(address.sun_path) - 1);
    ASSERT_THAT(bind(dead, reinterpret_cast<const sockaddr*>(&address),
                     sizeof(address)),
                Eq(0));
    close(dead);
  }

  const auto listener = Listen();

  EXPECT_THAT(listener, NotNull());
  EXPECT_GE(Dial(path_), 0);
}

TEST_F(UnixSocketTest, AFileThatIsNotASocketIsKept) {
  {
    std::ofstream file(path_);
    file << "not a socket";
  }
  std::string error;

  EXPECT_THAT(UnixListener::Create(path_, error), IsNull());
  EXPECT_THAT(error, HasSubstr("is not a socket"));
  std::ifstream kept(path_);
  std::string contents;
  std::getline(kept, contents);
  EXPECT_THAT(contents, Eq("not a socket"));
}

TEST_F(UnixSocketTest, CloseLeavesASocketThatReplacedIt) {
  auto first = Listen();
  ASSERT_THAT(unlink(path_.c_str()), Eq(0));
  const auto second = Listen();  // Another facepalm now owns the path.

  first->Close();

  EXPECT_GE(Dial(path_), 0);  // The second still listens.
}

TEST_F(UnixSocketTest, APathTooLongIsRefused) {
  std::string error;

  EXPECT_THAT(
      UnixListener::Create(directory_ + "/" + std::string(200, 'x'), error),
      IsNull());
  EXPECT_THAT(error, HasSubstr("the socket path is too long"));
}

TEST_F(UnixSocketTest, ADirectoryThatIsNotThereIsRefused) {
  std::string error;

  EXPECT_THAT(UnixListener::Create(directory_ + "/missing/s", error), IsNull());
  EXPECT_THAT(error, HasSubstr("cannot listen at"));
}

}  // namespace
}  // namespace facepalm
