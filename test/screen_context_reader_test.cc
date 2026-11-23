#include "src/screen_context_reader.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
#include "src/eye_signs.h"
#include "src/face_model.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

constexpr std::int64_t kFrameMs = 66;  // 15 frames per second.
constexpr double kDegree = std::numbers::pi / 180;

struct Pose {
  double yaw = 0;
  double pitch = 0;
  float eye_look_out_left = 0;  // Blendshape 15, for Eye signal tests.
  // eyeBlinkLeft/Right (9, 10) and eyeWideLeft/Right (21, 22), for
  // EyeSignSample tests (#145/#165, src/eye_signs.h's own SampleFrom).
  float eye_blink = 0;
  float eye_wide = 0;
};

// As test/look_away_reader_test.cc's FaceAt: a face whose transformation
// matrix is turned `pose.yaw` and then tipped `pose.pitch`.
Face FaceAt(Pose pose) {
  const double cy = std::cos(pose.yaw * kDegree);
  const double sy = std::sin(pose.yaw * kDegree);
  const double cp = std::cos(pose.pitch * kDegree);
  const double sp = std::sin(pose.pitch * kDegree);
  const std::array<std::array<double, 3>, 3> rows = {{
      {cy, sy * sp, sy * cp},
      {0, cp, -sp},
      {-sy, cy * sp, cy * cp},
  }};
  Face face{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      face.pose_column_major[(column * 4) + row] =
          static_cast<float>(rows.at(row).at(column));
    }
  }
  face.pose_column_major[15] = 1;
  face.blendshapes[15] = pose.eye_look_out_left;
  face.blendshapes[9] = pose.eye_blink;
  face.blendshapes[10] = pose.eye_blink;
  face.blendshapes[21] = pose.eye_wide;
  face.blendshapes[22] = pose.eye_wide;
  return face;
}

class ScreenContextReaderTest : public ::testing::Test {
 protected:
  void Hold(double seconds, Pose pose = {}) {
    const int frames = static_cast<int>(seconds * 15);
    for (int frame = 0; frame < frames; ++frame) {
      now_ms_ += kFrameMs;
      reader_.OnFace(FaceAt(pose), /*width=*/640, /*height=*/480, now_ms_,
                     reactions_);
    }
  }

  std::int64_t now_ms_ = 1000;
  ScreenContextReader reader_{
      ExpressionTracker::Settings{.rise = 0.06,
                                  .pose_degrees = 10,
                                  .pose_range = 3,
                                  .settle_ms = 600,
                                  .spreads = 3,
                                  .hold_ms = 200,
                                  .eye_look_down_threshold = 0.3,
                                  .eye_blink_threshold = 0.5},
      EyeGazeTracker::Settings{.blink_threshold = 0.5,
                               .median_ms = 250,
                               .resting_ms = 3000,
                               .gap_ms = 2000}};
  std::vector<Reaction> reactions_;
};

TEST_F(ScreenContextReaderTest, ReportsNoReactionOfItsOwn) {
  Hold(5, {.yaw = 30});
  EXPECT_THAT(reactions_, ::testing::IsEmpty());
}

TEST_F(ScreenContextReaderTest, NothingBeforeTheFirstFace) {
  EXPECT_EQ(reader_.Pose(), std::nullopt);
  EXPECT_EQ(reader_.RestingPose(), std::nullopt);
  EXPECT_EQ(reader_.EyeReading(), std::nullopt);
  EXPECT_EQ(reader_.EyeResting(), std::nullopt);
}

TEST_F(ScreenContextReaderTest, EyeReadingFollowsTheLatestFace) {
  Hold(1, {.eye_look_out_left = 0.3F});
  const EyeSignal eye = reader_.EyeReading().value_or(EyeSignal{});
  EXPECT_NEAR(eye.horizontal, 0.15, 0.01);
}

TEST_F(ScreenContextReaderTest, EyeRestingIsLearnedAfterThreeSeconds) {
  Hold(4, {.eye_look_out_left = 0.2F});
  const EyeSignal resting = reader_.EyeResting().value_or(EyeSignal{});
  EXPECT_NEAR(resting.horizontal, 0.1, 0.01);
}

TEST_F(ScreenContextReaderTest, OnNoFaceClearsTheEyeReadingToo) {
  Hold(1, {.eye_look_out_left = 0.3F});
  ASSERT_TRUE(reader_.EyeReading().has_value());
  reader_.OnNoFace();
  EXPECT_EQ(reader_.EyeReading(), std::nullopt);
}

TEST_F(ScreenContextReaderTest, PoseFollowsTheLatestFace) {
  Hold(1, {.yaw = 12, .pitch = -4});
  ASSERT_TRUE(reader_.Pose().has_value());
  const HeadRotation pose = reader_.Pose().value_or(HeadRotation{});
  EXPECT_NEAR(pose.yaw_degrees, 12, 0.01);
  EXPECT_NEAR(pose.pitch_degrees, -4, 0.01);
}

TEST_F(ScreenContextReaderTest, RestingPoseIsNulloptWhileStillLearning) {
  Hold(1);
  EXPECT_EQ(reader_.RestingPose(), std::nullopt);
}

TEST_F(ScreenContextReaderTest, RestingPoseIsLearnedAfterThreeSeconds) {
  Hold(4);
  ASSERT_TRUE(reader_.RestingPose().has_value());
  const HeadRotation resting = reader_.RestingPose().value_or(HeadRotation{});
  EXPECT_NEAR(resting.yaw_degrees, 0, 0.01);
}

