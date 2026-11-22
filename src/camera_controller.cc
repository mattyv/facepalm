#include "src/camera_controller.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace facepalm {

CameraController::CameraController(Camera& camera, Clients& clients,
                                   bool consent, RetrySchedule schedule_retry)
    : camera_(camera),
      clients_(clients),
      consent_(consent),
      schedule_retry_(std::move(schedule_retry)) {}

// specs/CameraLifecycle.tla, GrantConsent.
void CameraController::GrantConsent() {
  consent_ = true;
  OpenIfWanted();
}

// specs/CameraLifecycle.tla, RevokeConsent, with GuardRevokeCloses and
// GuardRevokeDiscards: a new epoch voids every ticket in flight.
void CameraController::RevokeConsent() {
  if (!consent_) {
    return;
  }
  consent_ = false;
  ++consent_epoch_;
  StartClosing();
}

// specs/CameraLifecycle.tla, Subscribe.
void CameraController::Subscribe(const ClientId& client) {
  subscribers_.insert(client);
  OpenIfWanted();
}

// specs/CameraLifecycle.tla, Leave, with GuardLastLeaveCloses.
void CameraController::Leave(const ClientId& client) {
  if (subscribers_.erase(client) == 0) {
    return;
  }
  if (subscribers_.empty()) {
    ++consent_epoch_;
    StartClosing();
  }
}

// specs/CameraLifecycle.tla, Opened. A report that arrives after closing
// began is ignored: Opened is not enabled while closing.
void CameraController::OnOpened() {
  if (state_ == State::kOpening) {
    state_ = State::kOpen;
  }
}

// specs/CameraLifecycle.tla, Start's failure branches. With no device, it
// stays unusable until DeviceBack, as before. A denied permission or a
// failed start (#52) blocks it instead, cleared only by Retry: unlike the
// device cause, nothing the OS reports on its own clears this one.
void CameraController::OnOpenFailed(OpenFailure reason) {
  if (state_ != State::kOpening) {
    return;
  }
  state_ = State::kClosed;
  if (reason == OpenFailure::kNoDevice) {
    device_ = false;
    return;
  }
  blocked_ = true;
  schedule_retry_([this] { Retry(); });
}

// specs/CameraLifecycle.tla, Retry, with GuardRetries (#52). Nothing but
// Retry clears `blocked_`, and OpenIfWanted will not open while it holds,
// so the camera is still closed and blocked when a scheduled retry runs.
void CameraController::Retry() {
  blocked_ = false;
  OpenIfWanted();
}

// specs/CameraLifecycle.tla, Closed.
void CameraController::OnClosed() {
  if (state_ != State::kClosing) {
    return;
  }
  state_ = State::kClosed;
  OpenIfWanted();
}

// specs/CameraLifecycle.tla, DeviceLost: the OS dropped an open camera, which
// ends its attempt. One still opening reports OpenFailed in its own time.
void CameraController::OnDeviceLost() {
  device_ = false;
  if (state_ == State::kOpen) {
    state_ = State::kClosed;
    ++attempt_;
  }
}

// specs/CameraLifecycle.tla, DeviceBack.
void CameraController::OnDeviceBack() {
  device_ = true;
  OpenIfWanted();
}

// specs/CameraLifecycle.tla, Ticket, with GuardTicketChecksCapture: only an
// open camera's frames count, and only those its current attempt captured.
std::optional<std::uint64_t> CameraController::OnFrameCaptured(
    std::uint64_t attempt) {
  if (state_ != State::kOpen || attempt != attempt_) {
    return std::nullopt;
  }
  return consent_epoch_;
}

// specs/CameraLifecycle.tla, Emit, with GuardEmitToSubscribers, or Drop when
// consent was revoked since the frame was captured.
void CameraController::OnReaction(std::uint64_t ticket,
                                  const Reaction& reaction) {
  if (!consent_ || ticket != consent_epoch_) {
    return;
  }
  for (const ClientId& client : subscribers_) {
    clients_.Deliver(client, reaction);
  }
}

// specs/CameraLifecycle.tla, Open, with GuardOpenNeedsConsent. Opening as
// soon as it is wanted is what WF_vars(Open) asks; `blocked_` (#52) holds
// it back the same way `device_` does, until Retry clears it.
void CameraController::OpenIfWanted() {
  if (state_ != State::kClosed || !consent_ || subscribers_.empty() ||
      !device_ || blocked_) {
    return;
  }
  state_ = State::kOpening;
  ++attempt_;
  camera_.Open(attempt_);
}

// The closing half of RevokeConsent and Leave: specs/CameraLifecycle.tla's
// Closing, which ends the attempt.
void CameraController::StartClosing() {
  if (state_ != State::kOpening && state_ != State::kOpen) {
    return;
  }
  state_ = State::kClosing;
  ++attempt_;
  camera_.Close();
}

}  // namespace facepalm
