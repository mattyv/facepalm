#include "src/nodding_pigeon.h"

#include <array>
#include <cmath>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm::nodding_pigeon {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::FloatNear;

constexpr float kTolerance = 1e-6F;

// A face box at (0.2, 0.1), 0.5 wide and 0.4 tall, with key points at x
// 0.25 to 0.5 and y 0.2 to 0.45 in steps of 0.05, then moved by `shift`.
RawFrame FrameAt(float shift) {
  RawFrame frame = {0.2F, 0.1F, 0.5F, 0.4F};
  for (int i = 0; i < 6; ++i) {
    frame[4 + i] = 0.25F + (0.05F * static_cast<float>(i)) + shift;
    frame[10 + i] = 0.2F + (0.05F * static_cast<float>(i)) + shift;
  }
  return frame;
}

TEST(PreprocessTest, MakesKeyPointsRelativeToTheBoxAndAddsTheirSpread) {
  const auto features = Preprocess(FrameAt(0));

  // x: (0.25 - 0.2) / 0.5 = 0.1, then steps of 0.05 / 0.5 = 0.1.
  // y: (0.2 - 0.1) / 0.4 = 0.25, then steps of 0.05 / 0.4 = 0.125.
  // Six evenly spaced values have a population spread of step * 1.7078.
  const float spread = std::sqrt(35.0F / 12.0F);
  EXPECT_THAT(
      features,
      ElementsAre(FloatNear(0.1F, kTolerance), FloatNear(0.2F, kTolerance),
                  FloatNear(0.3F, kTolerance), FloatNear(0.4F, kTolerance),
                  FloatNear(0.5F, kTolerance), FloatNear(0.6F, kTolerance),
                  FloatNear(0.25F, kTolerance), FloatNear(0.375F, kTolerance),
                  FloatNear(0.5F, kTolerance), FloatNear(0.625F, kTolerance),
                  FloatNear(0.75F, kTolerance), FloatNear(0.875F, kTolerance),
                  FloatNear(0.1F * spread, kTolerance),
                  FloatNear(0.125F * spread, kTolerance)));
}

TEST(PredictTest, GivesProbabilities) {
  std::array<RawFrame, kFrames> frames{};
  for (int i = 0; i < kFrames; ++i) {
    frames[i] = FrameAt(0.02F * std::sin(static_cast<float>(i)));
  }

  const Prediction prediction = Predict(frames);

  EXPECT_GE(prediction.has_motion, 0);
  EXPECT_LE(prediction.has_motion, 1);
  EXPECT_THAT(prediction.gestures[0] + prediction.gestures[1],
              FloatNear(1, kTolerance));
  EXPECT_GE(prediction.gestures[0], 0);
  EXPECT_GE(prediction.gestures[1], 0);
}

// FrameAt(0) with the key points' x (axis 0) or y (axis 1) moved by a
// triangle wave 0.05 high with period eight. FrameAt's own shifts leave
// every convolution ReLU at zero, where only the biases speak; these waves
// fire half of them, so the expected values below pin the kernels too.
RawFrame WaveFrame(int i, int axis) {
  constexpr std::array<float, 8> kWave = {0.0F, 0.5F,  1.0F,  0.5F,
                                          0.0F, -0.5F, -1.0F, -0.5F};
  RawFrame frame = FrameAt(0);
  for (int j = 0; j < 6; ++j) {
    frame[4 + (6 * axis) + j] += 0.05F * kWave[i % 8];
  }
  return frame;
}

// The expected values come from Nodding Pigeon's weights run through a NumPy
// replica of model.py, written independently of nodding_pigeon.cc, on these
// exact frames. They pin the Conv1D kernel layout, strides, the
// preprocessing, and both heads: a wrong layout moves them far outside the
// margin, which only allows for float32 against float64.
constexpr float kModelMargin = 1e-5F;

TEST(PredictTest, MatchesTheOriginalModelOnAHorizontalWave) {
  std::array<RawFrame, kFrames> frames{};
  for (int i = 0; i < kFrames; ++i) {
    frames[i] = WaveFrame(i, 0);
  }

  const Prediction prediction = Predict(frames);

  EXPECT_THAT(prediction.has_motion, FloatNear(1.0F, kModelMargin));
  EXPECT_THAT(prediction.gestures[0], FloatNear(0.95536832F, kModelMargin));
  EXPECT_THAT(prediction.gestures[1], FloatNear(0.04463168F, kModelMargin));
}

TEST(PredictTest, MatchesTheOriginalModelOnAVerticalWave) {
  std::array<RawFrame, kFrames> frames{};
  for (int i = 0; i < kFrames; ++i) {
    frames[i] = WaveFrame(i, 1);
  }

  const Prediction prediction = Predict(frames);

  EXPECT_THAT(prediction.has_motion, FloatNear(0.00998810F, kModelMargin));
  EXPECT_THAT(prediction.gestures[0], FloatNear(0.99387355F, kModelMargin));
  EXPECT_THAT(prediction.gestures[1], FloatNear(0.00612645F, kModelMargin));
}

