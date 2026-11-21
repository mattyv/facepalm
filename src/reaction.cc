#include "src/reaction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

// Rounds to one decimal place, with no negative zero: a turn of -0.04
// degrees reads 0.0, not -0.0.
double Tenths(double degrees) {
  constexpr double kTenthsPerDegree = 10;
  return (std::round(degrees * kTenthsPerDegree) / kTenthsPerDegree) + 0.0;
}

}  // namespace

std::optional<Reaction> ReactionFrom(const Face& face) {
  PoseMatrix pose{};
  std::ranges::copy(face.pose_column_major, pose.begin());
  const Rotation rotation = RotationFrom(pose);
  // From a finite rotation, every angle is finite too.
  for (const auto& row : rotation.rows) {
    if (!std::ranges::all_of(
            row, [](double value) { return std::isfinite(value); })) {
      return std::nullopt;
    }
  }
  const HeadRotation head = HeadRotationFrom(rotation);
  constexpr int kLongest = 64;  // Three angles to one decimal place, and keys.
  std::array<char, kLongest> text{};
  std::snprintf(text.data(), text.size(),
                R"({"yaw":%.1f,"pitch":%.1f,"roll":%.1f})",
                Tenths(head.yaw_degrees), Tenths(head.pitch_degrees),
                Tenths(head.roll_degrees));
  return Reaction(text.data());
}

}  // namespace facepalm
