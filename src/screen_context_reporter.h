#ifndef FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_
#define FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_

#include <optional>

#include "src/calibration.h"
#include "src/head_pose.h"
#include "src/screen_context.h"
#include "src/screen_source.h"

namespace facepalm {

// Builds the `context` JSON a reading carries (#61 item 11): what
// ScreenSource reports at the moment of the call, and the gaze
// CoarseGazeFrom reads from the pose it is given. Nothing here keeps state
// between calls or starts anything of its own, so it has no lifecycle to
// spec; each call is a fresh snapshot.
class ScreenContextReporter {
 public:
  // `titles_enabled` is Config's screen_context.window_titles_enabled.
  // `profile`, when given, is an optional calibration profile (#61 item 12)
  // this reporter reads no lifetime claim on, so the caller keeps it
  // alive; a nullptr, or one with no screens, never adds
  // "calibrated_screen" to Json's result.
  ScreenContextReporter(ScreenSource& source, GazeSettings settings,
                        bool titles_enabled,
                        const CalibrationProfile* profile = nullptr);

  // {"active_app":"...","window_title":"...","mouse":{"x":..,"y":..},
  // "gaze":"...","calibrated_screen":n}, with active_app, window_title and
  // mouse left out when ScreenSource cannot read them (or, for
  // window_title, when titles are not enabled); gaze is always present.
  // `pose` is the current frame's head pose, or nullopt with no face to
  // read it from, which reads as Gaze::kUnknown; `resting` is the pose
  // CoarseGazeFrom judges it against. calibrated_screen, the 0-based index
  // of the screen a calibration profile's saved points place `pose`
  // nearest to (NearestCalibratedScreen), is left out without `pose`, or
  // without a profile with at least one screen; it never changes what
  // `gaze` itself reads.
  [[nodiscard]] std::string Json(
      const std::optional<HeadRotation>& pose,
      const std::optional<HeadRotation>& resting) const;

 private:
  ScreenSource& source_;
  GazeSettings settings_;
  bool titles_enabled_;
  const CalibrationProfile* profile_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_
