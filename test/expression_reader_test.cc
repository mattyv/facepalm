#include "src/expression_reader.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/face_model.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr std::int64_t kFrameMs = 66;  // 15 frames per second.
constexpr int kFramesPerSecond = 15;
constexpr float kSmall = 0.2F;  // A small but clear expression's rise.

// MediaPipe's indices for the scores the tests move.
constexpr int kBrowInnerUp = 3;
constexpr int kBrowOuterUpLeft = 4;
constexpr int kBrowOuterUpRight = 5;
constexpr int kMouthSmileLeft = 44;
constexpr int kMouthSmileRight = 45;

// Sets `face`'s transformation matrix to a head looking straight ahead,
// MediaPipe's identity: no rotation, no translation, unit scale.
void LookAhead(Face& face) {
  face.pose_column_major[0] = 1;
  face.pose_column_major[5] = 1;
  face.pose_column_major[10] = 1;
  face.pose_column_major[15] = 1;
}

// Sets `face`'s transformation matrix to a head yawed `degrees` to the
// subject's left (RotationFrom reads yaw from column 0 and 2, row 0 and 2:
// src/head_pose.cc).
void Yaw(Face& face, double degrees) {
  const double radians = degrees * std::numbers::pi / 180.0;
  const auto cosine = static_cast<float>(std::cos(radians));
  const auto sine = static_cast<float>(std::sin(radians));
  face.pose_column_major[0] = cosine;
  face.pose_column_major[2] = -sine;
  face.pose_column_major[5] = 1;
  face.pose_column_major[8] = sine;
  face.pose_column_major[10] = cosine;
  face.pose_column_major[15] = 1;
}

// Feeds an ExpressionReader frames at 15 per second through OnFace, and
// collects every reaction across all of them: the tracker's own mapping
// from scores to expressions is test/expressions_test.cc's job, this file's
// is turning what it reports into reactions, and deriving the pose it
// reports from the face.
class ExpressionReaderTest : public ::testing::Test {
 protected:
  // `seconds` of frames with every score in `raised` lifted by `rise` above
  // the resting face.
  void Hold(double seconds, std::initializer_list<int> raised = {},
            float rise = kSmall) {
    const int frames = static_cast<int>(seconds * kFramesPerSecond);
    for (int frame = 0; frame < frames; ++frame) {
      Frame(raised, rise);
    }
  }

  // One frame, `raised` lifted by `rise`, with the head looking ahead.
  void Frame(std::initializer_list<int> raised, float rise) {
    Face face{};
    LookAhead(face);
    for (const int index : raised) {
      face.blendshapes[index] += rise;
    }
    now_ms_ += kFrameMs;
    reader_.OnFace(face, /*width=*/640, /*height=*/480, now_ms_, reactions_);
  }

  // A resting face long enough to learn it.
  void Rest() { Hold(5); }

  std::int64_t now_ms_ = 1000;
  ExpressionReader reader_{ExpressionTracker::Settings{.rise = 0.05,
                                                       .pose_degrees = 10,
                                                       .pose_range = 3,
                                                       .settle_ms = 600,
                                                       .spreads = 3,
                                                       .hold_ms = 200}};
  std::vector<Reaction> reactions_;
};

TEST_F(ExpressionReaderTest, ReportsNothingForARestingFace) {
  Hold(30);

  EXPECT_THAT(reactions_, IsEmpty());
}

TEST_F(ExpressionReaderTest, ReportsAReactionNamedByTheExpressionThatStarted) {
  Rest();

  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reactions_, ElementsAre(R"({"expression":"smile"})"));
}

TEST_F(ExpressionReaderTest, ReportsOneReactionPerExpressionThatStarts) {
  Rest();

  Hold(1, {kBrowInnerUp, kBrowOuterUpLeft, kBrowOuterUpRight, kMouthSmileLeft,
           kMouthSmileRight});

  EXPECT_THAT(reactions_, ElementsAre(R"({"expression":"brows_up"})",
                                      R"({"expression":"smile"})"));
}

// The tracker itself starts learning the resting face again after a gap of
// more than two seconds (ExpressionTracker::Update); OnNoFace has nothing
// of its own to drop, so a face right after it is read normally, not as the
// start of a new resting face.
TEST_F(ExpressionReaderTest, OnNoFaceDropsNothingOfItsOwn) {
  Rest();

  reader_.OnNoFace();
  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reactions_, ElementsAre(R"({"expression":"smile"})"));
}

// The reader derives the pose from the face's transformation matrix the
// same way HeadPoseReader does (src/reaction.cc), and hands it to the
// tracker: a face turned well away from the resting pose gates the scores
// out, where a frontal one reports as usual.
TEST_F(ExpressionReaderTest, DerivesThePoseFromTheFaceAndGatesOnIt) {
  Rest();

  Face turned{};
  Yaw(turned, 25);
  for (const int index : {kMouthSmileLeft, kMouthSmileRight}) {
    turned.blendshapes[index] += kSmall;
  }
  for (int frame = 0; frame < 3 * kFramesPerSecond; ++frame) {
    now_ms_ += kFrameMs;
    reader_.OnFace(turned, /*width=*/640, /*height=*/480, now_ms_, reactions_);
  }

  EXPECT_THAT(reactions_, IsEmpty());

  // The settle time, plus the pose-range window flushing the turn away,
  // takes just over a second once the face looks ahead again.
  Hold(2, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reactions_, ElementsAre(R"({"expression":"smile"})"));
}

}  // namespace
}  // namespace facepalm
