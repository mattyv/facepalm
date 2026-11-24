#ifndef FACEPALM_SRC_SOCKET_READY_H_
#define FACEPALM_SRC_SOCKET_READY_H_

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

namespace facepalm {

// System calls are injectable so the error paths can be tested in isolation.
struct SocketReadyCalls {
  int (*socket)(int, int, int) = ::socket;
  int (*fcntl)(int, int, ...) = ::fcntl;
  int (*connect)(int, const sockaddr*, socklen_t) = ::connect;
  int (*poll)(pollfd*, nfds_t, int) = ::poll;
  int (*getsockopt)(int, int, int, void*, socklen_t*) = ::getsockopt;
  int (*close)(int) = ::close;
};

// Probes a Unix socket without sending a request or starting a service.
// A connection in progress is given at most 50 ms; failure or interruption
// returns false. A successful connection does not guarantee future requests.
bool SocketReady(const std::string& path,
                 const SocketReadyCalls& calls = SocketReadyCalls{});

}  // namespace facepalm
#endif  // FACEPALM_SRC_SOCKET_READY_H_
