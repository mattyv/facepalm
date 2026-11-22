#ifndef FACEPALM_SRC_CAMERA_DIRECTORY_H_
#define FACEPALM_SRC_CAMERA_DIRECTORY_H_

#include <string>
#include <vector>

namespace facepalm {

// One camera the system can use, for the person to choose from (item 10 of
// #61, #13). `id` is stable across a reboot (macOS: AVCaptureDevice's
// uniqueID; Linux: v4l2_capability's bus_info), so a choice saved in
// Config survives one. `built_in` is the device the empty choice (the
// default) means: never a Continuity Camera or other external device,
// chosen silently.
struct CameraDevice {
  std::string id;
  std::string name;
  bool built_in = false;
};

// The cameras available right now, independent of which one is configured.
// Implemented by CaptureSession (AvFoundationSession, V4l2Session): unlike
// its other methods, which only run on the session queue SessionCamera
// posts to, `List` is safe to call from any thread, so a client connection
// can ask for it directly (src/service.cc).
class CameraDirectory {
 public:
  virtual ~CameraDirectory() = default;
  virtual std::vector<CameraDevice> List() = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CAMERA_DIRECTORY_H_
