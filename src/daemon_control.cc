#include "src/daemon_control.h"

#include <functional>
#include <utility>

#include "src/camera_controller.h"

namespace facepalm {
DaemonControl::DaemonControl(DaemonController& controller,
                             DaemonPipeline& pipeline,
                             std::function<void()> stop_service,
                             std::function<void()> stopped)
    : controller_(controller),
      pipeline_(pipeline),
      stop_service_(std::move(stop_service)),
      stopped_(std::move(stopped)) {}

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

void DaemonControl::OnOpened() {
  const bool opening = controller_.State() == CameraController::State::kOpening;
  controller_.Opened();
  if (opening && controller_.State() == CameraController::State::kOpen) {
    pipeline_.Open();
  }
}

void DaemonControl::OnOpenFailed() {
  controller_.OpenFailed();
  if (stopping_ && !retiring_ &&
      controller_.State() == CameraController::State::kClosed) {
    Retire(false);
  }
}
void DaemonControl::OnClosed() { Retire(true); }
void DaemonControl::OnDeviceLost() {
  controller_.DeviceLost();
  if (controller_.State() == CameraController::State::kClosed) {
    Retire(false);
  }
}
void DaemonControl::OnDeviceBack() {
  if (retiring_) {
    device_back_ = true;
  } else if (!stopping_) {
    controller_.DeviceBack();
  }
}

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
      stopped_();
      return;
    }
    if (device_back_) {
      device_back_ = false;
      controller_.DeviceBack();
    }
  });
}
}  // namespace facepalm
