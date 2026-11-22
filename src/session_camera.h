#ifndef FACEPALM_SRC_SESSION_CAMERA_H_
#define FACEPALM_SRC_SESSION_CAMERA_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "src/camera_controller.h"
#include "src/camera_directory.h"
#include "src/post.h"

namespace facepalm {

// One OS's capture API, kept to the calls a camera needs, so that the logic
// around it is tested with the API mocked. Calls may block.
class CaptureSession : public CameraDirectory {
 public:
  enum class Permission : std::uint8_t { kGranted, kUndetermined, kDenied };

  // One of the camera's supported frame-rate ranges, in frames a second.
  // A camera can have several, kept apart rather than collapsed into one
  // overall minimum and maximum (#43): two ranges such as 15 and 30 do not
  // make every rate between them supported, and a single fixed rate such
  // as 29.97 rounded outward to a span of integers, {29, 30}, no longer
  // has `min` above `max`, which a collapsed {ceil 30, floor 29} once did.
  struct FrameRateRange {
    int min = 0;
    int max = 0;
  };
  // The configured camera's frame-rate ranges; empty when the camera
  // reports none, which asks for the wanted rate as it is.
  using FrameRates = std::vector<FrameRateRange>;

  ~CaptureSession() override = default;
  virtual Permission CurrentPermission() = 0;
  // Asks the user, and calls `done` with the answer, on any thread.
  virtual void RequestPermission(std::function<void(bool granted)> done) = 0;
  // Sets the session up for the configured camera (item 10 of #61), and
  // returns its unique ID, or nothing when it cannot be used: there is no
  // such camera, or (with none configured) no default.
  virtual std::optional<std::string> Configure() = 0;
  // The configured camera's frame rates.
  virtual FrameRates SupportedFrameRates() = 0;
  // Starts capturing at `frames_per_second`, which is within one of
  // SupportedFrameRates' ranges, stamping each frame with `attempt`.
  // Returns whether it is running.
  virtual bool Start(int frames_per_second, std::uint64_t attempt) = 0;
  virtual void Stop() = 0;
};

// The rate nearest `wanted` that one of `rates`' ranges supports, or
// `wanted` as it is when the camera reports no ranges (#43): collapsing
// them into one overall minimum and maximum let `Start` be asked for a
// rate none of the camera's actual ranges covered. Exposed so #120's
// property test can check it always lies within one of `rates`' ranges.
int NearestSupportedRate(int wanted, const CaptureSession::FrameRates& rates);

// A camera over a CaptureSession. Session work runs on `session`, since it
// blocks; every report runs on `control`, the queue that owns the controller,
// so each is one specs/CameraLifecycle.tla action. It asks for the frame rate
// nearest the one wanted that the camera supports; a slower rate is the
// pipeline's to make, by skipping frames.
class SessionCamera : public Camera {
 public:
  SessionCamera(CaptureSession& capture, CameraReports& reports, Post session,
                Post control, int frames_per_second);

  void Open(std::uint64_t attempt) override;
  void Close() override;

  // What the OS reports, on any thread: the session failed, or a camera,
  // named by its unique ID, went away or came back. Only the camera in use
  // counts, or, before one is in use, any camera coming back (#39).
  void OnSessionFailed();
  void OnDeviceDisconnected(std::string id);
  void OnDeviceConnected(std::string id);

 private:
  void Start(std::uint64_t attempt);
  void Report(void (CameraReports::*report)());
  void ReportOpenFailed(OpenFailure reason);

  CaptureSession& capture_;
  CameraReports& reports_;
  Post session_;
  Post control_;
  int frames_per_second_;
  // The camera in use, touched only on the session queue.
  std::string device_id_;
  // The open attempt a start may still serve, or kNoAttempt once closed,
  // touched only on the session queue (#38).
  static constexpr std::uint64_t kNoAttempt = 0;
  std::uint64_t attempt_ = kNoAttempt;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SESSION_CAMERA_H_
