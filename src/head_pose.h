#ifndef FACEPALM_SRC_HEAD_POSE_H_
#define FACEPALM_SRC_HEAD_POSE_H_

#include <array>

namespace facepalm {

// A 3x3 rotation matrix, stored row by row.
struct Rotation {
  std::array<std::array<double, 3>, 3> rows;
};

// MediaPipe's facial transformation matrix: 4x4, stored column by column, as
// uniform scale times rotation, plus translation in centimetres. It maps
// MediaPipe's canonical face, whose +X is towards the subject's left, +Y up
// and +Z out of the face, into the camera's space.
inline constexpr int kPoseMatrixSize = 4 * 4;
using PoseMatrix = std::array<float, kPoseMatrixSize>;

// How far the head is turned, in degrees. Yaw turns about the vertical axis,
// pitch about the side-to-side axis, and roll about the axis through the face.
// For a rotation taken from a PoseMatrix, positive yaw is turning to the
// subject's left, positive pitch is looking down, and positive roll is tilting
// towards the subject's right shoulder.
struct HeadRotation {
  double yaw_degrees = 0;
  double pitch_degrees = 0;
  double roll_degrees = 0;
};

// Returns the yaw, pitch and roll that compose `rotation` as
// Ry(yaw) * Rx(pitch) * Rz(roll). At gimbal lock (pitch of plus or minus 90
// degrees) only yaw minus roll is determined, so roll is reported as 0.
HeadRotation HeadRotationFrom(const Rotation& rotation);

// Returns the rotation in `pose`, with its uniform scale divided out and its
// translation dropped.
Rotation RotationFrom(const PoseMatrix& pose);

// Whether every entry of `rotation` is finite. A degenerate pose (all
// zeros, NaN or infinity) fails this: dividing out a zero scale, or a
// NaN or infinite transformation matrix, leaves entries that are not
// finite, and from a finite rotation every yaw, pitch and roll
// HeadRotationFrom reads is finite too (#44, #85). The one check
// reaction.cc, expression_reader.cc, look_away_reader.cc, gesture_reader.cc
// and camera_probe.cc each need before trusting a rotation.
bool IsFiniteRotation(const Rotation& rotation);

}  // namespace facepalm

#endif  // FACEPALM_SRC_HEAD_POSE_H_
