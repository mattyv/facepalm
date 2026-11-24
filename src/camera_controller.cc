#include "src/camera_controller.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace facepalm {
namespace {
constexpr std::chrono::seconds kMaxRetryDelay{30};
}  // namespace

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
  CancelRetry();
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
    CancelRetry();
    StartClosing();
  }
}

// specs/CameraLifecycle.tla, Opened. A report that arrives after closing
// began is ignored: Opened is not enabled while closing.
void CameraController::OnOpened() {
  if (state_ == State::kOpening) {
    state_ = State::kOpen;
    retry_delay_ = std::chrono::seconds{1};
  }
}

// All open failures schedule bounded retries while demand and consent hold.
// A retry is scoped to this generation and cannot revive canceled work.
void CameraController::OnOpenFailed(OpenFailure reason,
                                    std::optional<ConfigureError> detail) {
  if (state_ != State::kOpening) {
    return;
  }
  state_ = State::kClosed;
  (void)reason;
  (void)detail;
  ScheduleRetry();
}

void CameraController::ScheduleRetry() {
  blocked_ = true;
  const auto generation = ++retry_generation_;
  const auto delay = retry_delay_;
  retry_delay_ = std::min(retry_delay_ * 2, kMaxRetryDelay);
  schedule_retry_(delay, [this, generation] { Retry(generation); });
}

void CameraController::Retry(std::uint64_t generation) {
  if (generation != retry_generation_) {
    return;
  }
  ++retry_generation_;
  blocked_ = false;
  device_ = true;
  OpenIfWanted();
}

void CameraController::CancelRetry() {
  ++retry_generation_;
  blocked_ = false;
  retry_delay_ = std::chrono::seconds{1};
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
    // Capture has already stopped, but the model's physical lease remains
    // held until DaemonControl acknowledges retirement via OnClosed.
    state_ = State::kClosing;
    ++attempt_;
  }
}

// specs/CameraLifecycle.tla, DeviceBack.
void CameraController::OnDeviceBack() {
  CancelRetry();
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
      blocked_) {
    return;
  }
  if (!device_) {
    ScheduleRetry();
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
