#include "src/expressions.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr std::int64_t kFrameMs = 66;  // 15 frames per second.
constexpr int kFramesPerSecond = 15;
constexpr float kSmall = 0.2F;  // A small but clear expression's rise.

// MediaPipe's indices for the scores the tests move.
constexpr int kBrowDownLeft = 1;
constexpr int kBrowDownRight = 2;
constexpr int kBrowInnerUp = 3;
constexpr int kBrowOuterUpLeft = 4;
constexpr int kBrowOuterUpRight = 5;
constexpr int kEyeSquintLeft = 19;
constexpr int kEyeSquintRight = 20;
constexpr int kMouthShrugLower = 42;
constexpr int kMouthSmileLeft = 44;
constexpr int kMouthSmileRight = 45;

// Feeds an ExpressionTracker frames at 15 per second, and collects every
// expression it reports.
class ExpressionsTest : public ::testing::Test {
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

  // One frame, `raised` lifted by `rise`, `frame_ms_` after the last.
  void Frame(std::initializer_list<int> raised, float rise) {
    Scores scores = rest_;
    for (const int index : raised) {
      scores.at(static_cast<std::size_t>(index)) += rise;
    }
    now_ms_ += frame_ms_;
    for (const Expression started : tracker_.Update(scores, now_ms_)) {
      reported_.push_back(started);
    }
  }

  // A resting face long enough to learn it.
  void Rest() { Hold(5); }

  Scores rest_{};
  std::int64_t now_ms_ = 1000;
  std::int64_t frame_ms_ = kFrameMs;
  ExpressionTracker tracker_{
      ExpressionTracker::Settings{.rise = 0.05, .spreads = 3, .hold_ms = 200}};
  std::vector<Expression> reported_;
};

TEST_F(ExpressionsTest, ReportsNothingForARestingFace) {
  Hold(30);

  EXPECT_THAT(reported_, IsEmpty());
}

TEST_F(ExpressionsTest, ReportsAnExpressionOnceWhenItStarts) {
  Rest();

  Hold(3, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

TEST_F(ExpressionsTest, ReportsItAgainOnceItHasEnded) {
  Rest();

  Hold(1, {kMouthSmileLeft, kMouthSmileRight});
  Hold(1);
  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile, Expression::kSmile));
}

TEST_F(ExpressionsTest, ReportsNothingWhileItLearnsTheRestingFace) {
  Hold(1);

  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, IsEmpty());
}

// A resting face has scores of its own: a mouth that turns up, eyes that
// sit narrow. They are where it starts from, not a rise to settle from.
TEST_F(ExpressionsTest, StartsFromTheScoresOfTheFirstFrame) {
  rest_.at(kMouthSmileLeft) = 0.4F;
  rest_.at(kMouthSmileRight) = 0.4F;
  Hold(3.2);

  Hold(1, {kMouthSmileLeft, kMouthSmileRight}, 0.1F);

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// The first frames may not be the resting face: someone sits down smiling.
TEST_F(ExpressionsTest, SettlesOnTheRestingFaceWhileItLearns) {
  Hold(1, {kMouthSmileLeft, kMouthSmileRight});
  Hold(15);

  Hold(1, {kMouthSmileLeft, kMouthSmileRight}, 0.1F);

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// A face that settles somewhere new becomes the resting face, and a further
// rise from there is a new expression.
TEST_F(ExpressionsTest, TakesAnExpressionHeldForMinutesAsTheRestingFace) {
  Rest();

  Hold(120, {kMouthSmileLeft, kMouthSmileRight});
  Hold(1, {kMouthSmileLeft, kMouthSmileRight}, 0.4F);

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile, Expression::kSmile));
}

// An expression is not part of how far the resting face wobbles.
TEST_F(ExpressionsTest, AnExpressionDoesNotWidenTheSpread) {
  Rest();

  Hold(5, {kMouthSmileLeft, kMouthSmileRight});
  Hold(1);
  Hold(1, {kMouthSmileLeft, kMouthSmileRight}, 0.15F);

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile, Expression::kSmile));
}

TEST_F(ExpressionsTest, IgnoresTwoShortRisesApart) {
  Rest();

  Hold(0.2, {kMouthSmileLeft, kMouthSmileRight});
  Hold(1);
  Hold(0.2, {kMouthSmileLeft, kMouthSmileRight});
  Hold(1);

  EXPECT_THAT(reported_, IsEmpty());
}

// A blink lifts the squint scores for a few frames.
TEST_F(ExpressionsTest, IgnoresARiseShorterThanTheHold) {
  Rest();

  Hold(0.2, {kMouthSmileLeft, kMouthSmileRight});
  Hold(1);

  EXPECT_THAT(reported_, IsEmpty());
}

TEST_F(ExpressionsTest, IgnoresARiseSmallerThanTheSmallestThatCounts) {
  Rest();

  Hold(3, {kMouthSmileLeft, kMouthSmileRight}, 0.04F);

  EXPECT_THAT(reported_, IsEmpty());
}

