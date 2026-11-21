#include "src/look_away_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/face_model.h"
#include "src/head_pose.h"
#include "src/look_away.h"

namespace facepalm {
namespace {

// `face`'s head pose, read as ReactionFrom (src/reaction.cc) reads it, or
// nothing when its transformation matrix has no rotation to read (#44): a
// non-finite pose would never leave the resting pose it was averaged into.
std::optional<HeadRotation> PoseOf(const Face& face) {
  PoseMatrix pose{};
  std::ranges::copy(face.pose_column_major, pose.begin());
  const Rotation rotation = RotationFrom(pose);
  for (const auto& row : rotation.rows) {
    if (!std::ranges::all_of(
            row, [](double value) { return std::isfinite(value); })) {
      return std::nullopt;
    }
  }
  return HeadRotationFrom(rotation);
}

}  // namespace

LookAwayReader::LookAwayReader(ExpressionTracker::Settings tracker,
                               LookAwayDetector::Settings detector)
    : tracker_(tracker), detector_(detector) {}

void LookAwayReader::OnFace(const Face& face, int /*width*/, int /*height*/,
                            std::int64_t timestamp_ms,
                            std::vector<Reaction>& reactions) {
  const std::optional<HeadRotation> pose = PoseOf(face);
  if (!pose) {
    return;  // As a frame with no face: nothing to learn or judge.
  }
  Scores scores{};
  std::ranges::copy(face.blendshapes, scores.begin());
  tracker_.Update(scores, *pose, timestamp_ms);
  if (detector_.Update(*pose, tracker_.RestingPose(), timestamp_ms)) {
    reactions.emplace_back(R"({"gesture":"looking_away"})");
  }
}

void LookAwayReader::OnNoFace() {}

}  // namespace facepalm
