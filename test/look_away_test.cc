#include "src/look_away.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstddef>
#include <optional>

#include "src/calibration.h"
#include "src/head_pose.h"

// GoogleTest assertions check each optional before access.
// NOLINTBEGIN(bugprone-unchecked-optional-access)
namespace facepalm {
namespace {

using ::testing::Eq;

// 15 degrees, 1,000 ms hold, 6,000 ms averted and 3 degrees of margin are
// repeated here as literals: the rule under test is that these are
// honoured, not Config's own defaults (which #139's recordings have not
// yet tuned).
constexpr LookAwayDetector::Settings kSettings = {
    .degrees = 15, .hold_ms = 1000, .averted_ms = 6000,
    .other_screen_margin_degrees = 3};
constexpr HeadRotation kRest = {};

HeadRotation Yawed(double degrees) { return {.yaw_degrees = degrees}; }
HeadRotation Pitched(double degrees) { return {.pitch_degrees = degrees}; }

// One screen, spanning the given yaw and pitch, for the kOtherScreen
// tests; the centre is left zeroed, since CalibratedScreenBoxContaining
// never reads it. Every call site below passes all four as named
// literals in a comment or an obvious box shape, never a bare swap.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
CalibrationProfile ProfileWithScreen(double left, double right, double top,
                                     double bottom) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  ScreenCalibration screen;
  screen.points[static_cast<std::size_t>(CalibrationPoint::kTopLeft)] =
      {.yaw_degrees = left, .pitch_degrees = top};
  screen.points[static_cast<std::size_t>(CalibrationPoint::kTopRight)] =
      {.yaw_degrees = right, .pitch_degrees = top};
  screen.points[static_cast<std::size_t>(CalibrationPoint::kBottomLeft)] =
      {.yaw_degrees = left, .pitch_degrees = bottom};
  screen.points[static_cast<std::size_t>(CalibrationPoint::kBottomRight)] =
      {.yaw_degrees = right, .pitch_degrees = bottom};
  CalibrationProfile profile;
  profile.screens = {screen};
  return profile;
}

// Name(LookAwayKind): read back, in LookAwayKind's own order.
TEST(NameTest, EveryKindHasItsOwnName) {
  EXPECT_THAT(Name(LookAwayKind::kAverted), Eq("averted"));
  EXPECT_THAT(Name(LookAwayKind::kOtherScreen), Eq("other_screen"));
  EXPECT_THAT(Name(LookAwayKind::kAway), Eq("away"));
  EXPECT_THAT(Name(LookAwayKind::kGone), Eq("gone"));
}

// The model's LookAway.tla names each test by the property it stands for.

TEST(LookReportedTest, AGazeHeldAwayAndBackQuicklyReadsAsAverted) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 0), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 1999), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, nullptr, 2000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kAverted);
  EXPECT_EQ(report->duration_ms, 2000);
}

TEST(LookReportedTest, AGazeHeldAwayPastAvertedMsReadsAsAway) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 0), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, nullptr, 6000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kAway);
  EXPECT_EQ(report->duration_ms, 6000);
}

TEST(LookReportedTest, LookingDownCountsAsAway) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Pitched(20), kRest, nullptr, 0), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Pitched(0), kRest, nullptr, 1000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kAverted);
}

TEST(LookReportedTest, AwayIsMeasuredFromTheRestingPose) {
  LookAwayDetector detector(kSettings);
  const HeadRotation resting = {.yaw_degrees = 30, .pitch_degrees = -10};
  // 20 degrees of yaw is 10 from this resting pose: not away.
  EXPECT_EQ(detector.Update(Yawed(20), resting, nullptr, 0), std::nullopt);
  // 10 degrees of pitch is 20 from it: away.
  EXPECT_EQ(detector.Update(
                HeadRotation{.yaw_degrees = 30, .pitch_degrees = 10}, resting,
                nullptr, 1000),
           std::nullopt);
  const std::optional<LookAwayReport> report = detector.Update(
      HeadRotation{.yaw_degrees = 30, .pitch_degrees = -10}, resting, nullptr,
      2000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->duration_ms, 1000);
}

TEST(LookReportedTest, YawIsComparedAcrossTheWrapAt180Degrees) {
  LookAwayDetector detector(kSettings);
  const HeadRotation resting = {.yaw_degrees = 175};
  // -175 is 10 degrees from 175, not 350: not away.
  EXPECT_EQ(detector.Update(Yawed(-175), resting, nullptr, 0), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(175), resting, nullptr, 2000), std::nullopt);
}

