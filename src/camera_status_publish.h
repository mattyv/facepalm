#ifndef FACEPALM_SRC_CAMERA_STATUS_PUBLISH_H_
#define FACEPALM_SRC_CAMERA_STATUS_PUBLISH_H_

#include <cstdint>

#include "src/camera_controller.h"

namespace facepalm {

// What tools/daemon/main.cc's Engine::Opened() or Engine::OpenFailed()
// decided about the camera-in-use file (src/daemon_status.h's
// PublishCameraStatus).
enum class CameraStatusAction : std::uint8_t { kNone, kPublishOn, kPublishOff };

// CameraStatus!OpenSucceeds (#185): Engine::Opened() publishes "on" only
// for a report CameraController actually accepted as really opening and
// really reaching State::kOpen -- the same two-part check
// DaemonControl::OnOpened already makes of it, so a stray or late report
// never publishes on for a camera that was never really made to open by
// it. `was_opening` is whether the controller was mid-opening right
// before this report arrived; `new_state` is what it is now, after
// CameraController::OnOpened has already applied the report.
CameraStatusAction DecideCameraStatusOnOpened(
    bool was_opening, CameraController::State new_state);

// CameraStatus!OpenFails (#185): Engine::OpenFailed() republishes "off"
// only for a report that really was opening -- the demand edge that asked
// for the camera already published on before the open was even attempted,
// and a denied permission or a failed start must correct that publish
// itself, since CameraLifecycle.tla's own retry keeps the camera wanted
// throughout, with no demand edge of its own to otherwise republish the
// fix. Published only once, the same guard DecideCameraStatusOnOpened
// takes, so a stray or late report never republishes off for a camera a
// genuinely later attempt already opened.
CameraStatusAction DecideCameraStatusOnOpenFailed(bool was_opening);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CAMERA_STATUS_PUBLISH_H_
