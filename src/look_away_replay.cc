#include "src/look_away_replay.h"

#include <cstdint>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

#include "src/config.h"
#include "src/expressions.h"
#include "src/head_pose.h"
#include "src/look_away.h"

namespace facepalm {

bool ReplayLookingAway(std::istream& input, std::ostream& output) {
  // As LookAwayReader runs them in the daemon and the probe. The scores do
  // not move the resting pose, so the replay gives the tracker none.
  ExpressionTracker tracker(ExpressionTracker::Settings{
      .rise = Config::kDefaultExpressionsSmallestRise,
      .pose_degrees = Config::kDefaultExpressionsPoseDegrees,
      .pose_range = Config::kDefaultExpressionsPoseRange,
      .settle_ms = Config::kDefaultExpressionsSettleMs,
      .spreads = Config::kDefaultExpressionsSpreads,
      .hold_ms = Config::kDefaultExpressionsHoldMs});
  LookAwayDetector detector({.degrees = Config::kDefaultLookingAwayDegrees,
                             .hold_ms = Config::kDefaultLookingAwayHoldMs});
  std::int64_t last = -1;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string kind;
    row >> kind;
    if (kind != "head_pose") {
      continue;
    }
    std::int64_t timestamp = 0;
    HeadRotation pose;
    std::string extra;
    if (!(row >> timestamp >> pose.yaw_degrees >> pose.pitch_degrees >>
          pose.roll_degrees) ||
        row >> extra || timestamp <= last) {
      return false;
    }
    last = timestamp;
    tracker.Update(Scores{}, pose, timestamp);
    if (detector.Update(pose, tracker.RestingPose(), timestamp)) {
      output << "looking_away " << timestamp << '\n';
    }
  }
  return !input.bad() && output.good();
}

}  // namespace facepalm
