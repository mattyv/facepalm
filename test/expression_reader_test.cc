#include "src/expression_reader.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
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

// Feeds an ExpressionReader frames at 15 per second through OnFace, and
// collects every reaction across all of them: the tracker's own mapping
// from scores to expressions is test/expressions_test.cc's job, this file's
// is turning what it reports into reactions.
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

  // One frame, `raised` lifted by `rise`.
  void Frame(std::initializer_list<int> raised, float rise) {
    Face face{};
    for (const int index : raised) {
      face.blendshapes[index] += rise;
    }
    now_ms_ += kFrameMs;
    reader_.OnFace(face, /*width=*/640, /*height=*/480, now_ms_, reactions_);
  }

  // A resting face long enough to learn it.
  void Rest() { Hold(5); }

  std::int64_t now_ms_ = 1000;
  ExpressionReader reader_{
      ExpressionTracker::Settings{.rise = 0.05, .spreads = 3, .hold_ms = 200}};
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

}  // namespace
}  // namespace facepalm
