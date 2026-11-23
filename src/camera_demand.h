#ifndef FACEPALM_SRC_CAMERA_DEMAND_H_
#define FACEPALM_SRC_CAMERA_DEMAND_H_

#include <functional>

namespace facepalm {

// Combines every reason the camera may be wanted into the one admission
// signal DaemonControl::Subscription already needs (#176), following
// specs/PreviewLease.tla's `Demand`: an approved client subscribing
// (ApprovedFeed's own first-join/last-leave, unchanged) and the camera
// preview's own live, fresh request (src/preview_request.h's
// IsFreshPreviewRequest, polled independently of frame capture, since no
// frame can arrive before the camera is admitted). `changed` is told the
// combined value only when it changes -- the same first-source/last-source
// edge DaemonControl::Subscription's own `joined` boolean already expects,
// so the camera opens for the preview alone exactly the way it already
// does for an approved client, and closes only once every source agrees
// nothing wants it ("Turn on the camera" is never the right answer once
// the preview itself can ask).
class CameraDemand {
 public:
  explicit CameraDemand(std::function<void(bool)> changed);

  // specs/PreviewLease.tla's `client_demand`: ApprovedFeed's own
  // first-join/last-leave signal.
  void SetClientDemand(bool wants);
  // specs/PreviewLease.tla's preview term of `Demand`: the preview's
  // request file's own presence and freshness, polled on the daemon's own
  // timer (src/preview_request.h's kPreviewRequestStaleMs).
  void SetPreviewDemand(bool wants);

  [[nodiscard]] bool Wanted() const { return wanted_; }

 private:
  void Update();

  std::function<void(bool)> changed_;
  bool client_demand_ = false;
  bool preview_demand_ = false;
  bool wanted_ = false;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CAMERA_DEMAND_H_
