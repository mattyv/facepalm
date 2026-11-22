#ifndef FACEPALM_SRC_DAEMON_CALIBRATION_H_
#define FACEPALM_SRC_DAEMON_CALIBRATION_H_

#include "src/calibration.h"

namespace facepalm {

// What the daemon's composition root gives Service and MakeFaceReaders,
// and forwards the real camera's lifecycle to (#116): one place that
// decides calibration is unavailable, rather than a nullptr check
// repeated at each call site in tools/daemon/main.cc (#64).
class DaemonCalibration {
 public:
  // `controller` is nullptr when calibration is unavailable for this run
  // (no profile store path was resolved); when given, it must outlive
  // this.
  explicit DaemonCalibration(CalibrationController* controller);

  // The controller to give Service's constructor and MakeFaceReaders, or
  // nullptr when calibration is unavailable.
  [[nodiscard]] CalibrationController* Controller() const;
  // specs/Calibration.tla's CameraLost: cancels a session in progress,
  // since there is nothing left to capture, and refuses Start until
  // OnCameraUp. A no-op when calibration is unavailable.
  void OnCameraLost();
  // specs/Calibration.tla's CameraBack. A no-op when calibration is
  // unavailable.
  void OnCameraUp();

 private:
  CalibrationController* controller_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_DAEMON_CALIBRATION_H_
