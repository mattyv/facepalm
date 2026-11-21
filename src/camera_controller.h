#ifndef FACEPALM_SRC_CAMERA_CONTROLLER_H_
#define FACEPALM_SRC_CAMERA_CONTROLLER_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>

namespace facepalm {

// A client of the service, as the transport names it.
using ClientId = std::string;

// What the face pipeline made of one frame.
// ponytail: JSON text until the pipeline defines its own type.
using Reaction = std::string;

// Why an open attempt failed (#52, item 10 of #61), as
// specs/CameraLifecycle.tla's `Start` tells apart: no camera is usable
// (`device` false in the spec), the OS denied the camera permission
// (`~permitted`), or Start needed something else that was not there
// (`~startWorks`), such as a configuration race. Only the device cause
// waits for a device-connected report to recover; the other two are
// retried (GuardRetries).
enum class OpenFailure : std::uint8_t {
  kNoDevice,
  kPermissionDenied,
  kStartFailed
};

// What a camera reports, each a specs/CameraLifecycle.tla action.
class CameraReports {
 public:
  virtual ~CameraReports() = default;
  virtual void OnOpened() = 0;
  virtual void OnOpenFailed(OpenFailure reason) = 0;
  virtual void OnClosed() = 0;
  virtual void OnDeviceLost() = 0;
  virtual void OnDeviceBack() = 0;
};

// The camera device, one implementation per OS. Both calls return at once;
// the device reports back through CameraReports.
class Camera {
 public:
  virtual ~Camera() = default;
  // Opens for one attempt: frames it captures carry `attempt` (Frame), and
  // a Close ends it (specs/CameraLifecycle.tla's EndAttempt).
  virtual void Open(std::uint64_t attempt) = 0;
  virtual void Close() = 0;
};

// Sends reactions to clients.
class Clients {
 public:
  virtual ~Clients() = default;
  virtual void Deliver(const ClientId& client, const Reaction& reaction) = 0;
};

// Decides when the camera may be on, as specs/CameraLifecycle.tla specifies:
// only with the user's consent and while a client is subscribed. Each method
// names the spec action it implements. Call it from one thread.
//
// A restarted helper builds a new controller, which starts with the camera
// closed and no clients (the spec's Restart with GuardRestartRechecks).
// Gives captured frames tickets, and takes their reactions back. The frame
// pipeline's view of the controller, so its tests mock it.
class FrameTickets {
 public:
  virtual ~FrameTickets() = default;
  // Returns a ticket for a frame the camera captured in `attempt`, or
  // nothing when the camera is not open or that attempt has ended, and the
  // frame must be dropped.
  virtual std::optional<std::uint64_t> OnFrameCaptured(
      std::uint64_t attempt) = 0;
  // Delivers a frame's reaction to every subscribed client, unless consent
  // was revoked after the frame was captured.
  virtual void OnReaction(std::uint64_t ticket, const Reaction& reaction) = 0;
};

class CameraController : public CameraReports, public FrameTickets {
 public:
  enum class State : std::uint8_t { kClosed, kOpening, kOpen, kClosing };

  // Schedules `retry` to run later, on the control queue, for a denied
  // permission or a failed start (#52). The default never calls it back,
  // which is GuardRetries off: a blocked camera stays blocked, as today's
  // code leaves it.
  using RetrySchedule = std::function<void(std::function<void()> retry)>;

  explicit CameraController(
      Camera& camera, Clients& clients, bool consent,
      RetrySchedule schedule_retry = [](const std::function<void()>&) {});

  void GrantConsent();
  void RevokeConsent();
  void Subscribe(const ClientId& client);
  void Leave(const ClientId& client);

  // The camera's reports.
  void OnOpened() override;
  void OnOpenFailed(OpenFailure reason) override;
  void OnClosed() override;
  void OnDeviceLost() override;
  void OnDeviceBack() override;

  std::optional<std::uint64_t> OnFrameCaptured(std::uint64_t attempt) override;
  void OnReaction(std::uint64_t ticket, const Reaction& reaction) override;

  [[nodiscard]] State CurrentState() const { return state_; }
  [[nodiscard]] std::uint64_t CurrentAttempt() const { return attempt_; }

 private:
  void OpenIfWanted();
  void StartClosing();
  // specs/CameraLifecycle.tla, Retry: clears `blocked_` and tries again.
  void Retry();

  Camera& camera_;
  Clients& clients_;
  bool consent_;
  bool device_ = true;
  // Set by a denied permission or a failed start, and cleared only by
  // Retry (#52); unlike `device_`, nothing but Retry clears it.
  bool blocked_ = false;
  RetrySchedule schedule_retry_;
  std::set<ClientId> subscribers_;
  // The current open attempt; opening starts one and closing ends it.
  std::uint64_t attempt_ = 0;
  State state_ = State::kClosed;
  // Bumped when consent is revoked or the last subscriber leaves, voiding
  // tickets from the preceding authorized session.
  std::uint64_t consent_epoch_ = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CAMERA_CONTROLLER_H_
