#ifndef FACEPALM_SRC_SESSION_CAMERA_H_
#define FACEPALM_SRC_SESSION_CAMERA_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "src/camera_controller.h"
#include "src/post.h"

namespace facepalm {

// One OS's capture API, kept to the calls a camera needs, so that the logic
// around it is tested with the API mocked. Calls may block.
class CaptureSession {
 public:
  enum class Permission : std::uint8_t { kGranted, kUndetermined, kDenied };

  // The frame rates the configured camera supports, in frames a second.
  struct FrameRates {
    int min = 0;
    int max = 0;
  };

  virtual ~CaptureSession() = default;
  virtual Permission CurrentPermission() = 0;
  // Asks the user, and calls `done` with the answer, on any thread.
  virtual void RequestPermission(std::function<void(bool granted)> done) = 0;
  // Sets the session up for the default camera, and returns its unique ID, or
  // nothing when there is no camera it can use.
  virtual std::optional<std::string> Configure() = 0;
  // The configured camera's frame rates.
  virtual FrameRates SupportedFrameRates() = 0;
  // Starts capturing at `frames_per_second`, which is within
  // SupportedFrameRates. Returns whether it is running.
  virtual bool Start(int frames_per_second) = 0;
  virtual void Stop() = 0;
};

// A camera over a CaptureSession. Session work runs on `session`, since it
// blocks; every report runs on `control`, the queue that owns the controller,
// so each is one specs/CameraLifecycle.tla action. It asks for the frame rate
// nearest the one wanted that the camera supports; a slower rate is the
// pipeline's to make, by skipping frames.
class SessionCamera : public Camera {
 public:
  SessionCamera(CaptureSession& capture, CameraReports& reports, Post session,
                Post control, int frames_per_second);

  void Open() override;
  void Close() override;

  // What the OS reports, on any thread: the session failed, or a camera,
  // named by its unique ID, went away or came back. Only the camera in use
  // counts.
  void OnSessionFailed();
  void OnDeviceDisconnected(std::string id);
  void OnDeviceConnected(std::string id);

 private:
  void Start();
  void Report(void (CameraReports::*report)());

  CaptureSession& capture_;
  CameraReports& reports_;
  Post session_;
  Post control_;
  int frames_per_second_;
  // The camera in use, touched only on the session queue.
  std::string device_id_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SESSION_CAMERA_H_
