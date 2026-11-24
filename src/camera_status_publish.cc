#include "src/camera_status_publish.h"

#include "src/camera_controller.h"

namespace facepalm {

CameraStatusAction DecideCameraStatusOnOpened(
    bool was_opening, CameraController::State new_state) {
  if (was_opening && new_state == CameraController::State::kOpen) {
    return CameraStatusAction::kPublishOn;
  }
  return CameraStatusAction::kNone;
}

CameraStatusAction DecideCameraStatusOnOpenFailed(bool was_opening) {
  return was_opening ? CameraStatusAction::kPublishOff
                     : CameraStatusAction::kNone;
}

}  // namespace facepalm
