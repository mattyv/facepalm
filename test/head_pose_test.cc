#include "src/head_pose.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <ostream>

namespace facepalm {

// Prints a rotation in test names and failure messages. GoogleTest finds it by
// argument-dependent lookup, which ignores an anonymous namespace.
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
static void PrintTo(const HeadRotation& angles, std::ostream* out) {
  *out << "yaw " << angles.yaw_degrees << " pitch " << angles.pitch_degrees
       << " roll " << angles.roll_degrees;
}

namespace {

constexpr double kTolerance = 1e-9;

double Radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

// Builds R = Ry(yaw) * Rx(pitch) * Rz(roll), the convention the function
// under test decomposes.
Rotation Compose(const HeadRotation& angles) {
  const double cy = std::cos(Radians(angles.yaw_degrees));
  const double sy = std::sin(Radians(angles.yaw_degrees));
  const double cp = std::cos(Radians(angles.pitch_degrees));
  const double sp = std::sin(Radians(angles.pitch_degrees));
  const double cr = std::cos(Radians(angles.roll_degrees));
  const double sr = std::sin(Radians(angles.roll_degrees));
  return Rotation{{{
      {(cy * cr) + (sy * sp * sr), (-cy * sr) + (sy * sp * cr), sy * cp},
      {cp * sr, cp * cr, -sp},
      {(-sy * cr) + (cy * sp * sr), (sy * sr) + (cy * sp * cr), cy * cp},
  }}};
}

void ExpectAngles(const HeadRotation& actual, const HeadRotation& expected) {
  EXPECT_NEAR(actual.yaw_degrees, expected.yaw_degrees, kTolerance);
  EXPECT_NEAR(actual.pitch_degrees, expected.pitch_degrees, kTolerance);
  EXPECT_NEAR(actual.roll_degrees, expected.roll_degrees, kTolerance);
}

TEST(HeadRotationFromTest, IdentityIsFacingStraightAhead) {
  const Rotation identity{{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
  ExpectAngles(HeadRotationFrom(identity), HeadRotation{});
}

class RoundTripTest : public testing::TestWithParam<HeadRotation> {};

TEST_P(RoundTripTest, RecoversTheAnglesItWasComposedFrom) {
  ExpectAngles(HeadRotationFrom(Compose(GetParam())), GetParam());
}

INSTANTIATE_TEST_SUITE_P(AcrossTheRangeOfHeadMovement, RoundTripTest,
                         testing::Values(HeadRotation{.yaw_degrees = 30},
                                         HeadRotation{.pitch_degrees = -20},
                                         HeadRotation{.roll_degrees = 15},
                                         HeadRotation{.yaw_degrees = -45,
                                                      .pitch_degrees = 10,
                                                      .roll_degrees = -5},
                                         HeadRotation{.yaw_degrees = 170,
                                                      .pitch_degrees = 80,
                                                      .roll_degrees = -170}));

TEST(HeadRotationFromTest, AtGimbalLockPutsAllTurningInYaw) {
  ExpectAngles(
      HeadRotationFrom(Compose({.yaw_degrees = 30, .pitch_degrees = 90})),
      {.yaw_degrees = 30, .pitch_degrees = 90});
  ExpectAngles(
      HeadRotationFrom(Compose({.yaw_degrees = -60, .pitch_degrees = -90})),
      {.yaw_degrees = -60, .pitch_degrees = -90});
}

TEST(HeadRotationFromTest, RoundingPastStraightUpStillGivesNinetyDegrees) {
  Rotation straight_up = Compose({.pitch_degrees = 90});
  straight_up.rows[1][2] = -1.0000000001;
  const HeadRotation angles = HeadRotationFrom(straight_up);
  EXPECT_FALSE(std::isnan(angles.pitch_degrees));
  EXPECT_NEAR(angles.pitch_degrees, 90, kTolerance);
}

// Packs `rotation` times `scale`, with a translation, column by column.
PoseMatrix Pose(const Rotation& rotation, float scale) {
  PoseMatrix pose{};
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) {
      pose.at((column * 4) + row) =
          scale * static_cast<float>(rotation.rows.at(row).at(column));
    }
  }
  pose[12] = -1.5F;
  pose[13] = 20.0F;
  pose[14] = -60.0F;
  pose[15] = 1.0F;
  return pose;
}

// A float matrix carries about seven significant digits.
constexpr double kFloatTolerance = 1e-4;

void ExpectAnglesNear(const HeadRotation& actual,
                      const HeadRotation& expected) {
  EXPECT_NEAR(actual.yaw_degrees, expected.yaw_degrees, kFloatTolerance);
  EXPECT_NEAR(actual.pitch_degrees, expected.pitch_degrees, kFloatTolerance);
  EXPECT_NEAR(actual.roll_degrees, expected.roll_degrees, kFloatTolerance);
}

TEST(RotationFromTest, ReadsTheMatrixColumnByColumn) {
  const HeadRotation angles{
      .yaw_degrees = 25, .pitch_degrees = -15, .roll_degrees = 8};
  ExpectAnglesNear(HeadRotationFrom(RotationFrom(Pose(Compose(angles), 1))),
                   angles);
}

TEST(RotationFromTest, DividesOutTheScale) {
  const HeadRotation angles{
      .yaw_degrees = -30, .pitch_degrees = 20, .roll_degrees = -10};
  const Rotation rotation = RotationFrom(Pose(Compose(angles), 1.25F));
  ExpectAnglesNear(HeadRotationFrom(rotation), angles);
  EXPECT_NEAR(rotation.rows[0][0], Compose(angles).rows[0][0], kFloatTolerance);
}

// MediaPipe's expected facial transformation matrix for its portrait.jpg test
// image, from mediapipe/tasks/testdata/vision/
// portrait_expected_face_geometry.pbtxt at google-ai-edge/mediapipe commit
// 0b97e00a910a3ae0017db2733f55cb054ee41d8a (Apache-2.0). Its first column's
// length is 1.00037: the scale MediaPipe's Procrustes solver estimated.
constexpr PoseMatrix kMediaPipePortrait{
    0.9995292F,   0.01715696F, -0.03715533F, 0,  //
    -0.01294756F, 0.9937692F,  0.11070588F,  0,  //
    0.038823195F, -0.1101321F, 0.99315894F,  0,  //
    -0.3691378F,  22.75809F,   -65.765925F,  1};

// Expects the rows of `rotation` to be unit length and at right angles.
void ExpectOrthonormal(const Rotation& rotation) {
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double dot = 0;
      for (int k = 0; k < 3; ++k) {
        dot += rotation.rows.at(i).at(k) * rotation.rows.at(j).at(k);
      }
      EXPECT_NEAR(dot, i == j ? 1 : 0, 1e-3) << "rows " << i << ", " << j;
    }
  }
}

