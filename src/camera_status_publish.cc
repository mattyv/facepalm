#include "src/camera_status_publish.h"

#include "src/camera_controller.h"

namespace facepalm {

ServiceReason CameraFailureReason(OpenFailure reason,
                                  std::optional<ConfigureError> detail) {
  if (reason == OpenFailure::kPermissionDenied) {
    return ServiceReason::kPermissionDenied;
  }
  if (reason == OpenFailure::kStartFailed) {
    return ServiceReason::kStartFailed;
  }
  if (!detail) {
    return ServiceReason::kNoCandidate;
  }
  switch (*detail) {
    case ConfigureError::kNoCandidate:
      return ServiceReason::kNoCandidate;
    case ConfigureError::kPermissionDenied:
      return ServiceReason::kPermissionDenied;
    case ConfigureError::kQueryFailed:
      return ServiceReason::kQueryFailed;
    case ConfigureError::kUnsupported:
      return ServiceReason::kUnsupported;
  }
  return ServiceReason::kNoCandidate;
}

void RecordModelFailure(ServicePhase& phase, ServiceReason& reason,
                        std::string& detail, std::string_view error) {
  if (phase == ServicePhase::kFailed) {
    return;
  }
  phase = ServicePhase::kFailed;
  reason = ServiceReason::kModelFailure;
  detail = error;
}

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
