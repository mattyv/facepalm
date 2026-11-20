#ifndef FACEPALM_SRC_LINUX_V4L2_SESSION_H_
#define FACEPALM_SRC_LINUX_V4L2_SESSION_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "src/frame.h"
#include "src/session_camera.h"

namespace facepalm {

// The configured device and capture thread belong to the camera queue. Stop
// joins the thread before closing its descriptor or acknowledging retirement.
class V4l2Session final : public CaptureSession {
 public:
  V4l2Session(std::string device, std::function<void(const Frame&)> on_frame,
              std::function<void()> on_session_failed);
  ~V4l2Session() override;
  V4l2Session(const V4l2Session&) = delete;
  V4l2Session& operator=(const V4l2Session&) = delete;

  Permission CurrentPermission() override;
  void RequestPermission(std::function<void(bool)> done) override;
  std::optional<std::string> Configure() override;
  FrameRates SupportedFrameRates() override;
  bool Start(int frames_per_second, std::uint64_t attempt) override;
  void Stop() override;

 private:
  std::string device_;
  std::function<void(const Frame&)> on_frame_;
  std::function<void()> on_session_failed_;
  int fd_ = -1;
  std::jthread capture_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LINUX_V4L2_SESSION_H_
