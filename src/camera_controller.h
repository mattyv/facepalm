#ifndef FACEPALM_SRC_CAMERA_CONTROLLER_H_
#define FACEPALM_SRC_CAMERA_CONTROLLER_H_

#include <chrono>
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

// Why an open attempt failed. All three causes are retried while wanted.
enum class OpenFailure : std::uint8_t {
  kNoDevice,
  kPermissionDenied,
  kStartFailed
};

// Bounded facts from camera selection; no OS error strings cross queues.
enum class ConfigureError : std::uint8_t {
  kNoCandidate,
  kPermissionDenied,
  kQueryFailed,
  kUnsupported
};

// What a camera reports, each a specs/CameraLifecycle.tla action.
class CameraReports {
 public:
  virtual ~CameraReports() = default;
  virtual void OnOpened() = 0;
  virtual void OnOpenFailed(OpenFailure reason,
                            std::optional<ConfigureError> detail) = 0;
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

  // Schedules one delayed retry on the control queue. The callback checks
  // its generation before it may change the current attempt.
  using RetrySchedule =
      std::function<void(std::chrono::seconds, std::function<void()> retry)>;

  explicit CameraController(
      Camera& camera, Clients& clients, bool consent,
      RetrySchedule schedule_retry = [](std::chrono::seconds,
                                        const std::function<void()>&) {});

  void GrantConsent();
  void RevokeConsent();
  void Subscribe(const ClientId& client);
  void Leave(const ClientId& client);

  // The camera's reports.
  void OnOpened() override;
  void OnOpenFailed(OpenFailure reason,
                    std::optional<ConfigureError> detail) override;
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
  void ScheduleRetry();
  void Retry(std::uint64_t generation);
  void CancelRetry();

  Camera& camera_;
  Clients& clients_;
  bool consent_;
  bool device_ = true;
  // Set by any open failure until a current retry or cancellation.
  bool blocked_ = false;
  RetrySchedule schedule_retry_;
  std::uint64_t retry_generation_ = 0;
  std::chrono::seconds retry_delay_{1};
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