TEST(RotationFromTest, MediaPipesPortraitIsAlmostFacingTheCamera) {
  const Rotation rotation = RotationFrom(kMediaPipePortrait);
  ExpectOrthonormal(rotation);
  const HeadRotation angles = HeadRotationFrom(rotation);
  EXPECT_NEAR(angles.yaw_degrees, 2.24, 0.01);
  EXPECT_NEAR(angles.pitch_degrees, 6.32, 0.01);
  EXPECT_NEAR(angles.roll_degrees, 0.99, 0.01);
}

// The single check reaction.cc, expression_reader.cc, look_away_reader.cc,
// gesture_reader.cc and camera_probe.cc each need before trusting a
// rotation (#85): every entry finite, as a rotation taken from a real pose
// always is.
TEST(IsFiniteRotationTest, TrueForAFiniteRotation) {
  const Rotation identity{{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
  EXPECT_TRUE(IsFiniteRotation(identity));
}

// A zero transformation matrix has no rotation to read: its scale, the cube
// root of its determinant, is 0, so RotationFrom's divide gives NaN
// throughout (#44, #85).
TEST(IsFiniteRotationTest, FalseForTheRotationOfAZeroPose) {
  EXPECT_FALSE(IsFiniteRotation(RotationFrom(PoseMatrix{})));
}

TEST(IsFiniteRotationTest, FalseWhenAnEntryIsNan) {
  Rotation rotation{{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
  rotation.rows[1][2] = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(IsFiniteRotation(rotation));
}

TEST(IsFiniteRotationTest, FalseWhenAnEntryIsInfinite) {
  Rotation rotation{{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
  rotation.rows[2][0] = std::numeric_limits<double>::infinity();
  EXPECT_FALSE(IsFiniteRotation(rotation));
}

}  // namespace
}  // namespace facepalm
