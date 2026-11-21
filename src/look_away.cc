#include "src/look_away.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "src/head_pose.h"

namespace facepalm {
namespace {

constexpr double kFullTurnDegrees = 360;
// The head is back, and the next look can be reported, within this much of
// the threshold, so a head hovering at it is one look (LookAway.tla's
// GuardHysteresis); ExpressionTracker ends an expression the same way.
constexpr double kBackFraction = 0.5;

}  // namespace

LookAwayDetector::LookAwayDetector(Settings settings) : settings_(settings) {}

bool LookAwayDetector::Update(const HeadRotation& pose,
                              const std::optional<HeadRotation>& resting,
                              std::int64_t timestamp_ms) {
  if (!resting) {
    // LookAway.tla's Learn and Gap: no resting pose to be away from, so no
    // hold runs.
    away_ = false;
    return false;
  }
  const double off =
      std::max(std::abs(std::remainder(pose.yaw_degrees - resting->yaw_degrees,
                                       kFullTurnDegrees)),
               std::abs(pose.pitch_degrees - resting->pitch_degrees));
  if (off <= settings_.degrees * kBackFraction) {
    shown_ = false;  // Rearm.
  }
  if (off <= settings_.degrees) {
    away_ = false;  // Move, out of "away".
    return false;
  }
  if (!away_) {
    away_ = true;  // Move, into "away".
    away_ms_ = timestamp_ms;
  }
  if (shown_ || timestamp_ms - away_ms_ < settings_.hold_ms) {
    return false;  // Hold, not yet done, or already reported.
  }
  shown_ = true;  // Report.
  return true;
}

}  // namespace facepalm
