#include "src/look_away_replay.h"

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>

#include "src/calibration.h"
#include "src/config.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
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
      .hold_ms = Config::kDefaultExpressionsHoldMs,
      .eye_look_down_threshold =
          Config::kDefaultExpressionsEyeLookDownThreshold,
      .eye_blink_threshold = Config::kDefaultExpressionsEyeBlinkThreshold});
  LookAwayDetector detector(
      {.degrees = Config::kDefaultLookingAwayDegrees,
       .hold_ms = Config::kDefaultLookingAwayHoldMs,
       .averted_ms = Config::kDefaultLookingAwayAvertedMs,
       .other_screen_margin_degrees =
           Config::kDefaultLookingAwayOtherScreenMarginDegrees});
  std::int64_t last = -1;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string kind;
    row >> kind;
    if (kind == "gesture_no_face") {
      // #141's kGone: no timestamp of its own, as LookAwayReader::OnNoFace
      // has none (src/look_away_reader.cc); `last` is the closest reading
      // available, the way it reads there too.
      detector.Update(std::nullopt, tracker.RestingPose(), nullptr, last);
      continue;
    }
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
    // Read before tracker.Update, below, for the same reason
    // src/look_away_reader.cc's own OnFace does: a gap long enough for
    // ExpressionTracker to relearn its resting pose afresh would
    // otherwise blank out the one frame that closes the look the gap
    // itself opened.
    const std::optional<HeadRotation> resting = tracker.RestingPose();
    last = timestamp;
    tracker.Update(Scores{}, pose, timestamp);
    const HeadRotation gaze =
        ComposeGaze(pose, std::nullopt, std::nullopt, EyeGains{},
                   CalibrationSettings::kDefaultHeadOnlyYawDegrees);
    if (const std::optional<LookAwayReport> report =
            detector.Update(gaze, resting, nullptr, timestamp)) {
      output << "looked_away " << timestamp << ' ' << Name(report->kind)
             << ' ' << report->duration_ms << '\n';
    }
  }
  return !input.bad() && output.good();
}

}  // namespace facepalm
