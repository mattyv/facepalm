#include "src/expression_reader.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/face_model.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

// OnFace copies the shim's blendshape array into a Scores; were the shim's
// count ever to grow past kBlendshapeScores, the copy would write past the
// end, so hold the two to each other here.
static_assert(std::extent_v<decltype(Face::blendshapes)> == kBlendshapeScores);

// The reaction for `expression`, named by Name(): {"expression":"smile"}.
Reaction ReactionFor(Expression expression) {
  return std::string(R"({"expression":")") + std::string(Name(expression)) +
         "\"}";
}

// `face`'s head pose, read the same way ReactionFrom (src/reaction.cc) reads
// it: the facial transformation matrix MediaPipe hands back, undone into a
// rotation and then into yaw, pitch and roll. Nothing when the matrix has no
// rotation to read (#44, #79): a non-finite pose would never leave the
// resting pose it was averaged into, and the pose gate would never shut.
std::optional<HeadRotation> PoseOf(const Face& face) {
  PoseMatrix pose{};
  std::ranges::copy(face.pose_column_major, pose.begin());
  const Rotation rotation = RotationFrom(pose);
  if (!IsFiniteRotation(rotation)) {
    return std::nullopt;
  }
  return HeadRotationFrom(rotation);
}

}  // namespace

ExpressionReader::ExpressionReader(ExpressionTracker::Settings settings)
    : tracker_(settings) {}

void ExpressionReader::OnFace(const Face& face, int /*width*/, int /*height*/,
                              std::int64_t timestamp_ms,
                              std::vector<Reaction>& reactions) {
  const std::optional<HeadRotation> pose = PoseOf(face);
  if (!pose) {
    return;  // As a frame with no face: nothing to learn or judge.
  }
  Scores scores{};
  std::ranges::copy(face.blendshapes, scores.begin());
  for (const Expression started :
       tracker_.Update(scores, *pose, timestamp_ms)) {
    reactions.push_back(ReactionFor(started));
  }
}

void ExpressionReader::OnNoFace() {}

}  // namespace facepalm