// A roll is a tilt of the head, not a look away.
TEST(LookReportedTest, RollAloneIsNotAway) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(
      detector.Update(HeadRotation{.roll_degrees = 40}, kRest, nullptr, 0),
      std::nullopt);
  EXPECT_EQ(
      detector.Update(HeadRotation{.roll_degrees = 40}, kRest, nullptr, 2000),
      std::nullopt);
}

TEST(NoReportBeforeHoldTest, AGlanceThatReturnsBeforeTheHoldTimeIsNotReported) {
  LookAwayDetector detector(kSettings);
  // A big nod: 20 degrees down for 500 ms, then back, again and again.
  for (std::int64_t start = 0; start < 5000; start += 1000) {
    EXPECT_EQ(detector.Update(Pitched(20), kRest, nullptr, start),
             std::nullopt);
    EXPECT_EQ(detector.Update(Pitched(20), kRest, nullptr, start + 500),
             std::nullopt);
    EXPECT_EQ(detector.Update(Pitched(0), kRest, nullptr, start + 600),
             std::nullopt);
  }
}

TEST(NoReportWhileLearningTest, NothingIsReportedWithoutARestingPose) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(30), std::nullopt, nullptr, 0),
           std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(30), std::nullopt, nullptr, 2000),
           std::nullopt);
  // Nor does a hold that began while learning carry over.
  EXPECT_EQ(detector.Update(Yawed(30), kRest, nullptr, 2500), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, nullptr, 3500);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->duration_ms, 1000);  // From 2500, not from 0.
}

// LookAway.tla's Gap: the resting pose is learned afresh, which clears a
// look already open, so it is not folded into whatever comes after. Two
// detectors, not one: Return (fired once by the first assertion) already
// closes the look, so the second assertion needs its own run to show the
// duration measured is the later one, not a continuation of the first.
TEST(NoReportWhileLearningTest, AGapClearsALookSoItIsNotYetHeldJustAfter) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(30), kRest, nullptr, 0), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(30), std::nullopt, nullptr, 500),
           std::nullopt);  // The gap.
  EXPECT_EQ(detector.Update(Yawed(30), kRest, nullptr, 600), std::nullopt);
  // Were the look not cleared by the gap, 1,599 - 0 would already be past
  // hold_ms; measured from 600 (the gap's own end), it is not.
  EXPECT_EQ(detector.Update(Yawed(0), kRest, nullptr, 1599), std::nullopt);
}

TEST(NoReportWhileLearningTest, AGapClearsALookSoDurationStartsAfterIt) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(30), kRest, nullptr, 0), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(30), std::nullopt, nullptr, 500),
           std::nullopt);  // The gap.
  EXPECT_EQ(detector.Update(Yawed(30), kRest, nullptr, 600), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, nullptr, 1600);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->duration_ms, 1000);  // From 600, the gap's own end.
}

TEST(OncePerLookTest, OneReportPerLookEvenWhileStillAway) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 0), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 1000), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 2000), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, nullptr, 3000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->duration_ms, 3000);
}

TEST(RearmOnlyAtRestTest, HoveringRightAtTheThresholdIsOneLook) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 0), std::nullopt);
  // 10 degrees is past half the threshold (7.5), so the look stays open.
  EXPECT_EQ(detector.Update(Yawed(10), kRest, nullptr, 1000), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 1500), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, nullptr, 2000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->duration_ms, 2000);  // One look, from 0, not from 1500.
}

TEST(LookRearmsTest, ANewLookIsReportedOnceTheGazeHasReallyComeBack) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(20), kRest, nullptr, 0), std::nullopt);
  const std::optional<LookAwayReport> first =
      detector.Update(Yawed(0), kRest, nullptr, 1000);
  ASSERT_TRUE(first.has_value());
  const std::optional<LookAwayReport> second =
      detector.Update(Yawed(20), kRest, nullptr, 1001);
  EXPECT_EQ(second, std::nullopt);
  const std::optional<LookAwayReport> third =
      detector.Update(Yawed(0), kRest, nullptr, 2001);
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(third->duration_ms, 1000);
}

// Return, called with nothing open (the gaze was already at rest), is a
// no-op: there is no look to end.
TEST(ReturnTest, DoesNothingWithNoLookOpen) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(0), kRest, nullptr, 0), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(2), kRest, nullptr, 1000), std::nullopt);
}

