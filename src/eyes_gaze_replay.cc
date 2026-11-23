#include "src/eyes_gaze_replay.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

#include "src/calibration.h"
#include "src/config.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
#include "src/eyes_replay.h"
#include "src/face_model.h"
#include "src/head_pose.h"
#include "src/look_away.h"

namespace facepalm {
namespace {

// src/eyes_probe.h's own MediaPipe blendshape indices, in FormatEyesLine's
// order: eyeLookDown, In, Out and Up (each left eye then right), eyeBlink,
// eyeSquint and eyeWide (each left then right).
constexpr std::array<int, 14> kBlendshapeIndices = {11, 12, 13, 14, 15, 16, 17,
                                                    18, 9,  10, 19, 20, 21, 22};

// A synthetic Face holding only the 14 blendshape scores #139's eyes line
// carries, everything else zeroed: the landmarks and pose are never read
// from it here, since the pose this file composes gaze with comes
// straight from the paired head_pose line.
Face FaceFromEyes(const EyesFrame& eyes) {
  Face face{};
  const std::array<double, kBlendshapeIndices.size()> values = {
      eyes.eye_look_down[0], eyes.eye_look_down[1], eyes.eye_look_in[0],
      eyes.eye_look_in[1],   eyes.eye_look_out[0],  eyes.eye_look_out[1],
      eyes.eye_look_up[0],   eyes.eye_look_up[1],   eyes.eye_blink[0],
      eyes.eye_blink[1],     eyes.eye_squint[0],    eyes.eye_squint[1],
      eyes.eye_wide[0],      eyes.eye_wide[1]};
  for (std::size_t i = 0; i < kBlendshapeIndices.size(); ++i) {
    face.blendshapes[kBlendshapeIndices.at(i)] =
        static_cast<float>(values.at(i));
  }
  return face;
}

}  // namespace

bool ReplayEyesAndLookAway(std::istream& input, std::ostream& output,
                           const Config& config) {
  ExpressionTracker tracker(ExpressionTracker::Settings{
      .rise = config.expressions_smallest_rise,
      .pose_degrees = config.expressions_pose_degrees,
      .pose_range = config.expressions_pose_range,
      .settle_ms = config.expressions_settle_ms,
      .spreads = config.expressions_spreads,
      .hold_ms = config.expressions_hold_ms,
      .eye_look_down_threshold = config.expressions_eye_look_down_threshold,
      .eye_blink_threshold = config.expressions_eye_blink_threshold});
  EyeGazeTracker eye_tracker(EyeGazeTracker::Settings{
      .blink_threshold = config.eye_gaze_blink_threshold,
      .median_ms = config.eye_gaze_median_ms,
      .resting_ms = config.eye_gaze_resting_ms,
      .gap_ms = config.eye_gaze_gap_ms});
  LookAwayDetector detector(LookAwayDetector::Settings{
      .degrees = config.looking_away_degrees,
      .hold_ms = config.looking_away_hold_ms,
      .averted_ms = config.looking_away_averted_ms,
      .other_screen_margin_degrees =
          config.looking_away_other_screen_margin_degrees});
  const EyeGains gains{.horizontal = config.eye_gaze_horizontal_gain,
                       .vertical = config.eye_gaze_vertical_gain};
  std::int64_t last = -1;
  std::optional<std::pair<std::int64_t, HeadRotation>> pending_pose;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string kind;
    row >> kind;
    if (kind == "gesture_no_face") {
      pending_pose.reset();
      output << "no_face " << last << '\n';
      // Before any paired frame has set `last`, it is still only its own
      // placeholder, never a real timestamp (#157, matching
      // LookAwayReader's own had_face_ guard, src/look_away_reader.cc):
      // there is nothing yet to open or continue a look from.
      if (last != -1) {
        detector.Update(std::nullopt, tracker.RestingPose(), nullptr, last);
      }
      continue;
    }
    if (kind == "head_pose") {
      std::int64_t timestamp = 0;
      HeadRotation pose;
      std::string extra;
      if (!(row >> timestamp >> pose.yaw_degrees >> pose.pitch_degrees >>
            pose.roll_degrees) ||
          row >> extra || timestamp <= last) {
        return false;
      }
      pending_pose = {timestamp, pose};
      continue;
    }
    if (kind != "eyes") {
      continue;
    }
    const std::optional<EyesFrame> eyes = ParseEyesLine(line);
    if (!eyes || !pending_pose || eyes->timestamp_ms != pending_pose->first) {
      return false;
    }
    const auto [timestamp, pose] = *pending_pose;
    pending_pose.reset();
    // Read before tracker.Update, below, for the same reason
    // LookAwayReader::OnFace does (src/look_away_reader.cc): a gap long
    // enough to relearn the resting pose afresh would otherwise blank
    // out the one frame that closes a look spanning the gap.
    const std::optional<HeadRotation> resting = tracker.RestingPose();
    const Face face = FaceFromEyes(*eyes);
    eye_tracker.Update(face, timestamp);
    Scores scores{};
    std::ranges::copy(face.blendshapes, scores.begin());
    for (const Expression expression :
         tracker.Update(scores, pose, timestamp)) {
      output << "expression " << timestamp << ' ' << Name(expression) << '\n';
    }
    const std::optional<EyeSignal> eye = eye_tracker.Signal();
    const std::optional<EyeSignal> eye_resting = eye_tracker.Resting();
    const HeadRotation gaze = ComposeGaze(
        pose, eye, eye_resting, gains, config.eye_gaze_head_only_yaw_degrees);
    output << "gaze " << timestamp << ' ' << gaze.yaw_degrees << ' '
           << gaze.pitch_degrees << '\n';
    last = timestamp;
    if (const std::optional<LookAwayReport> report =
            detector.Update(gaze, resting, nullptr, timestamp)) {
      output << "looked_away " << timestamp << ' ' << Name(report->kind) << ' '
             << report->duration_ms << '\n';
    }
  }
  return !input.bad() && output.good();
}

}  // namespace facepalm
