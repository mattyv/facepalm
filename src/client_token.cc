#include "src/client_token.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <optional>
#include <string>

namespace facepalm {
namespace {
constexpr std::size_t kTokenLength = 64;
constexpr mode_t kOtherPermissions = S_IRWXG | S_IRWXO;
class PosixTokenIo : public ClientTokenIo {
 public:
  int Open(const std::string& path) override {
    return open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  }
  int Stat(int fd, struct stat& result) override { return fstat(fd, &result); }
  ssize_t Read(int fd, void* buffer, std::size_t count) override {
    return read(fd, buffer, count);
  }
  void Close(int fd) override { close(fd); }
  uid_t User() override { return geteuid(); }
};
}  // namespace
std::optional<std::string> LoadClientToken(const std::string& path,
                                           ClientTokenIo& io) {
  const int fd = io.Open(path);
  if (fd < 0) {
    return std::nullopt;
  }
  struct stat info{};
  if (io.Stat(fd, info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_uid != io.User() || (info.st_mode & kOtherPermissions) != 0) {
    io.Close(fd);
    return std::nullopt;
  }
  std::array<char, kTokenLength + 2> bytes{};
  std::size_t count = 0;
  while (count < bytes.size()) {
    const ssize_t n = io.Read(fd, bytes.data() + count, bytes.size() - count);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      io.Close(fd);
      return std::nullopt;
    }
    if (n == 0) {
      break;
    }
    count += static_cast<std::size_t>(n);
  }
  io.Close(fd);
  if (count == kTokenLength + 1 && bytes[kTokenLength] == '\n') {
    --count;
  }
  if (count != kTokenLength) {
    return std::nullopt;
  }
  std::string token(bytes.data(), count);
  if (!std::ranges::all_of(token, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      })) {
    return std::nullopt;
  }
  return token;
}
std::optional<std::string> LoadClientToken(const std::string& path) {
  PosixTokenIo io;
  return LoadClientToken(path, io);
}
}  // namespace facepalm
