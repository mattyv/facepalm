#include "src/socket_ready.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace facepalm {
namespace {
constexpr int kProbeTimeoutMs = 50;
}  // namespace

bool SocketReady(const std::string& path, const SocketReadyCalls& calls) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) {
    return false;
  }
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  const int fd = calls.socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  bool ready = false;
  const int flags = calls.get_flags(fd);
  if (flags >= 0 &&
      calls.set_flags(
          fd, static_cast<int>(static_cast<unsigned>(flags) |
                               static_cast<unsigned>(O_NONBLOCK))) == 0) {
    const int result = calls.connect(
        fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == 0) {
      ready = true;
    } else if (errno == EINPROGRESS) {
      pollfd item{.fd = fd, .events = POLLOUT, .revents = 0};
      int error = 0;
      socklen_t length = sizeof(error);
      ready =
          calls.poll(&item, 1, kProbeTimeoutMs) > 0 &&
          (static_cast<unsigned>(item.revents) &
           static_cast<unsigned>(POLLOUT)) != 0 &&
          calls.getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
          error == 0;
    }
  }
  calls.close(fd);
  return ready;
}

}  // namespace facepalm
