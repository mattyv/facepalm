#ifndef FACEPALM_SRC_SCREEN_CONTEXT_READER_H_
#define FACEPALM_SRC_SCREEN_CONTEXT_READER_H_

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
#include "src/eye_signs.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/head_pose.h"

namespace facepalm {

// Keeps the current head pose, eye signal and their resting levels
// ScreenContextReporter composes into gaze from (#61 item 11, #137): its
// own ExpressionTracker, configured as the expression reader's is, so
// gaze keeps working when expressions are turned off, and its own
// EyeGazeTracker for the eye side of #137's composition. Reports no
// reaction of its own; the reporter asks it for Pose(), RestingPose(),
// EyeReading() and EyeResting() instead.
//
// A snapshot of thread-safe storage, not a lifecycle: OnFace/OnNoFace run
// on the daemon's inference thread, and the accessors are read from its
// control thread when a reaction is delivered (tools/daemon/main.cc), so
// mutex_ is the only thing that makes the two safe together; it is not
// itself a spec-worthy state machine.
class ScreenContextReader : public FaceReader {
 public:
  // Told the same four readings OnFace/OnNoFace just updated, plus one
  // frame's own eye sign (src/eye_signs.h's SampleFrom, #145/#165) --
  // nullopt on OnNoFace, since there is no face to read eyeBlink or
  // eyeWide from -- once a frame regardless of whether it carries a
  // reaction (#142): the daemon's own hook into
  // ScreenContextReporter::Sample, so a beat's window holds every
  // frame's gaze and eye signs, not only the ones a reaction happened to
  // land on. nullptr (the default) tells no one.
  using OnSample =
      std::function<void(const std::optional<HeadRotation>& pose,
                         const std::optional<HeadRotation>& resting,
                         const std::optional<EyeSignal>& eye,
                         const std::optional<EyeSignal>& eye_resting,
                         const std::optional<EyeSignSample>& eye_sign)>;

  ScreenContextReader(ExpressionTracker::Settings tracker,
                      EyeGazeTracker::Settings eye_tracker,
                      OnSample on_sample = nullptr);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  // As LookAwayReader's: the trackers learn afresh on their own after a
  // gap, so this only clears the current pose and eye reading, which a
  // missing face has neither of.
  void OnNoFace() override;

  // The pose read on the last OnFace, or nullopt once OnNoFace has run.
  [[nodiscard]] std::optional<HeadRotation> Pose() const;
  // The resting pose ExpressionTracker has learned, or nullopt while it is
  // still learning.
  [[nodiscard]] std::optional<HeadRotation> RestingPose() const;
  // The eye signal EyeGazeTracker read on the last OnFace (its own
  // median, #137), or nullopt once OnNoFace has run or it is still
  // learning or filtering out blinks.
  [[nodiscard]] std::optional<EyeSignal> EyeReading() const;
  // The resting eye signal EyeGazeTracker has learned, or nullopt while
  // it is still learning.
  [[nodiscard]] std::optional<EyeSignal> EyeResting() const;

 private:
  mutable std::mutex mutex_;
  OnSample on_sample_;
  ExpressionTracker tracker_;
  EyeGazeTracker eye_tracker_;
  // eye_tracker's own blink_threshold, kept apart to build each frame's
  // EyeSignSample (#145/#165): SampleFrom reads a frame's own blink flag
  // at this same threshold, not eye_tracker_'s median-filtered one.
  double blink_threshold_;
  std::optional<HeadRotation> pose_;
  std::optional<HeadRotation> resting_;
  std::optional<EyeSignal> eye_;
  std::optional<EyeSignal> eye_resting_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_CONTEXT_READER_H_
