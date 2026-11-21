// A thin wrapper around src/calibrate_cli.h's tested logic, following
// tools/gesture_replay/main.cc's pattern: this file is not unit tested
// itself, only the real POSIX socket it opens.
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/calibrate_cli.h"

namespace facepalm {
namespace {

constexpr std::size_t kReadChunkBytes = 512;

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
  int fd_ = -1;
  std::string pending_;
};

}  // namespace
}  // namespace facepalm

// NOLINTNEXTLINE(misc-const-correctness,bugprone-exception-escape)
int main(int argc, char** argv) {
  const std::vector<std::string> arguments(argv + 1, argv + argc);
  facepalm::PosixCalibrateClientIo io;
  return facepalm::RunCalibrateCli(arguments, io, std::cout, std::cerr);
}
