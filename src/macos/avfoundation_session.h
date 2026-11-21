#ifndef FACEPALM_SRC_MACOS_AVFOUNDATION_SESSION_H_
#define FACEPALM_SRC_MACOS_AVFOUNDATION_SESSION_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/camera_directory.h"
#include "src/frame.h"
#include "src/session_camera.h"

namespace facepalm {

// The macOS capture API, through AVFoundation, and nothing else: the logic
// around it is SessionCamera's, tested with this mocked. It follows
// MediaPipe's mediapipe/objc/MPPCameraInputSource.m: a capture session with a
// device input, and a video data output that drops late frames.
//
// Frames go to `on_frame` on a capture queue of its own. A runtime error calls
// `on_session_failed`, and any camera disconnecting or connecting calls
// `on_disconnected` or `on_connected` with its unique ID, on any thread.
//
// `device_id` is the person's choice (item 10 of #61, #13): empty for the
// default, which Configure resolves to the built-in camera specifically,
// by its AVCaptureDeviceTypeBuiltInWideAngleCamera device type, never a
// Continuity Camera or other external device that
// `+defaultDeviceWithMediaType:` might otherwise hand back. A non-empty
// `device_id` is matched by AVCaptureDevice's own uniqueID, built-in or
// not, across every discoverable type.
class AvFoundationSession : public CaptureSession {
 public:
  AvFoundationSession(std::string device_id,
                      std::function<void(const Frame&)> on_frame,
                      std::function<void()> on_session_failed,
                      std::function<void(const std::string&)> on_disconnected,
                      std::function<void(const std::string&)> on_connected);
  ~AvFoundationSession() override;
  AvFoundationSession(const AvFoundationSession&) = delete;
  AvFoundationSession& operator=(const AvFoundationSession&) = delete;

  Permission CurrentPermission() override;
  void RequestPermission(std::function<void(bool granted)> done) override;
  std::optional<std::string> Configure() override;
  FrameRates SupportedFrameRates() override;
  bool Start(int frames_per_second, std::uint64_t attempt) override;
  void Stop() override;
  std::vector<CameraDevice> List() override;

 private:
  struct Impl;
  std::string device_id_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_MACOS_AVFOUNDATION_SESSION_H_
