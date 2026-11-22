#include "src/eye_gaze.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "src/face_model.h"
#include "src/head_pose.h"

// GoogleTest assertions check each optional before access.
// NOLINTBEGIN(bugprone-unchecked-optional-access)
namespace facepalm {
namespace {

constexpr double kBlinkThreshold = 0.5;

// Every eye-look and blink score src/eyes_probe.h reads, named, so a
// test builds a Face naming only the fields it cares about
// (bugprone-easily-swappable-parameters: an options object in place of
// ten positional floats of the same type).
struct EyeScores {
  float look_down_left = 0;
  float look_down_right = 0;
  float look_in_left = 0;
  float look_in_right = 0;
  float look_out_left = 0;
  float look_out_right = 0;
  float look_up_left = 0;
  float look_up_right = 0;
  float blink_left = 0;
  float blink_right = 0;
};

// A face with only the eye-look and blink blendshapes src/eyes_probe.h
// reads set, each from a distinct field, so a wrong index or a wrong
// sign shows up as a wrong result rather than passing by accident.
Face FaceWithEyes(const EyeScores& scores) {
  Face face{};
  face.blendshapes[11] = scores.look_down_left;
  face.blendshapes[12] = scores.look_down_right;
  face.blendshapes[13] = scores.look_in_left;
  face.blendshapes[14] = scores.look_in_right;
  face.blendshapes[15] = scores.look_out_left;
  face.blendshapes[16] = scores.look_out_right;
  face.blendshapes[17] = scores.look_up_left;
  face.blendshapes[18] = scores.look_up_right;
  face.blendshapes[9] = scores.blink_left;
  face.blendshapes[10] = scores.blink_right;
  return face;
}

// A face with every eye-look and blink score at 0: looking straight
// ahead, eyes open, no blink.
Face CentredFace() { return FaceWithEyes({}); }

TEST(EyeSignalFromTest, ALeftLookReadsPositiveHorizontal) {
  // Looking to the subject's left: the left eye looks out, the right eye
  // looks in.
  const Face face =
      FaceWithEyes({.look_in_right = 0.4F, .look_out_left = 0.4F});
  const EyeSignal signal = EyeSignalFrom(face, kBlinkThreshold);
  EXPECT_GT(signal.horizontal, 0);
  EXPECT_FALSE(signal.blink);
}

TEST(EyeSignalFromTest, ARightLookReadsNegativeHorizontal) {
  const Face face =
      FaceWithEyes({.look_in_left = 0.4F, .look_out_right = 0.4F});
  const EyeSignal signal = EyeSignalFrom(face, kBlinkThreshold);
  EXPECT_LT(signal.horizontal, 0);
}

TEST(EyeSignalFromTest, LookingDownReadsPositiveVertical) {
  const Face face =
      FaceWithEyes({.look_down_left = 0.4F, .look_down_right = 0.4F});
  const EyeSignal signal = EyeSignalFrom(face, kBlinkThreshold);
  EXPECT_GT(signal.vertical, 0);
}

TEST(EyeSignalFromTest, LookingUpReadsNegativeVertical) {
  const Face face =
      FaceWithEyes({.look_up_left = 0.4F, .look_up_right = 0.4F});
  const EyeSignal signal = EyeSignalFrom(face, kBlinkThreshold);
  EXPECT_LT(signal.vertical, 0);
}

TEST(EyeSignalFromTest, ABlinkPassesTheThreshold) {
  const Face face = FaceWithEyes({.blink_left = 0.9F, .blink_right = 0.9F});
  EXPECT_TRUE(EyeSignalFrom(face, kBlinkThreshold).blink);
}

TEST(EyeSignalFromTest, HalfOpenEyesAreNotABlink) {
  const Face face = FaceWithEyes({.blink_left = 0.1F, .blink_right = 0.1F});
  EXPECT_FALSE(EyeSignalFrom(face, kBlinkThreshold).blink);
}

HeadRotation Head(double yaw, double pitch) {
  return HeadRotation{.yaw_degrees = yaw, .pitch_degrees = pitch,
                      .roll_degrees = 3};
}

TEST(ComposeGazeTest, AddsTheGainedOffsetFromRestingPerAxis) {
  const HeadRotation head = Head(5, -2);
  const EyeSignal eye{.horizontal = 0.3, .vertical = -0.1};
  const EyeSignal resting{.horizontal = 0.1, .vertical = 0.0};
  const EyeGains gains{.horizontal = 10, .vertical = 20};
  const HeadRotation gaze =
      ComposeGaze(head, eye, resting, gains, /*head_only_yaw_degrees=*/35);
  EXPECT_DOUBLE_EQ(gaze.yaw_degrees, 5 + (10 * (0.3 - 0.1)));
  EXPECT_DOUBLE_EQ(gaze.pitch_degrees, -2 + (20 * (-0.1 - 0.0)));
  EXPECT_DOUBLE_EQ(gaze.roll_degrees, head.roll_degrees);
}

TEST(ComposeGazeTest, FallsBackToHeadAloneBeyondTheYawThreshold) {
  const HeadRotation head = Head(40, 0);
  const EyeSignal eye{.horizontal = 1, .vertical = 1};
  const EyeSignal resting{};
  const HeadRotation gaze = ComposeGaze(
      head, eye, resting, EyeGains{.horizontal = 10, .vertical = 10}, 35);
  EXPECT_DOUBLE_EQ(gaze.yaw_degrees, head.yaw_degrees);
  EXPECT_DOUBLE_EQ(gaze.pitch_degrees, head.pitch_degrees);
}

TEST(ComposeGazeTest, FallsBackToHeadAloneWithoutEyeSignal) {
  const HeadRotation head = Head(5, 5);
  const HeadRotation gaze =
      ComposeGaze(head, std::nullopt, std::nullopt, EyeGains{}, 35);
  EXPECT_DOUBLE_EQ(gaze.yaw_degrees, head.yaw_degrees);
  EXPECT_DOUBLE_EQ(gaze.pitch_degrees, head.pitch_degrees);
}

TEST(ComposeGazeTest, FallsBackToHeadAloneWithoutARestingLevel) {
  const HeadRotation head = Head(5, 5);
  const EyeSignal eye{.horizontal = 1, .vertical = 1};
  const HeadRotation gaze =
      ComposeGaze(head, eye, std::nullopt, EyeGains{.horizontal = 10}, 35);
  EXPECT_DOUBLE_EQ(gaze.yaw_degrees, head.yaw_degrees);
}

class EyeGazeTrackerTest : public ::testing::Test {
 protected:
  EyeGazeTracker::Settings settings_{.blink_threshold = kBlinkThreshold,
                                     .median_ms = 250,
                                     .resting_ms = 1000,
                                     .gap_ms = 500};
  EyeGazeTracker tracker_{settings_};
};

TEST_F(EyeGazeTrackerTest, ReportsNeitherSignalNorRestingBeforeAnyFrame) {
  EXPECT_FALSE(tracker_.Signal().has_value());
  EXPECT_FALSE(tracker_.Resting().has_value());
}

TEST_F(EyeGazeTrackerTest, LearnsARestingLevelOverTheRestingWindow) {
  // Centred eyes, steady, for just over resting_ms: by then the level is
  // learned and a face arriving the same instant also reports a signal.
  const Face centred = CentredFace();
  for (std::int64_t ms = 0; ms <= 1000; ms += 100) {
    tracker_.Update(centred, ms);
  }
  ASSERT_TRUE(tracker_.Resting().has_value());
  EXPECT_DOUBLE_EQ(tracker_.Resting()->horizontal, 0);
  ASSERT_TRUE(tracker_.Signal().has_value());
  EXPECT_DOUBLE_EQ(tracker_.Signal()->horizontal, 0);
}

TEST_F(EyeGazeTrackerTest, DoesNotReportRestingBeforeItHasHadLongEnough) {
  const Face centred = CentredFace();
  tracker_.Update(centred, 0);
  tracker_.Update(centred, 500);
  EXPECT_FALSE(tracker_.Resting().has_value());
}

TEST_F(EyeGazeTrackerTest, SkipsBlinksEntirely) {
  const Face centred = CentredFace();
  for (std::int64_t ms = 0; ms <= 1000; ms += 100) {
    tracker_.Update(centred, ms);
  }
  const Face blinking = FaceWithEyes({.look_in_left = 0.9F,
                                      .look_in_right = 0.9F,
                                      .blink_left = 0.9F,
                                      .blink_right = 0.9F});
  const std::optional<EyeSignal> resting_before = tracker_.Resting();
  tracker_.Update(blinking, 1100);
  // A blink neither moves the signal nor disturbs the already-learned
  // resting level: Signal() still reads the last non-blink median.
  ASSERT_TRUE(tracker_.Signal().has_value());
  EXPECT_DOUBLE_EQ(tracker_.Signal()->horizontal, 0);
  ASSERT_TRUE(tracker_.Resting().has_value());
  ASSERT_TRUE(resting_before.has_value());
  EXPECT_DOUBLE_EQ(tracker_.Resting()->horizontal, resting_before->horizontal);
}

TEST_F(EyeGazeTrackerTest, MedianFilterIgnoresASingleOutlierFrame) {
  const Face centred = CentredFace();
  std::int64_t ms = 0;
  for (; ms <= 1000; ms += 50) {
    tracker_.Update(centred, ms);
  }
  // One frame, briefly, reads as a large look-out; the median of the
  // window it joins stays at 0 since every other recent frame is centred.
  const Face outlier = FaceWithEyes({.look_out_left = 0.9F});
  tracker_.Update(outlier, ms);
  ASSERT_TRUE(tracker_.Signal().has_value());
  EXPECT_DOUBLE_EQ(tracker_.Signal()->horizontal, 0);
}

TEST_F(EyeGazeTrackerTest, RelearnsRestingAfterAGap) {
  const Face centred = CentredFace();
  for (std::int64_t ms = 0; ms <= 1000; ms += 100) {
    tracker_.Update(centred, ms);
  }
  ASSERT_TRUE(tracker_.Resting().has_value());
  // A new posture, after a gap of more than gap_ms with no frame: the
  // resting level is learned afresh rather than averaged into the old one.
  const Face shifted =
      FaceWithEyes({.look_in_right = 0.3F, .look_out_left = 0.3F});
  tracker_.Update(shifted, 2000);
  EXPECT_FALSE(tracker_.Resting().has_value());
  for (std::int64_t ms = 2000; ms <= 3000; ms += 100) {
    tracker_.Update(shifted, ms);
  }
  ASSERT_TRUE(tracker_.Resting().has_value());
  EXPECT_GT(tracker_.Resting()->horizontal, 0);
}

TEST_F(EyeGazeTrackerTest, AMissingFaceClearsTheMedianWindow) {
  const Face centred = CentredFace();
  for (std::int64_t ms = 0; ms <= 1000; ms += 100) {
    tracker_.Update(centred, ms);
  }
  tracker_.Update(std::nullopt, 1100);
  EXPECT_FALSE(tracker_.Signal().has_value());
}

// specs/README.md carries no entry for EstimateGains: it is a stateless
// computation with no lifecycle of its own, tested directly, the way
// src/screen_context.h's own CoarseGazeFrom is.
TEST(EstimateGainsTest, RecoversTheGainFromALinearFixateAndTurn) {
  // The person fixates a dot while turning their head from -10 to 10
  // degrees of yaw; their eyes compensate at a real gain of 20 (eye moves
  // -0.5 to 0.5 as head moves 10 to -10, so head = -20 * eye, matching
  // ComposeGaze's gaze = head + 20 * eye staying at 0 throughout).
  std::vector<GainsSample> samples;
  constexpr int kSteps = 11;
  samples.reserve(kSteps);
  for (int step = -10; step <= 10; step += 2) {
    const double head_yaw = step;
    const double eye_horizontal = -head_yaw / 20.0;
    samples.push_back(
        {.head = Head(head_yaw, 0), .eye = {.horizontal = eye_horizontal}});
  }
  const std::optional<EstimatedGains> gains =
      EstimateGains(samples, /*min_travel_degrees=*/5, /*min_axis_degrees=*/2);
  ASSERT_TRUE(gains.has_value());
  ASSERT_TRUE(gains->horizontal.has_value());
  EXPECT_NEAR(*gains->horizontal, 20, 1e-9);
  EXPECT_FALSE(gains->vertical.has_value());
}

TEST(EstimateGainsTest, RefusesTheWholeStepWhenNeitherAxisTravelledEnough) {
  std::vector<GainsSample> samples;
  constexpr int kSteps = 5;
  samples.reserve(kSteps);
  for (int step = 0; step < kSteps; ++step) {
    samples.push_back({.head = Head(step * 0.1, step * 0.1),
                       .eye = {.horizontal = step * 0.01}});
  }
  EXPECT_FALSE(
      EstimateGains(samples, /*min_travel_degrees=*/5, /*min_axis_degrees=*/2)
          .has_value());
}

TEST(EstimateGainsTest, LeavesAnUnmovedAxisNulloptWithoutRefusingTheStep) {
  // Enough yaw travel to pass the step, but pitch never moves: pitch's own
  // gain is left for the caller's default rather than a divide-by-nearly-
  // zero slope.
  std::vector<GainsSample> samples;
  constexpr int kSteps = 11;
  samples.reserve(kSteps);
  for (int step = -10; step <= 10; step += 2) {
    samples.push_back({.head = Head(step, 0),
                       .eye = {.horizontal = -step / 20.0, .vertical = 0}});
  }
  const std::optional<EstimatedGains> gains =
      EstimateGains(samples, /*min_travel_degrees=*/5, /*min_axis_degrees=*/2);
  ASSERT_TRUE(gains.has_value());
  EXPECT_TRUE(gains->horizontal.has_value());
  EXPECT_FALSE(gains->vertical.has_value());
}

TEST(EstimateGainsTest, LeavesAnAxisNulloptWhenEyesNeverCompensated) {
  // Head travels enough, but the eyes stay fixed (a head-only turn, not a
  // fixate-and-turn): the axis's own slope cannot divide by eye variance.
  std::vector<GainsSample> samples;
  constexpr int kSteps = 11;
  samples.reserve(kSteps);
  for (int step = -10; step <= 10; step += 2) {
    samples.push_back({.head = Head(step, 0), .eye = {.horizontal = 0}});
  }
  const std::optional<EstimatedGains> gains =
      EstimateGains(samples, /*min_travel_degrees=*/5, /*min_axis_degrees=*/2);
  ASSERT_TRUE(gains.has_value());
  EXPECT_FALSE(gains->horizontal.has_value());
}

TEST(EstimateGainsTest, RefusesTooFewSamples) {
  const std::vector<GainsSample> samples = {
      {.head = Head(10, 10), .eye = EyeSignal{}}};
  EXPECT_FALSE(
      EstimateGains(samples, /*min_travel_degrees=*/5, /*min_axis_degrees=*/2)
          .has_value());
}

}  // namespace
}  // namespace facepalm
// NOLINTEND(bugprone-unchecked-optional-access)