TEST_F(ScreenContextReaderTest, OnNoFaceClearsTheCurrentPoseOnly) {
  Hold(4, {.yaw = 5});
  ASSERT_TRUE(reader_.RestingPose().has_value());
  const HeadRotation resting_before =
      reader_.RestingPose().value_or(HeadRotation{});
  reader_.OnNoFace();
  EXPECT_EQ(reader_.Pose(), std::nullopt);
  ASSERT_TRUE(reader_.RestingPose().has_value());
  const HeadRotation resting_after =
      reader_.RestingPose().value_or(HeadRotation{});
  EXPECT_NEAR(resting_after.yaw_degrees, resting_before.yaw_degrees, 0.01);
}

// As LookAwayReader's: a degenerate pose is skipped, as a missing face.
TEST_F(ScreenContextReaderTest, ADegeneratePoseIsSkipped) {
  Hold(4, {.yaw = 5});
  now_ms_ += kFrameMs;
  reader_.OnFace(Face{}, /*width=*/640, /*height=*/480, now_ms_, reactions_);
  EXPECT_EQ(reader_.Pose(), std::nullopt);
}

// The on_sample tests below stand for #142's own per-frame hook into
// ScreenContextReporter::Sample (main.cc's own wiring): told the same
// four readings OnFace/OnNoFace just updated, plus one frame's own eye
// sign (#145/#165), once a frame regardless of whether it carries a
// reaction.
struct Sample {
  std::optional<HeadRotation> pose;
  std::optional<HeadRotation> resting;
  std::optional<EyeSignal> eye;
  std::optional<EyeSignal> eye_resting;
  std::optional<EyeSignSample> eye_sign;
};

class OnSampleTest : public ::testing::Test {
 protected:
  std::int64_t now_ms_ = 1000;
  std::vector<Sample> samples_;
  std::vector<Reaction> reactions_;
  ScreenContextReader reader_{
      ExpressionTracker::Settings{.rise = 0.06,
                                  .pose_degrees = 10,
                                  .pose_range = 3,
                                  .settle_ms = 600,
                                  .spreads = 3,
                                  .hold_ms = 200,
                                  .eye_look_down_threshold = 0.3,
                                  .eye_blink_threshold = 0.5},
      EyeGazeTracker::Settings{.blink_threshold = 0.5,
                               .median_ms = 250,
                               .resting_ms = 3000,
                               .gap_ms = 2000},
      [this](const std::optional<HeadRotation>& pose,
             const std::optional<HeadRotation>& resting,
             const std::optional<EyeSignal>& eye,
             const std::optional<EyeSignal>& eye_resting,
             const std::optional<EyeSignSample>& eye_sign) {
        samples_.push_back({.pose = pose,
                            .resting = resting,
                            .eye = eye,
                            .eye_resting = eye_resting,
                            .eye_sign = eye_sign});
      }};
};

TEST_F(OnSampleTest, OnFaceTellsItTheFourReadings) {
  reader_.OnFace(FaceAt({.yaw = 12}), /*width=*/640, /*height=*/480, now_ms_,
                 reactions_);

  // on_sample_ is told exactly what OnFace just updated: the same four
  // readings the reader's own accessors give right after (EyeSignal and
  // HeadRotation carry no operator==, so each is checked by its own
  // presence and, for pose, its value).
  ASSERT_THAT(samples_, ::testing::SizeIs(1));
  const Sample& sample = samples_.at(0);
  if (!sample.pose.has_value()) {
    ADD_FAILURE() << "no pose";
    return;
  }
  EXPECT_NEAR(sample.pose->yaw_degrees, 12, 0.01);
  // Still learning: no resting pose of its own yet.
  EXPECT_EQ(sample.resting, std::nullopt);
  EXPECT_EQ(sample.eye.has_value(), reader_.EyeReading().has_value());
  EXPECT_EQ(sample.eye_resting.has_value(), reader_.EyeResting().has_value());
}

// #145/#165: the eye sign is SampleFrom's own, read from this exact
// frame's face, not eye_tracker_'s median-filtered reading -- it is
// given even while EyeGazeTracker is still learning (sample.eye is
// nullopt above), since SampleFrom needs no resting level.
TEST_F(OnSampleTest, OnFaceTellsItThisFramesOwnEyeSign) {
  reader_.OnFace(FaceAt({.eye_blink = 0.6F, .eye_wide = 0.2F}),
                 /*width=*/640, /*height=*/480, now_ms_, reactions_);

  ASSERT_THAT(samples_, ::testing::SizeIs(1));
  const std::optional<EyeSignSample>& eye_sign = samples_.at(0).eye_sign;
  if (!eye_sign.has_value()) {
    ADD_FAILURE() << "no eye sign";
    return;
  }
  EXPECT_TRUE(eye_sign->blink);
  EXPECT_NEAR(eye_sign->eye_wide, 0.2, 1e-6);
  EXPECT_EQ(eye_sign->timestamp_ms, now_ms_);
}

TEST_F(OnSampleTest, OnNoFaceTellsItNoPoseButKeepsTheRestingLevel) {
  for (int frame = 0; frame < 60; ++frame) {
    now_ms_ += kFrameMs;
    reader_.OnFace(FaceAt({}), /*width=*/640, /*height=*/480, now_ms_,
                   reactions_);
  }
  ASSERT_TRUE(reader_.RestingPose().has_value());
  samples_.clear();

  reader_.OnNoFace();

  ASSERT_THAT(samples_, ::testing::SizeIs(1));
  EXPECT_EQ(samples_.at(0).pose, std::nullopt);
  EXPECT_EQ(samples_.at(0).eye, std::nullopt);
  EXPECT_TRUE(samples_.at(0).resting.has_value());
  // #145/#165: no face to read eyeBlink or eyeWide from either.
  EXPECT_EQ(samples_.at(0).eye_sign, std::nullopt);
}

}  // namespace
}  // namespace facepalm
