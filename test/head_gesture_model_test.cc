// SPDX-FileCopyrightText: 2024 Idiap Research Institute <contact@idiap.ch>
// SPDX-License-Identifier: GPL-3.0-only
//
// Pins the C++ port to numbers produced by Idiap's own
// ccdbhg-head-gesture-recognition code (test/head_gesture_reference.h).

#include "src/head_gesture_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "src/head_pose.h"
#include "test/head_gesture_reference.h"

namespace facepalm::head_gesture {
namespace {

using test_reference::CnnCase;
using test_reference::EulerXyzCase;
using test_reference::IdiapHeadPoseCase;
using test_reference::kEulerXyzCases;
using test_reference::kIdiapHeadPoseCases;
using test_reference::kImpulseCnnCase;
using test_reference::kRampCnnCase;
using test_reference::kShakeLiveCase;
using test_reference::kSmallNodLiveCase;
using test_reference::kStationaryLiveCase;
using test_reference::kTiltLiveCase;
using test_reference::kTurnLiveCase;
using test_reference::kWaveCnnCase;
using test_reference::kZerosCnnCase;
using test_reference::LiveCase;

constexpr double kAngleTolerance = 1e-9;
constexpr float kProbabilityTolerance = 1e-4F;
constexpr float kFeatureTolerance = 1e-4F;
constexpr std::size_t kFlatFeatureSize =
    static_cast<std::size_t>(kWindowFrames) * kRawChannels;

// Unflattens a row-major 3x3 matrix into a Rotation, sidestepping
// std::array's own nested-aggregate brace rules in generated test data.
Rotation ToRotation(const std::array<double, 9>& flat) {
  Rotation rotation{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      rotation.rows.at(row).at(column) = flat.at((row * 3) + column);
    }
  }
  return rotation;
}

// Packs `rotation` times `scale`, with a translation, column by column, as
// test/head_pose_test.cc's own Pose helper does.
PoseMatrix Pose(const Rotation& rotation, float scale) {
  PoseMatrix pose{};
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) {
      pose.at((column * 4) + row) =
          scale * static_cast<float>(rotation.rows.at(row).at(column));
    }
  }
  pose[12] = 3.0F;
  pose[13] = -8.0F;
  pose[14] = 42.0F;
  pose[15] = 1.0F;
  return pose;
}

NormalizedFeatures ToFeatures(const std::array<float, kFlatFeatureSize>& flat) {
  NormalizedFeatures out{};
  for (int t = 0; t < kWindowFrames; ++t) {
    for (int c = 0; c < kRawChannels; ++c) {
      out.at(t).at(c) =
          flat.at((static_cast<std::size_t>(t) * kRawChannels) + c);
    }
  }
  return out;
}

std::array<RawFrame, kWindowFrames> ToWindow(
    const std::array<float, kFlatFeatureSize>& flat) {
  return ToFeatures(flat);
}

void ExpectProbabilitiesNear(const Prediction& actual,
                             const std::array<float, kClasses>& expected) {
  for (int i = 0; i < kClasses; ++i) {
    EXPECT_NEAR(actual.probabilities.at(i), expected.at(i),
                kProbabilityTolerance)
        << "class " << i;
  }
}

// Sets every landmark in `indices` to the same (x, y, z), so its group
// average is exactly that point.
void SetGroup(FpFace& face, std::span<const int> indices,
              std::array<float, 3> point) {
  for (const int index : indices) {
    face.landmarks[index][0] = point[0];
    face.landmarks[index][1] = point[1];
    face.landmarks[index][2] = point[2];
  }
}

class EulerXyzTest : public testing::TestWithParam<EulerXyzCase> {};

TEST_P(EulerXyzTest, MatchesScipysAsEulerXyz) {
  const std::array<double, 3> actual =
      EulerXyz(ToRotation(GetParam().matrix_row_major));
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(actual.at(i), GetParam().expected_abc.at(i), kAngleTolerance)
        << "component " << i;
  }
}

INSTANTIATE_TEST_SUITE_P(FromReference, EulerXyzTest,
                         testing::ValuesIn(kEulerXyzCases));

class IdiapHeadPoseTest : public testing::TestWithParam<IdiapHeadPoseCase> {};

TEST_P(IdiapHeadPoseTest, MatchesGetHeadPose) {
  const std::array<double, 3> actual =
      IdiapHeadPoseFromRotation(ToRotation(GetParam().matrix_row_major));
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(actual.at(i), GetParam().expected_yaw_roll_pitch.at(i),
                kAngleTolerance)
        << "component " << i;
  }
}

