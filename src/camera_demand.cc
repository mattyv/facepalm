#include "src/camera_demand.h"

#include <utility>

namespace facepalm {

CameraDemand::CameraDemand(std::function<void(bool)> changed)
    : changed_(std::move(changed)) {}

void CameraDemand::SetClientDemand(bool wants) {
  client_demand_ = wants;
  Update();
}

void CameraDemand::SetPreviewDemand(bool wants) {
  preview_demand_ = wants;
  Update();
}

// specs/PreviewLease.tla's CameraOpens/CameraCloses: fires only on the
// combined signal's own edge, so a second source joining or leaving while
// the first already holds it changes nothing.
void CameraDemand::Update() {
  const bool wanted = client_demand_ || preview_demand_;
  if (wanted == wanted_) {
    return;
  }
  wanted_ = wanted;
  changed_(wanted_);
}

}  // namespace facepalm
