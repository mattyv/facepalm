#ifndef FACEPALM_SRC_LOOK_AWAY_H_
#define FACEPALM_SRC_LOOK_AWAY_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "src/calibration.h"
#include "src/head_pose.h"

namespace facepalm {

// How a look away ended (#141): kAverted (gaze came back within
// Settings::averted_ms), kOtherScreen (gaze was ever inside another
// calibrated screen's own box while away), kAway (longer than
// averted_ms, and never in another screen's box) or kGone (there was no
// face to read at some point during the look). A look never held for
// Settings::hold_ms is a glance and is never reported at all, under any
// kind.
enum class LookAwayKind : std::uint8_t {
  kAverted,
  kOtherScreen,
  kAway,
  kGone,
};

// "averted", "other_screen", "away" or "gone".
std::string_view Name(LookAwayKind kind);

// One look away's end (#141): the kind it read as, and how long it
// lasted, from the gaze first passing Settings::degrees to its return.
struct LookAwayReport {
  LookAwayKind kind = LookAwayKind::kAway;
  std::int64_t duration_ms = 0;
};

// Finds where the person looks away (#35, #141), in a stream of one
// face's composed gaze (src/eye_gaze.h's ComposeGaze: head pose composed
// with eye-in-head signal, not head pose alone, #137) against the resting
// pose ExpressionTracker learns: the gaze is away once its yaw or pitch is
// more than Settings::degrees from the resting pose, and a look away is
// reported once the gaze returns within half of that, with how it read
// and how long it lasted. A nod or a shake leaves the resting pose for
// less than Settings::hold_ms, so neither is reported (a glance). Roll is
// a tilt, not a look away, and is not read. A look already open survives
// a gap long enough that ExpressionTracker relearns the resting pose
// afresh (#157): nothing is judged while there is no resting pose to
// judge it against, but the look itself is never cleared by that alone,
// so it is still reported once relearning finishes and the gaze is read
// against the new resting pose. specs/LookAway.tla models the rule.
class LookAwayDetector {
 public:
  // Their defaults live in Config.
  struct Settings {
    // How far yaw or pitch must be from the resting pose to be away.
    double degrees;
    // How long the gaze must stay away for the look to be reported at
    // all, rather than read as a glance.
    std::int64_t hold_ms;
    // Back within this long of first passing `degrees` is kAverted
    // rather than kAway (#141, provisional: about 5 to 8 seconds, to tune
    // on #139's recordings).
    std::int64_t averted_ms;
    // How far outside a calibrated screen's own box (the span of its own
    // four corners, src/calibration.h's CalibratedScreenBoxContaining)
    // still counts as that screen, for the kOtherScreen kind (#141,
    // #137's own design: "a screen is the box they span, away is outside
    // every box plus a margin").
    double other_screen_margin_degrees;
  };

  explicit LookAwayDetector(Settings settings);

  // Takes the next frame's composed gaze (nullopt for no face: #141's
  // kGone), the resting pose (nullopt while it is still being learned,
  // whether or not a face is present), an optional calibration profile
  // (read only for the kOtherScreen box test; nullptr reads as no
  // screens calibrated, so kOtherScreen never applies) and the frame's
  // timestamp, which must increase from call to call while a face keeps
  // being read. Returns a report once a look away ends; nullopt on every
  // other frame, including for a glance and while the resting pose is
  // still being learned.
  std::optional<LookAwayReport> Update(
      const std::optional<HeadRotation>& gaze,
      const std::optional<HeadRotation>& resting,
      const CalibrationProfile* profile, std::int64_t timestamp_ms);

 private:
  // Return, once the gaze (or the lack of a face) has come back within
  // half of Settings::degrees of the resting pose: the look, if one ever
  // passed the full threshold for long enough to count, is reported now.
  std::optional<LookAwayReport> Return(std::int64_t timestamp_ms);

  Settings settings_;
  // Whether the gaze has passed Settings::degrees (or there has been no
  // face) at some point since the last Return: there is a look open to
  // report, if it has lasted long enough.
  bool away_ = false;
  // When it first did (this look's own start, for its duration).
  std::int64_t away_ms_ = 0;
  // This look, gaze was ever inside another calibrated screen's box.
  bool saw_other_screen_ = false;
  // This look, there was ever no face to read gaze from.
  bool saw_gone_ = false;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LOOK_AWAY_H_
