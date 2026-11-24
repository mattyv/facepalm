#include "src/socket_ready.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>

namespace facepalm {
namespace {
constexpr int kProbeTimeoutMs = 50;

std::optional<std::int64_t> PeerPid(int fd, const SocketReadyCalls& calls) {
#ifdef __linux__
  ucred credentials{};
  socklen_t length = sizeof(credentials);
  if (calls.getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) ==
          0 &&
      length == sizeof(credentials) && credentials.pid > 0) {
    return static_cast<std::int64_t>(credentials.pid);
  }
#elif defined(__APPLE__)
  pid_t pid = 0;
  socklen_t length = sizeof(pid);
  if (calls.getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &length) == 0 &&
      length == sizeof(pid) && pid > 0) {
    return static_cast<std::int64_t>(pid);
  }
#else
  (void)fd;
  (void)calls;
#endif
  return std::nullopt;
}
}  // namespace

SocketProbeResult ProbeSocketReady(const std::string& path,
                                   const SocketReadyCalls& calls) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) {
    return {.ready = false, .peer_pid = std::nullopt};
  }
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  const int fd = calls.socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return {.ready = false, .peer_pid = std::nullopt};
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
  const auto peer_pid = ready ? PeerPid(fd, calls) : std::nullopt;
  calls.close(fd);
  return {.ready = ready, .peer_pid = peer_pid};
}

bool SocketReady(const std::string& path, const SocketReadyCalls& calls) {
  return ProbeSocketReady(path, calls).ready;
}

}  // namespace facepalm