INSTANTIATE_TEST_SUITE_P(FromReference, IdiapHeadPoseTest,
                         testing::ValuesIn(kIdiapHeadPoseCases));

// left_eye's 16 indices get one distinct point and a second point for the
// rest, so its mean is easy to check by hand; every other group gets one
// constant point, so its mean is that point. The pose is the identity, at a
// scale that must divide out.
FpFace MakeAveragingTestFace() {
  FpFace face{};
  constexpr std::array<int, 16> kLeftEyeIndices = {
      130, 247, 30, 29, 27, 28, 56, 190, 243, 112, 26, 22, 23, 24, 110, 25};
  SetGroup(face, std::span(kLeftEyeIndices).subspan(1), {0.3F, 0.2F, 0.3F});
  SetGroup(face, std::span(kLeftEyeIndices).first(1), {0.1F, 0.2F, 0.3F});
  constexpr std::array<int, 16> kRightEyeIndices = {
      463, 414, 286, 258, 257, 259, 260, 467,
      359, 255, 339, 254, 253, 252, 256, 341};
  SetGroup(face, kRightEyeIndices, {0.6F, 0.25F, 0.1F});
  constexpr std::array<int, 15> kNoseIndices = {
      51, 5, 281, 45, 4, 275, 220, 134, 236, 3, 195, 248, 456, 363, 440};
  SetGroup(face, kNoseIndices, {0.5F, 0.3F, 0.05F});
  constexpr std::array<int, 4> kEarLeftIndices = {234, 93, 227, 137};
  SetGroup(face, kEarLeftIndices, {0.2F, 0.28F, -0.1F});
  constexpr std::array<int, 4> kEarRightIndices = {454, 323, 447, 366};
  SetGroup(face, kEarRightIndices, {0.8F, 0.28F, -0.1F});

  const Rotation identity = ToRotation({1, 0, 0, 0, 1, 0, 0, 0, 1});
  const PoseMatrix pose = Pose(identity, 2.5F);
  std::ranges::copy(pose, std::begin(face.pose_column_major));
  return face;
}

TEST(RawFrameFromTest, AveragesEachGroupAndScalesToPixels) {
  constexpr int kWidth = 200;
  constexpr int kHeight = 100;
  const RawFrame frame = RawFrameFrom(MakeAveragingTestFace(), kWidth, kHeight);

  // left_eye mean x = (0.1 + 15*0.3)/16 = 0.290625, scaled by width.
  constexpr float kLeftEyeMeanX = ((0.1F + (15 * 0.3F)) / 16) * kWidth;
  EXPECT_NEAR(frame.at(0), kLeftEyeMeanX, 1e-3F);
  EXPECT_NEAR(frame.at(1), 0.2 * kHeight, 1e-3F);  // left_eye y.
  EXPECT_NEAR(frame.at(2), 0.3 * kWidth, 1e-3F);   // left_eye z (x's scale).
  EXPECT_NEAR(frame.at(3), 0.6 * kWidth, 1e-3F);   // right_eye x.
  EXPECT_NEAR(frame.at(6), 0.5 * kWidth, 1e-3F);   // nose x.
  EXPECT_NEAR(frame.at(9), 0.2 * kWidth, 1e-3F);   // ear_l x.
  EXPECT_NEAR(frame.at(12), 0.8 * kWidth, 1e-3F);  // ear_r x.
}

TEST(RawFrameFromTest, TheIdentityPoseIsFacingStraightAhead) {
  const RawFrame frame = RawFrameFrom(MakeAveragingTestFace(), 200, 100);
  constexpr std::array<double, 3> kZero{0.0, 0.0, 0.0};
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(frame.at(15 + i), kZero.at(i), 1e-4);
  }
}

class CnnCaseTest : public testing::TestWithParam<CnnCase> {};

TEST_P(CnnCaseTest, MatchesCnnModelForwardAndSoftmax) {
  const Prediction prediction = PredictFromNormalizedFeatures(
      ToFeatures(GetParam().feature_tensor_31x18));
  ExpectProbabilitiesNear(prediction, GetParam().probabilities);
}

INSTANTIATE_TEST_SUITE_P(FromReference, CnnCaseTest,
                         testing::Values(kZerosCnnCase, kRampCnnCase,
                                         kWaveCnnCase, kImpulseCnnCase));