TEST(ClassifyTest, IsStationaryUnderTheMotionThreshold) {
  EXPECT_THAT(Classify({.has_motion = 0.49F, .gestures = {1, 0}}, 0.5F, 0.9F),
              Eq(Gesture::kStationary));
}

TEST(ClassifyTest, IsTheLikelierGestureAtTheGestureThreshold) {
  EXPECT_THAT(
      Classify({.has_motion = 0.5F, .gestures = {0.9F, 0.1F}}, 0.5F, 0.9F),
      Eq(Gesture::kNodding));
  EXPECT_THAT(
      Classify({.has_motion = 1, .gestures = {0.05F, 0.95F}}, 0.5F, 0.9F),
      Eq(Gesture::kTurning));
}

TEST(ClassifyTest, IsUndefinedWhenNeitherGestureIsLikelyEnough) {
  EXPECT_THAT(Classify({.has_motion = 1, .gestures = {0.6F, 0.4F}}, 0.5F, 0.9F),
              Eq(Gesture::kUndefined));
}

TEST(WindowTest, GivesNothingUntilItHasSixtyFrames) {
  Window window;
  for (int i = 0; i < kFrames - 1; ++i) {
    EXPECT_THAT(window.Add(FrameAt(0.01F * i)), Eq(std::nullopt));
  }
  EXPECT_TRUE(window.Add(FrameAt(0.6F)).has_value());
}

TEST(WindowTest, KeepsTheLastSixtyOldestFirst) {
  Window window;
  std::optional<std::array<RawFrame, kFrames>> frames;
  for (int i = 0; i < kFrames + 1; ++i) {
    frames = window.Add(FrameAt(0.01F * static_cast<float>(i)));
  }
  ASSERT_TRUE(frames.has_value());
  // value_or: clang-tidy cannot see the assertion; zeroes would fail below.
  const std::array<RawFrame, kFrames> kept = frames.value_or({});
  EXPECT_THAT(kept[0][4], FloatNear(0.26F, kTolerance));
  EXPECT_THAT(kept[kFrames - 1][4], FloatNear(0.85F, kTolerance));
}

TEST(WindowTest, DropsAFrameEqualToTheLastToTwoDecimalPlaces) {
  Window window;
  for (int i = 0; i < kFrames - 1; ++i) {
    window.Add(FrameAt(0.01F * static_cast<float>(i)));
  }
  // 0.5804 rounds as 0.58 did, frame 58's shift, so it is not kept.
  EXPECT_THAT(window.Add(FrameAt(0.5804F)), Eq(std::nullopt));
  EXPECT_TRUE(window.Add(FrameAt(0.6F)).has_value());
}

TEST(WindowTest, PadsAShortRunWithItsLastFrame) {
  Window window;
  EXPECT_THAT(window.Padded(), Eq(std::nullopt));
  window.Add(FrameAt(0));
  window.Add(FrameAt(0.1F));

  ASSERT_TRUE(window.Padded().has_value());
  // value_or: clang-tidy cannot see the assertion; zeroes would fail below.
  const std::array<RawFrame, kFrames> padded = window.Padded().value_or({});
  EXPECT_THAT(padded[0][4], FloatNear(0.25F, kTolerance));
  EXPECT_THAT(padded[1][4], FloatNear(0.35F, kTolerance));
  EXPECT_THAT(padded[kFrames - 1][4], FloatNear(0.35F, kTolerance));
}

TEST(RawFrameFromTest, PutsTheBoxThenEveryXThenEveryY) {
  FpDetection detection{};
  for (int i = 0; i < 4; ++i) {
    detection.box[i] = 0.01F * static_cast<float>(i);
  }
  for (int i = 0; i < FP_KEYPOINTS; ++i) {
    detection.keypoints[i][0] = 0.1F + (0.01F * static_cast<float>(i));
    detection.keypoints[i][1] = 0.2F + (0.01F * static_cast<float>(i));
  }

  const RawFrame frame = RawFrameFrom(detection);

  EXPECT_THAT(frame[3], FloatNear(0.03F, kTolerance));
  EXPECT_THAT(frame[4], FloatNear(0.1F, kTolerance));
  EXPECT_THAT(frame[9], FloatNear(0.15F, kTolerance));
  EXPECT_THAT(frame[10], FloatNear(0.2F, kTolerance));
  EXPECT_THAT(frame[15], FloatNear(0.25F, kTolerance));
}

}  // namespace
}  // namespace facepalm::nodding_pigeon
