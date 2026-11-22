#include "src/calibration_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "src/camera_controller.h"
#include "src/eye_gaze.h"
#include "src/face_model.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

// `face`'s head pose, as LookAwayReader's PoseOf reads it: nothing when its
// transformation matrix has no rotation to read (#44).
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

CalibrationReader::CalibrationReader(CalibrationController& controller,
                                     double blink_threshold)
    : controller_(controller), blink_threshold_(blink_threshold) {}

void CalibrationReader::OnFace(const Face& face, int /*width*/, int /*height*/,
                               std::int64_t /*timestamp_ms*/,
                               std::vector<Reaction>& /*reactions*/) {
  const std::optional<HeadRotation> pose = PoseOf(face);
  if (pose) {
    controller_.Update(*pose, EyeSignalFrom(face, blink_threshold_));
  }
}

void CalibrationReader::OnNoFace() {}

}  // namespace facepalm
