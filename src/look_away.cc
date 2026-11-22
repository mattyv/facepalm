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
  if (!resting) {
    // LookAway.tla's Learn and Gap: no resting pose to be away from, so
    // nothing is open and nothing is held.
    away_ = false;
    saw_other_screen_ = false;
    saw_gone_ = false;
    return std::nullopt;
  }
  if (!gaze) {
    // #141's kGone: no face to read gaze from, read as past the full
    // threshold either way.
    if (!away_) {
      away_ = true;
      away_ms_ = timestamp_ms;
    }
    saw_gone_ = true;
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