TEST_F(ExpressionsTest, CountsARiseJustOverTheSmallestThatCounts) {
  Rest();

  Hold(3, {kMouthSmileLeft, kMouthSmileRight}, 0.06F);

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// A score that wobbles at rest needs a bigger rise: the same rise that
// counts on a still face is within this face's own spread.
TEST_F(ExpressionsTest, JudgesARiseAgainstThePersonsOwnSpread) {
  for (int frame = 0; frame < 10 * kFramesPerSecond; ++frame) {
    Frame({kMouthSmileLeft, kMouthSmileRight}, frame % 2 == 0 ? 0.1F : -0.1F);
  }

  Hold(3, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, IsEmpty());
}

// An expression that eases to just over half its threshold has not ended.
TEST_F(ExpressionsTest, DoesNotRepeatAnExpressionThatEasesAndReturns) {
  Rest();

  Hold(1, {kMouthSmileLeft, kMouthSmileRight});
  Hold(1, {kMouthSmileLeft, kMouthSmileRight}, 0.04F);
  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// The resting face moves over minutes: a different seat, different light.
TEST_F(ExpressionsTest, FollowsASlowDriftWithoutReporting) {
  Rest();

  constexpr int kFrames = 120 * kFramesPerSecond;
  for (int frame = 0; frame < kFrames; ++frame) {
    Frame({kMouthSmileLeft, kMouthSmileRight},
          0.3F * static_cast<float>(frame) / kFrames);
  }

  EXPECT_THAT(reported_, IsEmpty());
}

// After the face has been away, what it comes back with is the new resting
// face, not an expression.
TEST_F(ExpressionsTest, LearnsTheRestingFaceAgainAfterAGap) {
  Rest();
  now_ms_ += 5000;

  Hold(2, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, IsEmpty());
}

// A gap of exactly two seconds is not "more than two": the resting face is
// kept, and a rise right after the gap is an expression, not a new face.
TEST_F(ExpressionsTest, KeepsTheRestingFaceOverAGapOfTwoSeconds) {
  Rest();
  now_ms_ += 2000 - kFrameMs;

  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// One frame a second stays within the two-second gap, and the hold is
// time, not frames: the second raised frame is already a second old.
TEST_F(ExpressionsTest, ReportsAtOneFrameEachSecond) {
  frame_ms_ = 1000;
  for (int frame = 0; frame < 6; ++frame) {
    Frame({}, 0);
  }

  for (int frame = 0; frame < 3; ++frame) {
    Frame({kMouthSmileLeft, kMouthSmileRight}, kSmall);
  }

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// At sixty frames a second a blink is several frames of squint; the hold
// being time, not frames, is what keeps it out, while a real squint counts.
TEST_F(ExpressionsTest, IgnoresABlinkAtSixtyFramesPerSecondButNotASquint) {
  frame_ms_ = 16;
  constexpr int kRestFrames = 300;  // 4.8 seconds: enough to learn.
  for (int frame = 0; frame < kRestFrames; ++frame) {
    Frame({}, 0);
  }

  constexpr int kBlinkFrames = 6;  // About 100 ms, under the 200 ms hold.
  for (int frame = 0; frame < kBlinkFrames; ++frame) {
    Frame({kEyeSquintLeft, kEyeSquintRight}, kSmall);
  }
  constexpr int kBetweenFrames = 60;
  for (int frame = 0; frame < kBetweenFrames; ++frame) {
    Frame({}, 0);
  }
  constexpr int kSquintFrames = 31;  // Half a second, over the hold.
  for (int frame = 0; frame < kSquintFrames; ++frame) {
    Frame({kEyeSquintLeft, kEyeSquintRight}, kSmall);
  }

  EXPECT_THAT(reported_, ElementsAre(Expression::kEyesNarrowed));
}

TEST_F(ExpressionsTest, ReadsRaisedBrowsFromTheBrowUpScores) {
  Rest();

  Hold(1, {kBrowInnerUp, kBrowOuterUpLeft, kBrowOuterUpRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kBrowsUp));
}

TEST_F(ExpressionsTest, ReadsPressedLipsFromTheLowerLipShrugScore) {
  Rest();

  Hold(1, {kMouthShrugLower});

  EXPECT_THAT(reported_, ElementsAre(Expression::kLipsPressed));
}

// A small frown lifts the brow-down scores by about 0.1 and leaves the
// squint scores where they were (#46).
TEST_F(ExpressionsTest, ReadsLoweredBrowsFromTheBrowDownScoresAlone) {
  Rest();

  Hold(1, {kBrowDownLeft, kBrowDownRight}, 0.1F);

  EXPECT_THAT(reported_, ElementsAre(Expression::kBrowsDown));
}

TEST_F(ExpressionsTest, ReadsNarrowedEyesFromTheSquintScores) {
  Rest();

  Hold(1, {kEyeSquintLeft, kEyeSquintRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kEyesNarrowed));
}

TEST_F(ExpressionsTest, ReportsExpressionsThatStartTogether) {
  Rest();

  Hold(1, {kBrowInnerUp, kBrowOuterUpLeft, kBrowOuterUpRight, kMouthSmileLeft,
           kMouthSmileRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kBrowsUp, Expression::kSmile));
}

TEST(ExpressionNameTest, NamesEachExpression) {
  EXPECT_EQ(Name(Expression::kBrowsUp), "brows_up");
  EXPECT_EQ(Name(Expression::kSmile), "smile");
  EXPECT_EQ(Name(Expression::kLipsPressed), "lips_pressed");
  EXPECT_EQ(Name(Expression::kBrowsDown), "brows_down");
  EXPECT_EQ(Name(Expression::kEyesNarrowed), "eyes_narrowed");
}

}  // namespace
}  // namespace facepalm
