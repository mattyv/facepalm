#ifndef FACEPALM_SRC_HEAD_POSE_H_
#define FACEPALM_SRC_HEAD_POSE_H_

#include <array>

namespace facepalm {

// A 3x3 rotation matrix, stored row by row.
struct Rotation {
  std::array<std::array<double, 3>, 3> rows;
};

// How far the head is turned, in degrees. Yaw turns about the vertical axis,
// pitch about the side-to-side axis, and roll about the axis through the face.
struct HeadRotation {
  double yaw_degrees = 0;
  double pitch_degrees = 0;
  double roll_degrees = 0;
};

// Returns the yaw, pitch and roll that compose `rotation` as
// Ry(yaw) * Rx(pitch) * Rz(roll). At gimbal lock (pitch of plus or minus 90
// degrees) only yaw minus roll is determined, so roll is reported as 0.
HeadRotation HeadRotationFrom(const Rotation& rotation);

}  // namespace facepalm

#endif  // FACEPALM_SRC_HEAD_POSE_H_
