#ifndef FACEPALM_SRC_MACOS_AVFOUNDATION_SESSION_H_
#define FACEPALM_SRC_MACOS_AVFOUNDATION_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "src/session_camera.h"

namespace facepalm {

// One captured frame, valid only during the call that hands it over.
struct Frame {
  int width = 0;
  int height = 0;
  std::size_t bytes_per_row = 0;
  const std::uint8_t* bgra = nullptr;  // 32BGRA, as MediaPipe's camera uses.
};

// The macOS capture API, through AVFoundation, and nothing else: the logic
// around it is SessionCamera's, tested with this mocked. It follows
// MediaPipe's mediapipe/objc/MPPCameraInputSource.m: a capture session with a
// device input, and a video data output that drops late frames.
//
// Frames go to `on_frame` on a capture queue of its own. A runtime error or a
// disconnect calls `on_device_lost`, and a device connecting calls
// `on_device_back`, on any thread.
class AvFoundationSession : public CaptureSession {
 public:
  AvFoundationSession(std::function<void(const Frame&)> on_frame,
                      std::function<void()> on_device_lost,
                      std::function<void()> on_device_back);
  ~AvFoundationSession() override;
  AvFoundationSession(const AvFoundationSession&) = delete;
  AvFoundationSession& operator=(const AvFoundationSession&) = delete;

  Permission CurrentPermission() override;
  void RequestPermission(std::function<void(bool granted)> done) override;
  bool Configure() override;
  FrameRates SupportedFrameRates() override;
  bool Start(int frames_per_second) override;
  void Stop() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_MACOS_AVFOUNDATION_SESSION_H_
