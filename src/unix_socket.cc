#include "src/unix_socket.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace facepalm {
namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;  // Linux: no SIGPIPE on a hang-up.
#else
constexpr int kSendFlags = 0;  // macOS: SO_NOSIGPIPE is set on the socket.
#endif

constexpr int kBacklog = 8;
constexpr mode_t kOwnerOnly = S_IRUSR | S_IWUSR;

// `path` as a socket address; false when it does not fit.
bool AddressOf(const std::string& path, sockaddr_un& address) {
  address = {};
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) {
    return false;
  }
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  return true;
}

// Whether something is listening at `address`.
bool Listening(const sockaddr_un& address) {
  const int probe = socket(AF_UNIX, SOCK_STREAM, 0);
  const bool answered =
      connect(probe, reinterpret_cast<const sockaddr*>(&address),
              sizeof(address)) == 0;
  close(probe);
  return answered;
}

}  // namespace

std::unique_ptr<UnixListener> UnixListener::Create(const std::string& path,
                                                   std::string& error) {
  sockaddr_un address{};
  if (!AddressOf(path, address)) {
    error = "the socket path is too long: " + path;
    return nullptr;
  }
  if (Listening(address)) {
    error = "facepalm is already running at " + path;
    return nullptr;
  }
  unlink(path.c_str());  // A socket file left by a facepalm that is gone.
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  std::array<int, 2> wake{-1, -1};
  // Every step runs, and one check covers them all: none of the later ones
  // fails on a socket path that bound, and a test cannot make them fail.
  // Each returns 0, or -1 on failure.
  const int status =
      bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) +
      chmod(path.c_str(), kOwnerOnly) + listen(fd, kBacklog) +
      pipe(wake.data());
  if (status != 0) {
    error = "cannot listen at " + path + ": " + std::strerror(errno);
    close(fd);
    close(wake[0]);
    close(wake[1]);
    return nullptr;
  }
  return std::unique_ptr<UnixListener>(new UnixListener(path, fd, wake));
}

UnixListener::UnixListener(std::string path, int fd, std::array<int, 2> wake)
    : path_(std::move(path)),
      fd_(fd),
      wake_read_(wake[0]),
      wake_write_(wake[1]) {}

UnixListener::~UnixListener() {
  Close();
  close(fd_);
  close(wake_read_);
  close(wake_write_);
}

std::unique_ptr<Stream> UnixListener::Accept() {
  std::array<pollfd, 2> fds{
      {{.fd = fd_, .events = POLLIN, .revents = 0},
       {.fd = wake_read_, .events = POLLIN, .revents = 0}}};
  int connection = -1;
  // ponytail: retries a failed accept at once; a lasting failure, such as
  // running out of file descriptors, spins until one is free.
  while (connection < 0) {
    poll(fds.data(), fds.size(), -1);
    if (fds[1].revents != 0) {
      return nullptr;  // Closed.
    }
    connection = accept(fd_, nullptr, nullptr);
  }
  return std::make_unique<UnixStream>(connection);
}

void UnixListener::Close() {
  const char wake = 0;
  // The pipe stays readable, so every later Accept returns at once, too.
  // A full pipe is already readable, so a failed write changes nothing.
  const ssize_t written = write(wake_write_, &wake, 1);
  static_cast<void>(written);
  unlink(path_.c_str());
}

UnixStream::UnixStream(int fd) : fd_(fd) {
#ifdef SO_NOSIGPIPE
  const int on = 1;
  setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}

UnixStream::~UnixStream() { close(fd_); }

std::optional<std::string> UnixStream::ReadLine() {
  constexpr std::size_t kChunk = 512;
  std::array<char, kChunk> chunk{};
  while (true) {
    const std::size_t newline = pending_.find('\n');
    if (newline != std::string::npos) {
      if (newline > kLongestLine) {
        return std::nullopt;
      }
      std::string line = pending_.substr(0, newline);
      pending_.erase(0, newline + 1);
      return line;
    }
    if (pending_.size() > kLongestLine) {
      return std::nullopt;
    }
    const ssize_t got = recv(fd_, chunk.data(), chunk.size(), 0);
    if (got <= 0) {
      return std::nullopt;  // Hung up, shut down, or failed.
    }
    pending_.append(chunk.data(), static_cast<std::size_t>(got));
  }
}

void UnixStream::WriteLine(std::string_view line) {
  std::string text(line);
  text += '\n';
  std::string_view rest = text;
  while (!rest.empty()) {
    const ssize_t sent = send(fd_, rest.data(), rest.size(), kSendFlags);
    if (sent <= 0) {
      return;  // The peer is gone; the next ReadLine says so.
    }
    rest.remove_prefix(static_cast<std::size_t>(sent));
  }
}

void UnixStream::Shutdown() { shutdown(fd_, SHUT_RDWR); }

}  // namespace facepalm
