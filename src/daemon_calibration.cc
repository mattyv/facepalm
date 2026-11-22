#include "src/daemon_calibration.h"

namespace facepalm {

DaemonCalibration::DaemonCalibration(CalibrationController* controller)
    : controller_(controller) {}

CalibrationController* DaemonCalibration::Controller() const {
  return controller_;
}

void DaemonCalibration::OnCameraLost() {
  if (controller_ != nullptr) {
    controller_->OnCameraLost();
  }
}

void DaemonCalibration::OnCameraUp() {
  if (controller_ != nullptr) {
    controller_->OnCameraUp();
  }
}

}  // namespace facepalm
