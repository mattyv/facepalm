#ifndef FACEPALM_SRC_CAMERA_CONTROLLER_H_
#define FACEPALM_SRC_CAMERA_CONTROLLER_H_

#include <cstdint>
#include <optional>
#include <set>
#include <string>

namespace facepalm {

// A client of the service, as the transport names it.
using ClientId = std::string;

// What the face pipeline made of one frame.
// ponytail: JSON text until the pipeline defines its own type.
using Reaction = std::string;

// The camera device, one implementation per OS. Both calls return at once;
// the device reports back through CameraController's On methods.
class Camera {
 public:
  virtual ~Camera() = default;
  virtual void Open() = 0;
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
class CameraController {
 public:
  enum class State : std::uint8_t { kClosed, kOpening, kOpen, kClosing };

  CameraController(Camera& camera, Clients& clients, bool consent);

  void GrantConsent();
  void RevokeConsent();
  void Subscribe(const ClientId& client);
  void Leave(const ClientId& client);

  // The camera's reports.
  void OnOpened();
  void OnOpenFailed();
  void OnClosed();
  void OnDeviceLost();
  void OnDeviceBack();

  // Returns a ticket for a frame the camera captured, or nothing when the
  // camera is not open and the frame must be dropped.
  std::optional<std::uint64_t> OnFrameCaptured();

  // Delivers a frame's reaction to every subscribed client, unless consent
  // was revoked after the frame was captured.
  void OnReaction(std::uint64_t ticket, const Reaction& reaction);

  [[nodiscard]] State CurrentState() const { return state_; }

 private:
  void OpenIfWanted();
  void StartClosing();

  Camera& camera_;
  Clients& clients_;
  bool consent_;
  bool device_ = true;
  std::set<ClientId> subscribers_;
  State state_ = State::kClosed;
  // Bumped when consent is revoked, which voids every ticket given before.
  std::uint64_t consent_epoch_ = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CAMERA_CONTROLLER_H_
