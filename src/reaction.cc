#include "src/reaction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

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

Reaction ReactionFrom(const Face& face) {
  PoseMatrix pose{};
  std::ranges::copy(face.pose_column_major, pose.begin());
  const HeadRotation head = HeadRotationFrom(RotationFrom(pose));
  constexpr int kLongest = 64;  // Three angles to one decimal place, and keys.
  std::array<char, kLongest> text{};
  std::snprintf(text.data(), text.size(),
                R"({"yaw":%.1f,"pitch":%.1f,"roll":%.1f})",
                Tenths(head.yaw_degrees), Tenths(head.pitch_degrees),
                Tenths(head.roll_degrees));
  return text.data();
}

}  // namespace facepalm
