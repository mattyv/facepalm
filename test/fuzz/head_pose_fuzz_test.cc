#include <cmath>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

// Comfortably wider than any real rotation entry, while finite.
constexpr double kWide = 1e6;

// Property test (#120, #44, #85): a finite rotation gives finite angles.
// `rotation`'s nine entries, row by row, are each fuzzed separately: the
// struct holds no invariant (such as being an actual rotation) that a
// pure "every entry is finite" domain needs to respect.
void FiniteRotationGivesFiniteAngles(double r00, double r01, double r02,
                                     double r10, double r11, double r12,
                                     double r20, double r21, double r22) {
  const Rotation rotation{{{
      {r00, r01, r02},
      {r10, r11, r12},
      {r20, r21, r22},
  }}};
  const HeadRotation angles = HeadRotationFrom(rotation);
  EXPECT_TRUE(std::isfinite(angles.yaw_degrees));
  EXPECT_TRUE(std::isfinite(angles.pitch_degrees));
  EXPECT_TRUE(std::isfinite(angles.roll_degrees));
}
FUZZ_TEST(HeadPoseFuzz, FiniteRotationGivesFiniteAngles)
    .WithDomains(
        fuzztest::InRange(-kWide, kWide), fuzztest::InRange(-kWide, kWide),
        fuzztest::InRange(-kWide, kWide), fuzztest::InRange(-kWide, kWide),
        fuzztest::InRange(-kWide, kWide), fuzztest::InRange(-kWide, kWide),
        fuzztest::InRange(-kWide, kWide), fuzztest::InRange(-kWide, kWide),
        fuzztest::InRange(-kWide, kWide));

}  // namespace
}  // namespace facepalm
