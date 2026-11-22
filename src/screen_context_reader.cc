#include "src/screen_context_reader.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
#include "src/face_model.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

// As src/look_away_reader.cc's own PoseOf: `face`'s head pose, read as
// ReactionFrom (src/reaction.cc) reads it, or nothing for a degenerate
// transformation matrix (#44, #85).
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

ScreenContextReader::ScreenContextReader(ExpressionTracker::Settings tracker,
                                         EyeGazeTracker::Settings eye_tracker,
                                         OnSample on_sample)
    : on_sample_(std::move(on_sample)),
      tracker_(tracker),
      eye_tracker_(eye_tracker) {}

void ScreenContextReader::OnFace(const Face& face, int /*width*/,
                                 int /*height*/, std::int64_t timestamp_ms,
                                 std::vector<Reaction>& /*reactions*/) {
  const std::optional<HeadRotation> pose = PoseOf(face);
  if (!pose) {
    OnNoFace();  // As a frame with no face: nothing to learn or cache.
    return;
  }
  Scores scores{};
  std::ranges::copy(face.blendshapes, scores.begin());
  std::unique_lock lock(mutex_);
  tracker_.Update(scores, *pose, timestamp_ms);
  eye_tracker_.Update(face, timestamp_ms);
  pose_ = pose;
  resting_ = tracker_.RestingPose();
  eye_ = eye_tracker_.Signal();
  eye_resting_ = eye_tracker_.Resting();
  const std::optional<HeadRotation> sampled_pose = pose_;
  const std::optional<HeadRotation> sampled_resting = resting_;
  const std::optional<EyeSignal> sampled_eye = eye_;
  const std::optional<EyeSignal> sampled_eye_resting = eye_resting_;
  lock.unlock();
  if (on_sample_) {
    on_sample_(sampled_pose, sampled_resting, sampled_eye, sampled_eye_resting);
  }
}

void ScreenContextReader::OnNoFace() {
  std::unique_lock lock(mutex_);
  pose_ = std::nullopt;
  eye_ = std::nullopt;
  const std::optional<HeadRotation> sampled_resting = resting_;
  const std::optional<EyeSignal> sampled_eye_resting = eye_resting_;
  lock.unlock();
  if (on_sample_) {
    on_sample_(std::nullopt, sampled_resting, std::nullopt,
               sampled_eye_resting);
  }
}

std::optional<HeadRotation> ScreenContextReader::Pose() const {
  const std::scoped_lock lock(mutex_);
  return pose_;
}

std::optional<HeadRotation> ScreenContextReader::RestingPose() const {
  const std::scoped_lock lock(mutex_);
  return resting_;
}

std::optional<EyeSignal> ScreenContextReader::EyeReading() const {
  const std::scoped_lock lock(mutex_);
  return eye_;
}

std::optional<EyeSignal> ScreenContextReader::EyeResting() const {
  const std::scoped_lock lock(mutex_);
  return eye_resting_;
}

}  // namespace facepalm
