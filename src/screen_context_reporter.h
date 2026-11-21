#ifndef FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_
#define FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_

#include <optional>

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
  ScreenContextReporter(ScreenSource& source, GazeSettings settings,
                        bool titles_enabled);

  // {"active_app":"...","window_title":"...","mouse":{"x":..,"y":..},
  // "gaze":"..."}, with active_app, window_title and mouse left out when
  // ScreenSource cannot read them (or, for window_title, when titles are
  // not enabled); gaze is always present. `pose` is the current frame's
  // head pose, or nullopt with no face to read it from, which reads as
  // Gaze::kUnknown; `resting` is the pose CoarseGazeFrom judges it against.
  [[nodiscard]] std::string Json(
      const std::optional<HeadRotation>& pose,
      const std::optional<HeadRotation>& resting) const;

 private:
  ScreenSource& source_;
  GazeSettings settings_;
  bool titles_enabled_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_