// #141's kOtherScreen: gaze landing inside a calibrated screen's own box
// while away.

TEST(OtherScreenKindTest, GazeInsideAnotherScreensBoxReadsAsThatKind) {
  const CalibrationProfile profile = ProfileWithScreen(20, 40, -5, 5);
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(30), kRest, &profile, 0), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, &profile, 1000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kOtherScreen);
}

TEST(OtherScreenKindTest, OtherScreenWinsOverDurationEvenWhenBackQuickly) {
  const CalibrationProfile profile = ProfileWithScreen(20, 40, -5, 5);
  LookAwayDetector detector(kSettings);
  // Away for only just over hold_ms, well under averted_ms: this would
  // otherwise read as kAverted.
  EXPECT_EQ(detector.Update(Yawed(30), kRest, &profile, 0), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, &profile, 1001);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kOtherScreen);
}

TEST(OtherScreenKindTest, OutsideEveryScreensBoxReadsAsAwayOrAverted) {
  const CalibrationProfile profile = ProfileWithScreen(20, 40, -5, 5);
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(90), kRest, &profile, 0), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, &profile, 1000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kAverted);
}

TEST(OtherScreenKindTest, NoProfileNeverReadsAsOtherScreen) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(30), kRest, nullptr, 0), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, nullptr, 1000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kAverted);
}

TEST(OtherScreenKindTest, WithinTheBoxOnlyByItsOwnMargin) {
  const CalibrationProfile profile = ProfileWithScreen(20, 40, -5, 5);
  LookAwayDetector detector(kSettings);
  // 18 degrees is past the box's own 20-degree edge, but within its
  // 3-degree margin.
  EXPECT_EQ(detector.Update(Yawed(18), kRest, &profile, 0), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, &profile, 1000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kOtherScreen);
}

TEST(OtherScreenKindTest, TheBoxIsOnlyReadOnceTrulyAway) {
  // 10 degrees is within the resting pose's own `degrees` threshold (15),
  // so it never reads as away at all, whether or not it sits in a box.
  const CalibrationProfile profile = ProfileWithScreen(5, 15, -5, 5);
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(Yawed(10), kRest, &profile, 0), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(0), kRest, &profile, 1000), std::nullopt);
}

// #141's kGone: no face to read gaze from.

TEST(GoneKindTest, NoFaceHeldLongEnoughReadsAsGone) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(std::nullopt, kRest, nullptr, 0), std::nullopt);
  EXPECT_EQ(detector.Update(std::nullopt, kRest, nullptr, 500), std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, nullptr, 1000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kGone);
  EXPECT_EQ(report->duration_ms, 1000);
}

TEST(GoneKindTest, ABriefNoFaceThatReturnsBeforeTheHoldTimeIsAGlance) {
  LookAwayDetector detector(kSettings);
  EXPECT_EQ(detector.Update(std::nullopt, kRest, nullptr, 0), std::nullopt);
  EXPECT_EQ(detector.Update(Yawed(0), kRest, nullptr, 500), std::nullopt);
}

TEST(GoneKindTest, GoneWinsOverOtherScreenAndDuration) {
  const CalibrationProfile profile = ProfileWithScreen(20, 40, -5, 5);
  LookAwayDetector detector(kSettings);
  // Looks at the other screen first, then the face is lost, then found
  // again, back at rest.
  EXPECT_EQ(detector.Update(Yawed(30), kRest, &profile, 0), std::nullopt);
  EXPECT_EQ(detector.Update(std::nullopt, kRest, &profile, 1000),
           std::nullopt);
  const std::optional<LookAwayReport> report =
      detector.Update(Yawed(0), kRest, &profile, 7000);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->kind, LookAwayKind::kGone);
  EXPECT_EQ(report->duration_ms, 7000);
}

TEST(GoneKindTest, NoFaceFromTheVeryFirstFrameStartsTheLookToo) {
  LookAwayDetector detector(kSettings);
  const std::optional<LookAwayReport> during_gap =
      detector.Update(std::nullopt, std::nullopt, nullptr, 0);
  EXPECT_EQ(during_gap, std::nullopt);  // No resting pose yet either.
}

}  // namespace
}  // namespace facepalm
// NOLINTEND(bugprone-unchecked-optional-access)
