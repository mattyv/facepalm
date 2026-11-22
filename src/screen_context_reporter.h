#ifndef FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_
#define FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_

#include <mutex>
#include <optional>

#include "src/calibration.h"
#include "src/eye_gaze.h"
#include "src/head_pose.h"
#include "src/screen_context.h"
#include "src/screen_source.h"

namespace facepalm {

// Builds the `context` JSON a reading carries (#61 item 11): what
// ScreenSource reports at the moment of the call, and the gaze
// CoarseGazeFrom reads from the head pose and eye signal (src/eye_gaze.h's
// ComposeGaze, #137) it is given. Json itself keeps no state between
// calls and starts nothing of its own, so it has no lifecycle to spec;
// each call is a fresh snapshot. SetProfile is the one piece of state
// this holds, guarded by its own mutex: the daemon's composition root
// calls it once a calibration session finishes (#136, #137), so Json's
// very next call already reads the new profile, rather than only after a
// restart.
class ScreenContextReporter {
 public:
  // `titles_enabled` is Config's screen_context.window_titles_enabled.
  // `profile`, when given, is an optional calibration profile (#61 item 12)
  // this reporter reads no lifetime claim on, so the caller keeps it
  // alive; a nullptr, or one with no screens, never adds
  // "calibrated_screen" to Json's result, and its own gains (`profile`'s,
  // once given) take over from `default_gains` for every ComposeGaze
  // call Json makes. `head_only_yaw_degrees` is ComposeGaze's own.
  ScreenContextReporter(ScreenSource& source, GazeSettings settings,
                        bool titles_enabled,
                        const CalibrationProfile* profile = nullptr,
                        EyeGains default_gains = {},
                        double head_only_yaw_degrees =
                            CalibrationSettings::kDefaultHeadOnlyYawDegrees);

  // {"active_app":"...","window_title":"...","mouse":{"x":..,"y":..},
  // "gaze":"...","calibrated_screen":n}, with active_app, window_title and
  // mouse left out when ScreenSource cannot read them (or, for
  // window_title, when titles are not enabled); gaze is always present.
  // `pose` is the current frame's head pose, or nullopt with no face to
  // read it from, which reads as Gaze::kUnknown; `eye` and `eye_resting`
  // are the eye signal ComposeGaze composes with it and its own resting
  // level (#137), left out -- gaze then reads head pose alone -- by a
  // caller with no eye signal of its own, such as this reporter's own
  // tests; `resting` is the resting head pose CoarseGazeFrom judges the
  // composed gaze against. calibrated_screen, the 0-based index of the
  // screen a calibration profile's saved points place the composed gaze
  // nearest to (NearestCalibratedScreen), is left out without `pose`, or
  // without a profile with at least one screen; it never changes what
  // `gaze` itself reads.
  //
  // `pose`/`resting` and `eye`/`eye_resting` are never swappable in
  // practice: every call site passes all four from distinct, named
  // sources (ScreenContextReader's own Pose/RestingPose/EyeReading/
  // EyeResting, in the daemon, or named test locals), never a bare
  // literal in any position.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  [[nodiscard]] std::string Json(
      const std::optional<HeadRotation>& pose,
      const std::optional<HeadRotation>& resting,
      const std::optional<EyeSignal>& eye = std::nullopt,
      const std::optional<EyeSignal>& eye_resting = std::nullopt) const;
  // NOLINTEND(bugprone-easily-swappable-parameters)

  // Swaps the calibration profile Json reads from now on (#136, #137):
  // nullptr for none. `profile`, when not null, must outlive every Json
  // call made after this returns, the same lifetime constraint the
  // constructor's own `profile` carries.
  void SetProfile(const CalibrationProfile* profile);

 private:
  ScreenSource& source_;
  GazeSettings settings_;
  bool titles_enabled_;
  EyeGains default_gains_;
  double head_only_yaw_degrees_;
  mutable std::mutex mutex_;
  const CalibrationProfile* profile_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_
