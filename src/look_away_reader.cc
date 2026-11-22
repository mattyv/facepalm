#include "src/look_away_reader.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "src/calibration.h"
#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
#include "src/face_model.h"
#include "src/head_pose.h"
#include "src/look_away.h"
#include "src/protocol.h"

namespace facepalm {
namespace {

// `face`'s head pose, read as ReactionFrom (src/reaction.cc) reads it, or
// nothing when its transformation matrix has no rotation to read (#44): a
// non-finite pose would never leave the resting pose it was averaged into.
std::optional<HeadRotation> PoseOf(const Face& face) {
  PoseMatrix pose{};
  std::ranges::copy(face.pose_column_major, pose.begin());
  const Rotation rotation = RotationFrom(pose);
  if (!IsFiniteRotation(rotation)) {
    return std::nullopt;
  }
  return HeadRotationFrom(rotation);
}

// {"gesture":"looked_away","kind":"...","duration_ms":n} (#141).
std::string JsonFor(const LookAwayReport& report) {
  return R"({"gesture":"looked_away","kind":)" +
        protocol::Quoted(Name(report.kind)) + R"(,"duration_ms":)" +
        std::to_string(report.duration_ms) + "}";
}

}  // namespace

LookAwayReader::LookAwayReader(ExpressionTracker::Settings tracker,
                               EyeGazeTracker::Settings eye_tracker,
                               LookAwayDetector::Settings detector,
                               EyeGains gains, double head_only_yaw_degrees,
                               const CalibrationProfile* profile)
    : tracker_(tracker),
      eye_tracker_(eye_tracker),
      detector_(detector),
      gains_(gains),
      head_only_yaw_degrees_(head_only_yaw_degrees),
      profile_(profile) {}

void LookAwayReader::OnFace(const Face& face, int /*width*/, int /*height*/,
                            std::int64_t timestamp_ms,
                            std::vector<Reaction>& reactions) {
  const std::optional<HeadRotation> pose = PoseOf(face);
  if (!pose) {
    return;  // As a frame with no face: nothing to learn or judge.
  }
  last_timestamp_ms_ = timestamp_ms;
  // Read before tracker_.Update, below, which may itself just now relearn
  // the resting pose afresh after a gap (#141): were this frame judged
  // against that brand new resting pose instead, the one frame that ends
  // a look spanning the gap (such as a kGone one) would see no resting
  // pose yet (ExpressionTracker::RestingPose's own three-second
  // relearning window) and lose the look it was closing, rather than the
  // next one that opens against the new posture.
  const std::optional<HeadRotation> resting = tracker_.RestingPose();
  Scores scores{};
  std::ranges::copy(face.blendshapes, scores.begin());
  eye_tracker_.Update(face, timestamp_ms);
  tracker_.Update(scores, *pose, timestamp_ms);
  const std::optional<EyeSignal> eye = eye_tracker_.Signal();
  const std::optional<EyeSignal> eye_resting = eye_tracker_.Resting();
  const HeadRotation gaze = ComposeGaze(*pose, eye, eye_resting, gains_,
                                       head_only_yaw_degrees_);
  const CalibrationProfile* profile = profile_.load();
  if (const std::optional<LookAwayReport> report =
          detector_.Update(gaze, resting, profile, timestamp_ms)) {
    reactions.emplace_back(JsonFor(*report));
  }
}

void LookAwayReader::OnNoFace() {
  // #141's kGone: no face to compose gaze from. ExpressionTracker's own
  // resting pose is read, not touched -- it keeps whatever it already
  // learned until a face returns and its own Update runs again, the way
  // it always has (see this file's own header comment).
  detector_.Update(std::nullopt, tracker_.RestingPose(), profile_.load(),
                   last_timestamp_ms_);
}

void LookAwayReader::SetProfile(const CalibrationProfile* profile) {
  profile_.store(profile);
}

}  // namespace facepalm
