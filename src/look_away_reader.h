#ifndef FACEPALM_SRC_LOOK_AWAY_READER_H_
#define FACEPALM_SRC_LOOK_AWAY_READER_H_

#include <atomic>
#include <cstdint>
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
  LookAwayReader(ExpressionTracker::Settings tracker,
                EyeGazeTracker::Settings eye_tracker,
                LookAwayDetector::Settings detector, EyeGains gains = {},
                double head_only_yaw_degrees =
                    CalibrationSettings::kDefaultHeadOnlyYawDegrees,
                const CalibrationProfile* profile = nullptr);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  // No face to read gaze from: LookAwayDetector reads this as #141's
  // kGone, using the last frame's own timestamp (OnNoFace is given
  // none) and whatever resting pose ExpressionTracker had already
  // learned, which it keeps until a face returns (it never calls
  // ExpressionTracker::Update itself, so a gap here does not start its
  // own gap-relearning; that happens on the next OnFace instead, the way
  // it always has).
  void OnNoFace() override;

  // Swaps the calibration profile the kOtherScreen box test reads from
  // now on (#136, #137, #141): nullptr for none. `profile`, when not
  // null, must outlive every OnFace call made after this returns. Safe
  // to call from a different thread than OnFace/OnNoFace run on (the
  // daemon's composition root calls it from the thread a calibration
  // session finishes on, mirroring ScreenContextReporter::SetProfile).
  void SetProfile(const CalibrationProfile* profile);

 private:
  ExpressionTracker tracker_;
  EyeGazeTracker eye_tracker_;
  LookAwayDetector detector_;
  EyeGains gains_;
  double head_only_yaw_degrees_;
  std::atomic<const CalibrationProfile*> profile_;
  // OnNoFace has no timestamp of its own; the last one OnFace read, so
  // the look stays measured in real time across a stretch with no face.
  std::int64_t last_timestamp_ms_ = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LOOK_AWAY_READER_H_
