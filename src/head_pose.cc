#include "src/head_pose.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace facepalm {
namespace {

constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;

// Within this distance of 1, the sine of pitch is treated as exactly
// plus or minus 1: yaw and roll can no longer be told apart.
constexpr double kGimbalLockMargin = 1e-9;

}  // namespace

// Follows the standard Tait-Bryan decomposition, as in G. Slabaugh, "Computing
// Euler angles from a rotation matrix", for the order Ry * Rx * Rz. Row 1,
// column 2 of that product is -sin(pitch); column 2 holds yaw over cos(pitch),
// and row 1 holds roll over cos(pitch).
HeadRotation HeadRotationFrom(const Rotation& rotation) {
  const auto& r = rotation.rows;
  const double sin_pitch = std::clamp(-r[1][2], -1.0, 1.0);
  const double pitch = std::asin(sin_pitch);
  if (std::abs(sin_pitch) > 1 - kGimbalLockMargin) {
    return {.yaw_degrees = std::atan2(-r[2][0], r[0][0]) * kDegreesPerRadian,
            .pitch_degrees = pitch * kDegreesPerRadian};
  }
  return {.yaw_degrees = std::atan2(r[0][2], r[2][2]) * kDegreesPerRadian,
          .pitch_degrees = pitch * kDegreesPerRadian,
          .roll_degrees = std::atan2(r[1][0], r[1][1]) * kDegreesPerRadian};
}

// Undoes MediaPipe's CombineTransformMatrix in
// mediapipe/modules/face_geometry/libs/procrustes_solver.cc, which stores
// scale * rotation in the upper-left 3x3, column by column. A rotation's
// determinant is 1, so that block's determinant is the scale cubed.
Rotation RotationFrom(const PoseMatrix& pose) {
  Rotation rotation;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      rotation.rows.at(row).at(column) = pose.at((column * 4) + row);
    }
  }
  const auto& m = rotation.rows;
  const double determinant =
      (m[0][0] * ((m[1][1] * m[2][2]) - (m[1][2] * m[2][1]))) -
      (m[0][1] * ((m[1][0] * m[2][2]) - (m[1][2] * m[2][0]))) +
      (m[0][2] * ((m[1][0] * m[2][1]) - (m[1][1] * m[2][0])));
  const double scale = std::cbrt(determinant);
  for (auto& row : rotation.rows) {
    for (double& value : row) {
      value /= scale;
    }
  }
  return rotation;
}

}  // namespace facepalm
