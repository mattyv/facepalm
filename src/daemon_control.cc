#include "src/daemon_control.h"

#include <functional>
#include <utility>

#include "src/camera_controller.h"

namespace facepalm {
DaemonControl::DaemonControl(DaemonController& controller,
                             DaemonPipeline& pipeline,
                             std::function<void()> stop_service,
                             std::function<void(bool failed)> exit)
    : controller_(controller),
      pipeline_(pipeline),
      stop_service_(std::move(stop_service)),
      exit_(std::move(exit)) {}

// specs/DaemonAdmission.tla, Subscription: ignored once stopping.
void DaemonControl::Subscription(bool joined) {
  if (stopping_) {
    return;
  }
  if (joined) {
    controller_.Subscribe();
  } else {
    controller_.Leave();
  }
}

// specs/DaemonAdmission.tla, Stop: revokes consent, which closes the camera
// (GuardStopRevokes), stops the service, and with the camera already closed
// retires the model at once (GuardStopRetires). A second Stop, such as one
// from OnOpenFailed, changes nothing.
void DaemonControl::Stop() {
  if (stopping_) {
    return;
  }
  stopping_ = true;
  controller_.Revoke();
  stop_service_();
  if (controller_.State() == CameraController::State::kClosed && !retiring_) {
    Retire(false);
  }
}

void DaemonControl::Fail() {
  failed_ = true;
  Stop();
}

// specs/DaemonAdmission.tla, OnOpened: only the camera that was opening
// creates a model (GuardOpenOnce).
void DaemonControl::OnOpened() {
  const bool opening = controller_.State() == CameraController::State::kOpening;
  controller_.Opened();
  if (opening && controller_.State() == CameraController::State::kOpen) {
    pipeline_.Open();
  }
}

// specs/DaemonAdmission.tla, OnOpenFailed: a camera that was opening and
// failed stops the daemon as a failure (GuardOpenFailStops). One that
// already began closing reports Closed next.
void DaemonControl::OnOpenFailed() {
  const bool opening = controller_.State() == CameraController::State::kOpening;
  controller_.OpenFailed();
  if (opening) {
    Fail();
  }
}

// specs/DaemonAdmission.tla, OnClosed: the controller counts the camera
// closed only once the model is retired (GuardAckAfterRetire).
void DaemonControl::OnClosed() { Retire(true); }

// specs/DaemonAdmission.tla, OnDeviceLost.
void DaemonControl::OnDeviceLost() {
  controller_.DeviceLost();
  if (controller_.State() == CameraController::State::kClosed) {
    Retire(false);
  }
}

// specs/DaemonAdmission.tla, OnDeviceBack: waits for a retirement under way
// (GuardDeferDeviceBack), and is ignored once stopping.
void DaemonControl::OnDeviceBack() {
  if (retiring_) {
    device_back_ = true;
  } else if (!stopping_) {
    controller_.DeviceBack();
  }
}

// specs/DaemonAdmission.tla, Retire, one at a time (GuardRetireOnce), then
// Retired once the pipeline has destroyed the model.
void DaemonControl::Retire(bool acknowledge_close) {
  if (retiring_) {
    return;
  }
  retiring_ = true;
  pipeline_.Close([this, acknowledge_close] {
    retiring_ = false;
    if (acknowledge_close) {
      controller_.Closed();
    }
    if (stopping_) {
      exit_(failed_);
      return;
    }
    if (device_back_) {
      device_back_ = false;
      controller_.DeviceBack();
    }
  });
}
}  // namespace facepalm
