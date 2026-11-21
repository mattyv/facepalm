#ifndef FACEPALM_SRC_SCREEN_CONTEXT_READER_H_
#define FACEPALM_SRC_SCREEN_CONTEXT_READER_H_

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/head_pose.h"

namespace facepalm {

// Keeps the current head pose and the resting pose ScreenContextReporter
// reads gaze from (#61 item 11), the way LookAwayReader keeps them for its
// own detector: its own ExpressionTracker, configured as the expression
// reader's is, so gaze keeps working when expressions are turned off.
// Reports no reaction of its own; the reporter asks it for Pose() and
// RestingPose() instead.
//
// A snapshot of thread-safe storage, not a lifecycle: OnFace/OnNoFace run
// on the daemon's inference thread, and Pose()/RestingPose() are read from
// its control thread when a reaction is delivered
// (tools/daemon/main.cc), so mutex_ is the only thing that makes the two
// safe together; it is not itself a spec-worthy state machine.
class ScreenContextReader : public FaceReader {
 public:
  explicit ScreenContextReader(ExpressionTracker::Settings tracker);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  // As LookAwayReader's: the tracker learns afresh on its own after a gap,
  // so this only clears the current pose, which a missing face has none of.
  void OnNoFace() override;

  // The pose read on the last OnFace, or nullopt once OnNoFace has run.
  [[nodiscard]] std::optional<HeadRotation> Pose() const;
  // The resting pose ExpressionTracker has learned, or nullopt while it is
  // still learning.
  [[nodiscard]] std::optional<HeadRotation> RestingPose() const;

 private:
  mutable std::mutex mutex_;
  ExpressionTracker tracker_;
  std::optional<HeadRotation> pose_;
  std::optional<HeadRotation> resting_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_CONTEXT_READER_H_
