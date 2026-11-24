#ifndef FACEPALM_TOOLS_POSIX_CALIBRATE_CLIENT_IO_H_
#define FACEPALM_TOOLS_POSIX_CALIBRATE_CLIENT_IO_H_

// A plain POSIX socket behind src/calibrate_cli.h's CalibrateClientIo
// (#36, #35, item 12 of #61), shared by tools/calibrate_cli/main.cc and,
// on macOS, tools/menu/main.mm (#118): untested POSIX glue by this
// repo's own convention, the same as tools/gesture_replay/main.cc's.
//
// SO_NOSIGPIPE and the send flags below follow src/unix_socket.cc's
// UnixStream exactly, for the same reason (#128's fourth finding): a
// daemon gone mid-wizard otherwise raises SIGPIPE on this process's own
// default disposition, which kills the menu rather than letting send(2)
// simply fail; recv(2) below gets a timeout of its own, since a daemon
// that stops answering, rather than closing the socket outright, would
// otherwise block this call -- and the menu's whole UI thread, since
// CalibrationFlow drives this synchronously -- forever.
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "src/calibrate_cli.h"

namespace facepalm {

#ifdef MSG_NOSIGNAL
inline constexpr int kPosixCalibrateSendFlags = MSG_NOSIGNAL;  // Linux.
#else
inline constexpr int kPosixCalibrateSendFlags = 0;  // macOS: SO_NOSIGPIPE.
#endif

class PosixCalibrateClientIo : public CalibrateClientIo {
 public:
  ~PosixCalibrateClientIo() override { Close(); }

  void Close() override {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
    pending_.clear();
  }

  bool Connect(const std::string& path) override {
    Close();
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
      return false;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
      return false;
    }
#ifdef SO_NOSIGPIPE
    const int on = 1;
    setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    const timeval timeout{.tv_sec = kReceiveTimeoutSeconds, .tv_usec = 0};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return connect(fd_, reinterpret_cast<const sockaddr*>(&address),
                   sizeof(address)) == 0;
  }

  void WriteLine(std::string_view line) override {
    std::string text(line);
    text += '\n';
    std::string_view rest = text;
    while (!rest.empty()) {
      const ssize_t sent =
          send(fd_, rest.data(), rest.size(), kPosixCalibrateSendFlags);
      if (sent <= 0) {
        return;
      }
      rest.remove_prefix(static_cast<std::size_t>(sent));
    }
  }

  std::optional<std::string> ReadLine() override {
    for (;;) {
      const std::size_t newline = pending_.find('\n');
      if (newline != std::string::npos) {
        std::string line = pending_.substr(0, newline);
        pending_.erase(0, newline + 1);
        return line;
      }
      std::array<char, kReadChunkBytes> chunk{};
      const ssize_t got = recv(fd_, chunk.data(), chunk.size(), 0);
      if (got <= 0) {
        // Includes SO_RCVTIMEO's own timeout (-1, errno == EAGAIN or
        // EWOULDBLOCK): a daemon that stops answering ends this read the
        // same way one that hangs up outright does, rather than blocking
        // forever.
        return std::nullopt;
      }
      pending_.append(chunk.data(), static_cast<std::size_t>(got));
    }
  }

 private:
  static constexpr std::size_t kReadChunkBytes = 512;
  // Long enough for a person to finish a calibration step, short enough
  // that a daemon gone quiet is noticed well within one wizard session.
  static constexpr time_t kReceiveTimeoutSeconds = 30;

  int fd_ = -1;
  std::string pending_;
};

}  // namespace facepalm

#endif  // FACEPALM_TOOLS_POSIX_CALIBRATE_CLIENT_IO_H_
