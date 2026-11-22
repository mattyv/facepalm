#ifndef FACEPALM_SRC_DAEMON_STATUS_H_
#define FACEPALM_SRC_DAEMON_STATUS_H_

#include <string>

namespace facepalm {

// The camera-in-use indicator (#37 item 8, #61), following
// specs/CameraStatus.tla. `path` is UserPaths::status, beside the
// service's socket and the client token files (#89): no second token or
// path of its own.

// CameraStatus!Publish: writes `path` to say the camera is on or off,
// replacing it atomically (a temporary file in the same directory,
// renamed over it) so a reader never sees a half-written file. `path`'s
// directory must already exist. False, with `error` saying why, when it
// cannot write there.
bool PublishCameraStatus(const std::string& path, bool on, std::string& error);

// CameraStatus!Poll: true when `path` was last published on. A missing
// file, one that cannot be read, or one that does not hold what Publish
// writes all read as off, since there is no camera to be in use without
// a daemon running to have published anything at all.
bool ReadCameraStatus(const std::string& path);

}  // namespace facepalm

#endif  // FACEPALM_SRC_DAEMON_STATUS_H_
