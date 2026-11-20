#ifndef FACEPALM_SRC_UNIX_SOCKET_H_
#define FACEPALM_SRC_UNIX_SOCKET_H_

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "src/service.h"

namespace facepalm {

// The service's connections over a Unix domain socket (#37), on macOS and
// Linux alike. The socket file is made readable and writable by its user
// alone, so only that user's programs can connect. These are the POSIX
// calls alone; test/unix_socket_test.cc runs them on real sockets.
class UnixListener : public Listener {
 public:
  // Listens at `path`, replacing a socket file left behind by a facepalm
  // that is no longer running. nullptr, with `error` saying why, when
  // another is still listening there or the socket cannot be made.
  static std::unique_ptr<UnixListener> Create(const std::string& path,
                                              std::string& error);
  ~UnixListener() override;
  UnixListener(const UnixListener&) = delete;
  UnixListener& operator=(const UnixListener&) = delete;

  std::unique_ptr<Stream> Accept() override;
  // Ends Accept, and removes the socket file.
  void Close() override;

 private:
  UnixListener(std::string path, int fd, std::array<int, 2> wake);

  const std::string path_;
  const int fd_;
  const int wake_read_;  // Close writes to wake_write_ to end a poll.
  const int wake_write_;
};

// One connection. A line longer than kLongestLine ends it, so a client
// cannot make facepalm hold an unbounded line.
class UnixStream : public Stream {
 public:
  static constexpr std::size_t kLongestLine = 4096;

  explicit UnixStream(int fd);
  ~UnixStream() override;
  UnixStream(const UnixStream&) = delete;
  UnixStream& operator=(const UnixStream&) = delete;

  std::optional<std::string> ReadLine() override;
  void WriteLine(std::string_view line) override;
  void Shutdown() override;

 private:
  const int fd_;
  std::string pending_;  // Bytes read past the last line returned.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_UNIX_SOCKET_H_
