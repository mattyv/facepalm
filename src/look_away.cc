#include "src/look_away.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "src/calibration.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

constexpr double kFullTurnDegrees = 360;
// The gaze is read as back home, ending a look (LookAway.tla's Return),
// within this much of Settings::degrees: a gaze hovering right at the
// threshold does not end and restart a look on every frame.
constexpr double kBackFraction = 0.5;

// `gaze` against `resting`, the way LookAwayDetector's own Settings::degrees
// judges it: yaw wrapped at the +/-180 seam, the larger of it and the pitch
// difference.
double OffFrom(const HeadRotation& gaze, const HeadRotation& resting) {
  return std::max(
      std::abs(std::remainder(gaze.yaw_degrees - resting.yaw_degrees,
                              kFullTurnDegrees)),
      std::abs(gaze.pitch_degrees - resting.pitch_degrees));
}

}  // namespace

std::string_view Name(LookAwayKind kind) {
  // In LookAwayKind's own order.
  constexpr std::array<std::string_view, 4> kNames = {"averted", "other_screen",
                                                      "away", "gone"};
  return kNames.at(static_cast<std::size_t>(kind));
}

LookAwayDetector::LookAwayDetector(Settings settings) : settings_(settings) {}

std::optional<LookAwayReport> LookAwayDetector::Update(
    const std::optional<HeadRotation>& gaze,
    const std::optional<HeadRotation>& resting,
    const CalibrationProfile* profile, std::int64_t timestamp_ms) {
  if (!gaze) {
    // #141's kGone: no face to read gaze from, read as past the full
    // threshold either way -- whether or not the resting pose is known
    // yet (LookAway.tla's Move, from any band, even while ExpressionTracker
    // is still relearning it after a gap): a face lost counts on its own,
    // never waiting on a resting pose it does not need.
    if (!away_) {
      away_ = true;
      away_ms_ = timestamp_ms;
    }
    saw_gone_ = true;
    return std::nullopt;
  }
  if (!resting) {
    // #157: no resting pose yet to judge this frame's gaze against --
    // ExpressionTracker is still (re)learning one, whether this is the
    // very first track or a gap long enough to relearn after (its own
    // kGapMs). A look already open (away_, saw_gone_, saw_other_screen_)
    // is left exactly as it is, not cleared the way this branch used to
    // (LookAway.tla's Gap and Learn, which no longer clear them either):
    // it is reported, once relearning finishes and the gaze turns out to
    // be home, against the newly learned resting pose, rather than
    // silently dropped the moment relearning started. A look that was
    // never open (away_ already false) has nothing to preserve.
    return std::nullopt;
  }
  const double off = OffFrom(*gaze, *resting);
  if (off <= settings_.degrees * kBackFraction) {
    return Return(timestamp_ms);
  }
  if (off <= settings_.degrees) {
    return std::nullopt;  // Between the two: neither home nor truly away.
  }
  if (!away_) {
    away_ = true;
    away_ms_ = timestamp_ms;
  }
  if (profile != nullptr &&
      CalibratedScreenBoxContaining(*profile, *gaze,
                                    settings_.other_screen_margin_degrees)) {
    saw_other_screen_ = true;
  }
  return std::nullopt;
}

std::optional<LookAwayReport> LookAwayDetector::Return(
    std::int64_t timestamp_ms) {
  if (!away_) {
    return std::nullopt;  // Never passed the threshold: nothing to end.
  }
  const std::int64_t duration = timestamp_ms - away_ms_;
  const bool held = duration >= settings_.hold_ms;
  const bool gone = saw_gone_;
  const bool other_screen = saw_other_screen_;
  away_ = false;
  saw_gone_ = false;
  saw_other_screen_ = false;
  if (!held) {
    return std::nullopt;  // A glance: never reported.
  }
  LookAwayKind kind;
  if (gone) {
    kind = LookAwayKind::kGone;
  } else if (other_screen) {
    kind = LookAwayKind::kOtherScreen;
  } else if (duration >= settings_.averted_ms) {
    kind = LookAwayKind::kAway;
  } else {
    kind = LookAwayKind::kAverted;
  }
  return LookAwayReport{.kind = kind, .duration_ms = duration};
}

}  // namespace facepalm
