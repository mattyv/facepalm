#ifndef FACEPALM_SRC_DAEMON_STATUS_H_
#define FACEPALM_SRC_DAEMON_STATUS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace facepalm {

// health-v1 serializes these enum values numerically; preserve their order.
enum class ServicePhase : std::uint8_t {
  kIdle, kOpening, kActive, kRetrying, kFailed
};

enum class ServiceReason : std::uint8_t {
  kNone, kNoCandidate, kPermissionDenied, kQueryFailed, kUnsupported,
  kStartFailed, kModelFailure
};

struct DaemonHealth {
  ServicePhase phase;
  ServiceReason reason;
  std::string detail;
  std::int64_t pid;
  // PID existence alone does not establish service identity: PIDs can be reused.
  bool alive;
};

bool PublishDaemonHealth(const std::string& path, bool camera_on,
                         ServicePhase phase, ServiceReason reason,
                         std::string_view detail, std::string& error);
std::optional<DaemonHealth> ReadDaemonHealth(const std::string& path);


// The camera-in-use indicator (#37 item 8, #61), following
// specs/CameraStatus.tla. `path` is UserPaths::status, beside the
// service's socket and the client token files (#89): no second token or
// path of its own.

// CameraStatus!DemandChanges, CameraStatus!OpenFails, CameraStatus!
// OpenSucceeds, CameraStatus!Restart, CameraStatus!Shutdown's own write:
// writes `path` to say the camera is on or off, replacing it atomically
// (a temporary file in the same directory, renamed over it) so a reader
// never sees a half-written file, with the calling process's own pid
// alongside it (#184). `path`'s directory must already exist. False,
// with `error` saying why, when it cannot write there.
bool PublishCameraStatus(const std::string& path, bool on, std::string& error);

// CameraStatus!Poll: true when `path` was last published on by a pid
// that is still alive (#184, GuardReadChecksAlive) -- standing for the
// daemon that published it still being the one running, so a daemon
// that died with the camera on, with no chance to publish off itself
// (CameraStatus!Crash), does not leave a reader stuck seeing on forever.
// A missing file, one that cannot be read, one that does not hold what
// Publish writes, or one whose pid is no longer alive, all read as off,
// since there is no camera to be in use without a daemon running to
// have published anything at all.
bool ReadCameraStatus(const std::string& path);

}  // namespace facepalm

#endif  // FACEPALM_SRC_DAEMON_STATUS_H_
