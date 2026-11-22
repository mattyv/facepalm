#ifndef FACEPALM_TOOLS_POSIX_CALIBRATE_CLIENT_IO_H_
#define FACEPALM_TOOLS_POSIX_CALIBRATE_CLIENT_IO_H_

// A plain POSIX socket behind src/calibrate_cli.h's CalibrateClientIo
// (#36, #35, item 12 of #61), shared by tools/calibrate_cli/main.cc and,
// on macOS, tools/menu/main.mm (#118): untested POSIX glue by this
// repo's own convention, the same as tools/gesture_replay/main.cc's.
#include <sys/socket.h>
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

class PosixCalibrateClientIo : public CalibrateClientIo {
 public:
  ~PosixCalibrateClientIo() override {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  bool Connect(const std::string& path) override {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
      return false;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    return fd_ >= 0 && connect(fd_, reinterpret_cast<const sockaddr*>(&address),
                               sizeof(address)) == 0;
  }

  void WriteLine(std::string_view line) override {
    std::string text(line);
    text += '\n';
    std::string_view rest = text;
    while (!rest.empty()) {
      const ssize_t sent = send(fd_, rest.data(), rest.size(), 0);
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
        return std::nullopt;
      }
      pending_.append(chunk.data(), static_cast<std::size_t>(got));
    }
  }

 private:
  static constexpr std::size_t kReadChunkBytes = 512;

  int fd_ = -1;
  std::string pending_;
};

}  // namespace facepalm

#endif  // FACEPALM_TOOLS_POSIX_CALIBRATE_CLIENT_IO_H_
