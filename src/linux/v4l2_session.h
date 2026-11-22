#ifndef FACEPALM_SRC_LINUX_V4L2_SESSION_H_
#define FACEPALM_SRC_LINUX_V4L2_SESSION_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "src/camera_directory.h"
#include "src/frame.h"
#include "src/linux/v4l2_hotplug.h"
#include "src/session_camera.h"

namespace facepalm {

// The configured device and capture thread belong to the camera queue. Stop
// joins the thread before closing its descriptor or acknowledging retirement.
//
// `device_id` is the person's choice (item 10 of #61, #13): empty for the
// default, which Configure resolves to the first built-in camera it finds
// (CameraDevice::built_in), never an external one chosen silently. A
// non-empty `device_id` is matched by List's own `id`, a v4l2_capability's
// `bus_info`, which stays the same across a reboot, unlike the node number
// (`/dev/videoN`) a camera happens to land on.
//
// `on_disconnected` and `on_connected` are the callbacks AvFoundationSession
// already takes (#111, #13): `hotplug` is asked to call them, over inotify
// rather than AVFoundation's own notifications. The caller owns `hotplug`
// and keeps it alive at least as long as this session.
class V4l2Session final : public CaptureSession {
 public:
  V4l2Session(std::string device_id, std::function<void(const Frame&)> on_frame,
              std::function<void()> on_session_failed,
              std::function<void(const std::string&)> on_disconnected,
              std::function<void(const std::string&)> on_connected,
              HotplugMonitor& hotplug);
  ~V4l2Session() override;
  V4l2Session(const V4l2Session&) = delete;
  V4l2Session& operator=(const V4l2Session&) = delete;

  Permission CurrentPermission() override;
  void RequestPermission(std::function<void(bool)> done) override;
  std::optional<std::string> Configure() override;
  FrameRates SupportedFrameRates() override;
  bool Start(int frames_per_second, std::uint64_t attempt) override;
  void Stop() override;
  std::vector<CameraDevice> List() override;

 private:
  std::string device_id_;
  std::function<void(const Frame&)> on_frame_;
  std::function<void()> on_session_failed_;
  int fd_ = -1;
  std::jthread capture_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LINUX_V4L2_SESSION_H_