class LiveCaseTest : public testing::TestWithParam<LiveCase> {};

TEST_P(LiveCaseTest, PredictMatchesTheLivePathEndToEnd) {
  const Prediction prediction = Predict(ToWindow(GetParam().expected_fixed_y));
  ExpectProbabilitiesNear(prediction, GetParam().expected_probabilities);
}

// Feeds every frame of `live_case`'s track into `window`, checking it is not
// ready before the last one, and returns its result.
std::optional<std::array<RawFrame, kWindowFrames>> FeedTrack(
    Window& window, const LiveCase& live_case) {
  std::optional<std::array<RawFrame, kWindowFrames>> result;
  for (std::size_t i = 0; i < live_case.frames.size(); ++i) {
    result = window.Add(live_case.frames.at(i), live_case.time_stream_ms.at(i));
    if (i + 1 < live_case.frames.size()) {
      EXPECT_FALSE(result.has_value()) << "frame " << i;
    }
  }
  return result;
}

TEST_P(LiveCaseTest, WindowResamplesToTheExpectedFixedY) {
  Window window;
  const std::optional<std::array<RawFrame, kWindowFrames>> result =
      FeedTrack(window, GetParam());
  ASSERT_TRUE(result.has_value());
  // value_or: clang-tidy cannot see the assertion; zeroes would fail below.
  const std::array<RawFrame, kWindowFrames> actual = result.value_or({});
  const std::array<RawFrame, kWindowFrames> expected =
      ToWindow(GetParam().expected_fixed_y);
  for (int t = 0; t < kWindowFrames; ++t) {
    for (int c = 0; c < kRawChannels; ++c) {
      EXPECT_NEAR(actual.at(t).at(c), expected.at(t).at(c), kFeatureTolerance)
          << "t=" << t << " c=" << c;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(FromReference, LiveCaseTest,
                         testing::Values(kStationaryLiveCase, kSmallNodLiveCase,
                                         kShakeLiveCase, kTurnLiveCase,
                                         kTiltLiveCase));

TEST(WindowTest, ResetDropsBufferedFrames) {
  Window window;
  const RawFrame frame{};
  constexpr std::int64_t kFrameGapMs = 70;
  for (int i = 0; i < kMinTrackFrames - 1; ++i) {
    EXPECT_FALSE(window.Add(frame, static_cast<std::int64_t>(i) * kFrameGapMs)
                     .has_value());
  }
  window.Reset();
  for (int i = 0; i < kMinTrackFrames - 1; ++i) {
    EXPECT_FALSE(window.Add(frame, static_cast<std::int64_t>(i) * kFrameGapMs)
                     .has_value())
        << "should need a fresh kMinTrackFrames after Reset";
  }
}

TEST(WindowTest, NotReadyWhenTheBufferedFramesDoNotSpanLongEnough) {
  // At a high enough configured frame rate, kMinTrackFrames frames can span
  // less than kWindowDurationMs; Idiap's own time_interpolation would raise
  // here, so Add instead reports "not ready yet" (#34 design note).
  Window window;
  const RawFrame frame{};
  constexpr std::int64_t kFrameGapMs = 5;  // Far too fast for a second.
  std::optional<std::array<RawFrame, kWindowFrames>> result;
  for (int i = 0; i < kMinTrackFrames + 20; ++i) {
    result = window.Add(frame, static_cast<std::int64_t>(i) * kFrameGapMs);
  }
  EXPECT_FALSE(result.has_value());
}

TEST(ClassifyTest, ReturnsTheMostLikelyClassAboveTheThreshold) {
  Prediction prediction{};
  prediction.probabilities = {0.1F, 0.6F, 0.1F, 0.1F, 0.05F, 0.05F};
  EXPECT_EQ(Classify(prediction, 0.5F), Gesture::kNod);
}

TEST(ClassifyTest, ReturnsNoneBelowTheThreshold) {
  Prediction prediction{};
  prediction.probabilities = {0.3F, 0.3F, 0.1F, 0.1F, 0.1F, 0.1F};
  EXPECT_EQ(Classify(prediction, 0.5F), Gesture::kNone);
}

TEST(ClassifyTest, NanNeverReadsAsAGesture) {
  Prediction prediction{};
  prediction.probabilities.fill(std::numeric_limits<float>::quiet_NaN());
  EXPECT_EQ(Classify(prediction, 0.5F), Gesture::kNone);
}

}  // namespace
}  // namespace facepalm::head_gesture
