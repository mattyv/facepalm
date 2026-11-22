#ifndef FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_
#define FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_

#include <cstdint>
#include <deque>
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
  // protocol.cc's own longest beat window: nothing asks Between about a
  // window any longer than this, so it is Sample's own buffer's default
  // bound (#142).
  static constexpr std::int64_t kDefaultMaxWindowMs = 10000;

  // `titles_enabled` is Config's screen_context.window_titles_enabled.
  // `profile`, when given, is an optional calibration profile (#61 item 12)
  // this reporter reads no lifetime claim on, so the caller keeps it
  // alive; a nullptr, or one with no screens, never adds
  // "calibrated_screen" to Json's result, and its own gains (`profile`'s,
  // once given) take over from `default_gains` for every ComposeGaze
  // call Json makes. `head_only_yaw_degrees` is ComposeGaze's own.
  // `max_window_ms` bounds Sample's own buffer (#142).
  //
  // `head_only_yaw_degrees` and `max_window_ms` are never swappable in
  // practice: every call site passes both from distinct, named sources
  // (main.cc's own config fields, or a test's own named constant), never
  // a bare literal in either position.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  ScreenContextReporter(ScreenSource& source, GazeSettings settings,
                        bool titles_enabled,
                        const CalibrationProfile* profile = nullptr,
                        EyeGains default_gains = {},
                        double head_only_yaw_degrees =
                            CalibrationSettings::kDefaultHeadOnlyYawDegrees,
                        std::int64_t max_window_ms = kDefaultMaxWindowMs);

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

  // Samples this frame's composed gaze into the window Between can later
  // ask about (#142): the same four readings Json takes, without
  // building its JSON. Called once a frame regardless of whether a
  // reading is made (ScreenContextReader's own OnSample, in the daemon),
  // so a beat's window holds every frame since, not only the ones a
  // reaction happened to land on. `at_ms` is the wall clock at the
  // moment of the call (src/feed_clients.h's own UnixMilliseconds, the
  // same clock ReactionFeed marks a beat's own start and end on), not
  // the frame's own capture-clock timestamp, so Between's bounds and
  // this buffer's timestamps agree.
  //
  // See Json's own declaration for why `pose`/`resting` and
  // `eye`/`eye_resting` are never swappable in practice.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  void Sample(const std::optional<HeadRotation>& pose,
              const std::optional<HeadRotation>& resting,
              const std::optional<EyeSignal>& eye,
              const std::optional<EyeSignal>& eye_resting, std::int64_t at_ms);
  // NOLINTEND(bugprone-easily-swappable-parameters)

  // The majority gaze (#142), and, given `content`, whether gaze was
  // mostly inside it most of the time (#144), over every Sample call in
  // [start_ms, end_ms). Gaze::kUnknown, and ContentHit::kUnknown when
  // `content` is given, with no samples in range -- no screen context,
  // or a range predating every sample Sample's own `max_window_ms` still
  // keeps. A tie between zones, or between yes and no, keeps whichever
  // Sample saw first in the window.
  [[nodiscard]] GazeWindow Between(
      std::int64_t start_ms, std::int64_t end_ms,
      const std::optional<ContentRect>& content) const;

 private:
  // One Sample call's own reading (#142, #144): the zone CoarseGazeFrom
  // read, and the composed angle and resting pose Between's own
  // ContentHitFrom needs, had a content rectangle been asked for at this
  // exact moment.
  struct GazeSample {
    std::int64_t at_ms;
    Gaze zone;
    std::optional<HeadRotation> angle;
    std::optional<HeadRotation> resting;
  };

  // The composed gaze angle (nullopt without `pose`) and its coarse zone:
  // the pair Json and Sample each derive from the same four readings,
  // folded into one place so a change to ComposeGaze/CoarseGazeFrom's
  // own call has one caller to keep in step. Called while already
  // holding mutex_: reads profile_.
  struct Zone {
    Gaze zone;
    std::optional<HeadRotation> angle;
  };
  [[nodiscard]] Zone ZoneOf(const std::optional<HeadRotation>& pose,
                            const std::optional<HeadRotation>& resting,
                            const std::optional<EyeSignal>& eye,
                            const std::optional<EyeSignal>& eye_resting,
                            ScreenSide side) const;

  ScreenSource& source_;
  GazeSettings settings_;
  bool titles_enabled_;
  EyeGains default_gains_;
  double head_only_yaw_degrees_;
  const std::int64_t max_window_ms_;
  mutable std::mutex mutex_;
  const CalibrationProfile* profile_;
  std::deque<GazeSample> samples_;  // Oldest first; under mutex_.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_CONTEXT_REPORTER_H_
