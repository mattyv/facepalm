#ifndef FACEPALM_SRC_MACOS_AVFOUNDATION_CAMERA_H_
#define FACEPALM_SRC_MACOS_AVFOUNDATION_CAMERA_H_

#include <dispatch/dispatch.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "src/camera_controller.h"

namespace facepalm {

// One captured frame, valid only during the call that hands it over.
struct Frame {
  int width = 0;
  int height = 0;
  std::size_t bytes_per_row = 0;
  const std::uint8_t* bgra = nullptr;  // 32BGRA, as MediaPipe's camera uses.
};

// The macOS camera, through AVFoundation. It follows MediaPipe's
// mediapipe/objc/MPPCameraInputSource.m: a capture session with a device
// input and a video data output that drops late frames.
//
// Every report runs on `control`, the serial queue that owns the controller,
// so each one is a single specs/CameraLifecycle.tla action. Frames go to
// `on_frame` on a capture queue of the camera's own.
class AvFoundationCamera : public Camera {
 public:
  static constexpr int kDefaultFramesPerSecond = 15;

  struct Options {
    int frames_per_second = kDefaultFramesPerSecond;
  };

  AvFoundationCamera(dispatch_queue_t control, CameraReports& reports,
                     Options options,
                     std::function<void(const Frame&)> on_frame);
  ~AvFoundationCamera() override;
  AvFoundationCamera(const AvFoundationCamera&) = delete;
  AvFoundationCamera& operator=(const AvFoundationCamera&) = delete;

  void Open() override;
  void Close() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_MACOS_AVFOUNDATION_CAMERA_H_
