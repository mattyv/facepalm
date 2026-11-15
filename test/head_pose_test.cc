#include "src/head_pose.h"

#include <gtest/gtest.h>

#include <cmath>
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

}  // namespace
}  // namespace facepalm
