#include "src/look_away.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include "src/head_pose.h"

namespace facepalm {
namespace {

// 15 degrees and 1,000 ms, Config's defaults, are repeated here as literals:
// the rule under test is that these are honoured.
constexpr LookAwayDetector::Settings kSettings = {.degrees = 15,
                                                  .hold_ms = 1000};
constexpr HeadRotation kRest = {};

HeadRotation Yawed(double degrees) { return {.yaw_degrees = degrees}; }
HeadRotation Pitched(double degrees) { return {.pitch_degrees = degrees}; }

// The model's LookAway.tla names each test by the property it stands for.
TEST(LookReportedTest, AHeadHeldAwayForTheHoldTimeIsReportedOnce) {
  LookAwayDetector detector(kSettings);
  EXPECT_FALSE(detector.Update(Yawed(20), kRest, 0));
  EXPECT_FALSE(detector.Update(Yawed(25), kRest, 999));
  EXPECT_TRUE(detector.Update(Yawed(25), kRest, 1000));
}

TEST(LookReportedTest, LookingDownCountsAsAway) {
  LookAwayDetector detector(kSettings);
  EXPECT_FALSE(detector.Update(Pitched(20), kRest, 0));
  EXPECT_TRUE(detector.Update(Pitched(20), kRest, 1000));
}

TEST(LookReportedTest, AwayIsMeasuredFromTheRestingPose) {
  LookAwayDetector detector(kSettings);
  const HeadRotation resting = {.yaw_degrees = 30, .pitch_degrees = -10};
  // 20 degrees of yaw is 10 from this resting pose: not away.
  EXPECT_FALSE(detector.Update(Yawed(20), resting, 0));
  EXPECT_FALSE(detector.Update(Yawed(20), resting, 2000));
  // 10 degrees of pitch is 20 from it.
  EXPECT_FALSE(
      detector.Update({.yaw_degrees = 30, .pitch_degrees = 10}, resting, 3000));
  EXPECT_TRUE(
      detector.Update({.yaw_degrees = 30, .pitch_degrees = 10}, resting, 4000));
}

TEST(LookReportedTest, YawIsComparedAcrossTheWrapAt180Degrees) {
  LookAwayDetector detector(kSettings);
  const HeadRotation resting = {.yaw_degrees = 175};
  // -175 is 10 degrees from 175, not 350.
  EXPECT_FALSE(detector.Update(Yawed(-175), resting, 0));
  EXPECT_FALSE(detector.Update(Yawed(-175), resting, 2000));
}

// A roll is a tilt of the head, not a look away.
TEST(LookReportedTest, RollAloneIsNotAway) {
  LookAwayDetector detector(kSettings);
  EXPECT_FALSE(detector.Update({.roll_degrees = 40}, kRest, 0));
  EXPECT_FALSE(detector.Update({.roll_degrees = 40}, kRest, 2000));
}

TEST(NoReportBeforeHoldTest, ANodThatReturnsBeforeTheHoldTimeIsNot) {
  LookAwayDetector detector(kSettings);
  // A big nod: 20 degrees down for 500 ms, then back, again and again.
  for (std::int64_t start = 0; start < 5000; start += 1000) {
    EXPECT_FALSE(detector.Update(Pitched(20), kRest, start));
    EXPECT_FALSE(detector.Update(Pitched(20), kRest, start + 500));
    EXPECT_FALSE(detector.Update(Pitched(0), kRest, start + 600));
  }
}

TEST(NoReportBeforeHoldTest, EvenAFrameAtTheThresholdRestartsTheHold) {
  LookAwayDetector detector(kSettings);
  EXPECT_FALSE(detector.Update(Yawed(20), kRest, 0));
  EXPECT_FALSE(detector.Update(Yawed(15), kRest, 500));  // Not past it.
  EXPECT_FALSE(detector.Update(Yawed(20), kRest, 1000));
  EXPECT_FALSE(detector.Update(Yawed(20), kRest, 1999));
  EXPECT_TRUE(detector.Update(Yawed(20), kRest, 2000));
}

TEST(NoReportWhileLearningTest, NothingIsReportedWithoutARestingPose) {
  LookAwayDetector detector(kSettings);
  EXPECT_FALSE(detector.Update(Yawed(30), std::nullopt, 0));
  EXPECT_FALSE(detector.Update(Yawed(30), std::nullopt, 2000));
  // Nor does a hold that began while learning carry over.
  EXPECT_FALSE(detector.Update(Yawed(30), kRest, 2500));
  EXPECT_FALSE(detector.Update(Yawed(30), kRest, 3499));
  EXPECT_TRUE(detector.Update(Yawed(30), kRest, 3500));
}

TEST(OncePerLookTest, StayingAwayIsNotReportedAgain) {
  LookAwayDetector detector(kSettings);
  detector.Update(Yawed(30), kRest, 0);
  ASSERT_TRUE(detector.Update(Yawed(30), kRest, 1000));
  EXPECT_FALSE(detector.Update(Yawed(30), kRest, 2000));
  EXPECT_FALSE(detector.Update(Yawed(30), kRest, 10000));
}

TEST(RearmOnlyAtRestTest, HoveringAtTheThresholdIsOneLook) {
  LookAwayDetector detector(kSettings);
  detector.Update(Yawed(30), kRest, 0);
  ASSERT_TRUE(detector.Update(Yawed(30), kRest, 1000));
  // Back to 10 degrees, inside the threshold but not within half of it.
  EXPECT_FALSE(detector.Update(Yawed(10), kRest, 1500));
  EXPECT_FALSE(detector.Update(Yawed(30), kRest, 2000));
  EXPECT_FALSE(detector.Update(Yawed(30), kRest, 3000));
}

TEST(LookRearmsTest, BackWithinHalfTheThresholdTheNextLookIsReported) {
  LookAwayDetector detector(kSettings);
  detector.Update(Yawed(30), kRest, 0);
  ASSERT_TRUE(detector.Update(Yawed(30), kRest, 1000));
  EXPECT_FALSE(detector.Update(Yawed(7.5), kRest, 1500));  // Half: rearmed.
  EXPECT_FALSE(detector.Update(Pitched(-20), kRest, 2000));
  EXPECT_TRUE(detector.Update(Pitched(-20), kRest, 3000));
}

// The resting pose follows a pose held long enough (ExpressionTracker), so a
// head that settles away is at rest again and the suppression ends.
TEST(LookRearmsTest, ARestingPoseThatFollowsTheHeadRearmsIt) {
  LookAwayDetector detector(kSettings);
  detector.Update(Yawed(30), kRest, 0);
  ASSERT_TRUE(detector.Update(Yawed(30), kRest, 1000));
  EXPECT_FALSE(detector.Update(Yawed(30), Yawed(25), 30000));
  EXPECT_FALSE(detector.Update(Yawed(-10), Yawed(25), 31000));
  EXPECT_TRUE(detector.Update(Yawed(-10), Yawed(25), 32000));
}

}  // namespace
}  // namespace facepalm
