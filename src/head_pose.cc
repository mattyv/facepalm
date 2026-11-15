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

}  // namespace facepalm
