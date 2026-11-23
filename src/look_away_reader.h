#ifndef FACEPALM_SRC_LOOK_AWAY_READER_H_
#define FACEPALM_SRC_LOOK_AWAY_READER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "src/calibration.h"
#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/look_away.h"

namespace facepalm {

// Reports looking away (#35, #141) as
// {"gesture":"looked_away","kind":"...","duration_ms":n}: each face's
// composed gaze (src/eye_gaze.h's ComposeGaze: head pose and eye-in-head
// signal together, not head pose alone, #137) goes to LookAwayDetector,
// with the resting pose an ExpressionTracker learns from the same faces.
// The tracker is configured as the expression reader's is, so its
// resting pose is the one ExpressionReader learns, and it keeps working
// when expressions are turned off.
class LookAwayReader : public FaceReader {
 public:
  // `gains` is ComposeGaze's own, used while no calibration profile is
  // set; once one is (SetProfile), OnFace composes gaze with the
  // profile's own gains instead (#170), the way ScreenContextReporter's
  // own ZoneOf already does, since a profile's screens were calibrated
  // against its own estimated gains, not necessarily `gains`.
  LookAwayReader(ExpressionTracker::Settings tracker,
                 EyeGazeTracker::Settings eye_tracker,
                 LookAwayDetector::Settings detector, EyeGains gains = {},
                 double head_only_yaw_degrees =
                     CalibrationSettings::kDefaultHeadOnlyYawDegrees,
                 std::shared_ptr<const CalibrationProfile> profile = nullptr);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  // No face to read gaze from: LookAwayDetector reads this as #141's
  // kGone, using the last frame's own timestamp (OnNoFace is given
  // none) and whatever resting pose ExpressionTracker had already
  // learned, which it keeps until a face returns (it never calls
  // ExpressionTracker::Update itself, so a gap here does not start its
  // own gap-relearning; that happens on the next OnFace instead, the way
  // it always has). Before OnFace has ever read a usable pose, there is
  // no real timestamp to read either (#157), so this does nothing.
  void OnNoFace() override;

  // Swaps the calibration profile the kOtherScreen box test, and the
  // gains OnFace composes gaze with (#170), read from now on (#136, #137,
  // #141): nullptr for none. Takes shared ownership of `profile` (#167):
  // the caller is expected to hand this a freshly built CalibrationProfile
  // each time, never the one an earlier call already gave this reader,
  // so OnFace/OnNoFace keep reading a whole, unchanging object for as
  // long as the snapshot they took under profile_mutex_ holds a
  // reference to it. Safe to call from a different thread than
  // OnFace/OnNoFace run on (the daemon's composition root calls it from
  // the thread a calibration session finishes on, mirroring
  // ScreenContextReporter::SetProfile).
  void SetProfile(std::shared_ptr<const CalibrationProfile> profile);

 private:
  // A snapshot of profile_, copied under profile_mutex_ (#167): the
  // caller's own copy of the shared_ptr keeps whatever CalibrationProfile
  // it names alive for as long as the caller holds it, even once
  // SetProfile swaps a different one in immediately after this returns.
  // A named method, not a lambda at each call site, so its own branch
  // does not count toward OnFace/OnNoFace's own cognitive complexity.
  [[nodiscard]] std::shared_ptr<const CalibrationProfile> Profile() const;

  ExpressionTracker tracker_;
  EyeGazeTracker eye_tracker_;
  LookAwayDetector detector_;
  EyeGains gains_;
  double head_only_yaw_degrees_;
  mutable std::mutex profile_mutex_;  // #167; guards profile_ alone.
  std::shared_ptr<const CalibrationProfile> profile_;
  // OnNoFace has no timestamp of its own; the last one OnFace read, so
  // the look stays measured in real time across a stretch with no face.
  std::int64_t last_timestamp_ms_ = 0;
  // Whether OnFace has ever read a usable pose (#157): until it has,
  // last_timestamp_ms_ is only its own default, not a real reading, so
  // OnNoFace has no meaningful timestamp yet to open or continue a look
  // from and does nothing, rather than opening one at a placeholder time
  // that would read as an enormous duration once a face is finally seen.
  bool had_face_ = false;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LOOK_AWAY_READER_H_
