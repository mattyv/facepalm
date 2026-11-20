#include "src/expressions.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "src/head_pose.h"

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
// expression it reports. `pose_` is the head pose on every frame until a
// test changes it; it starts at the tracker's resting pose (all zero), so a
// test that never touches it sees the gate exactly as if it did not exist.
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

  // One frame, `raised` lifted by `rise`, `frame_ms_` after the last, with
  // the head at `pose_`.
  void Frame(std::initializer_list<int> raised, float rise) {
    Scores scores = rest_;
    for (const int index : raised) {
      scores.at(static_cast<std::size_t>(index)) += rise;
    }
    now_ms_ += frame_ms_;
    for (const Expression started : tracker_.Update(scores, pose_, now_ms_)) {
      reported_.push_back(started);
    }
  }

  // A resting face long enough to learn it.
  void Rest() { Hold(5); }

  Scores rest_{};
  HeadRotation pose_{};
  std::int64_t now_ms_ = 1000;
  std::int64_t frame_ms_ = kFrameMs;
  ExpressionTracker tracker_{ExpressionTracker::Settings{.rise = 0.05,
                                                         .pose_degrees = 10,
                                                         .pose_range = 3,
                                                         .settle_ms = 600,
                                                         .spreads = 3,
                                                         .hold_ms = 200}};
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

// Head pose gate (#46): a nod, a shake or a tilt moves the blendshape scores
// as much as an expression does, so the tracker pauses while the head is
// away from its resting pose or moving.

// A head held well away from its resting pose reports nothing, though the
// scores rise the whole time.
TEST_F(ExpressionsTest, AHeadHeldAwayFromItsRestingPoseReportsNothing) {
  Rest();
  pose_.pitch_degrees = -25;

  Hold(3, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, IsEmpty());
}

// A nodding head oscillates within the pose-range window without ever
// deviating far from the resting pose on its own; the range check alone
// gates it out.
TEST_F(ExpressionsTest, ANoddingHeadReportsNothingThoughTheScoresRise) {
  Rest();

  constexpr int kNodFrames = 2 * kFramesPerSecond;
  for (int frame = 0; frame < kNodFrames; ++frame) {
    pose_.pitch_degrees = frame % 2 == 0 ? -4 : 4;
    Frame({kMouthSmileLeft, kMouthSmileRight}, kSmall);
  }

  EXPECT_THAT(reported_, IsEmpty());
}

// The gate stays shut for the settle time after the head last moved (plus
// the time the pose-range window takes to flush the movement out), then
// opens, and a rise already under way is reported once the hold catches up.
TEST_F(ExpressionsTest, TheGateStaysShutForTheSettleTimeThenOpens) {
  Rest();
  pose_.pitch_degrees = -25;
  Hold(1);
  pose_.pitch_degrees = 0;
  // The pose-range window takes about half a second to flush the turn
  // away; the settle time then keeps the gate shut a further 600 ms. A
  // second is comfortably inside that, and comfortably past the roughly
  // 800 ms a report would need if the settle time did not apply.
  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, IsEmpty());

  Hold(1.2, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// While the head is away, the levels and the spreads are frozen: a face
// held smiling hard throughout a nod does not drag the baseline up with it,
// so a small rise right after the head settles is still judged against the
// old resting face and counts.
TEST_F(ExpressionsTest, LevelsAndSpreadDoNotLearnWhileTheHeadIsAway) {
  Rest();
  pose_.pitch_degrees = -25;
  Hold(5, {kMouthSmileLeft, kMouthSmileRight}, 0.3F);
  pose_.pitch_degrees = 0;

  Hold(2, {kMouthSmileLeft, kMouthSmileRight}, 0.06F);

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// A rise that straddles a head movement restarts its hold: the partial
// hold from before the movement does not combine with time after the gate
// reopens to report sooner than a fresh hold would.
TEST_F(ExpressionsTest, ARiseThatStraddlesMovementRestartsItsHold) {
  Rest();
  Hold(0.15, {kMouthSmileLeft, kMouthSmileRight});
  pose_.pitch_degrees = -25;
  Hold(1, {kMouthSmileLeft, kMouthSmileRight});
  pose_.pitch_degrees = 0;
  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, IsEmpty());

  Hold(1.2, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// A new posture held for long enough becomes the resting pose itself, and
// expressions are reported again from there.
TEST_F(ExpressionsTest, ANewPostureHeldForALongTimeBecomesTheRestingPose) {
  Rest();
  pose_.pitch_degrees = -25;
  Hold(120);

  EXPECT_THAT(reported_, IsEmpty());

  Hold(1, {kMouthSmileLeft, kMouthSmileRight});

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// At one frame a second the inter-frame gap (1000 ms) already exceeds the
// settle time (600 ms), so a single away frame gates out only that frame;
// the very next frame, a second later, already reports.
TEST_F(ExpressionsTest,
       GatesForTheSettleTimeNotAFixedFrameCountAtOneFramePerSecond) {
  frame_ms_ = 1000;
  for (int frame = 0; frame < 6; ++frame) {
    Frame({}, 0);
  }

  pose_.pitch_degrees = -25;
  Frame({kMouthSmileLeft, kMouthSmileRight}, kSmall);
  pose_.pitch_degrees = 0;
  for (int frame = 0; frame < 3; ++frame) {
    Frame({kMouthSmileLeft, kMouthSmileRight}, kSmall);
  }

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
}

// At sixty frames a second, the same settle time spans many frames: still
// gated at 640 ms after the movement, open and reported well past the
// roughly 1.1 second the window and the settle time together take.
TEST_F(ExpressionsTest,
       GatesForTheSettleTimeNotAFixedFrameCountAtSixtyFramesPerSecond) {
  frame_ms_ = 16;
  constexpr int kRestFrames = 300;  // 4.8 seconds: enough to learn.
  for (int frame = 0; frame < kRestFrames; ++frame) {
    Frame({}, 0);
  }

  pose_.pitch_degrees = -25;
  Frame({kMouthSmileLeft, kMouthSmileRight}, kSmall);
  pose_.pitch_degrees = 0;
  constexpr int kStillGatedFrames = 40;  // About 640 ms.
  for (int frame = 0; frame < kStillGatedFrames; ++frame) {
    Frame({kMouthSmileLeft, kMouthSmileRight}, kSmall);
  }

  EXPECT_THAT(reported_, IsEmpty());

  constexpr int kAfterGateFrames = 60;  // About 960 ms more.
  for (int frame = 0; frame < kAfterGateFrames; ++frame) {
    Frame({kMouthSmileLeft, kMouthSmileRight}, kSmall);
  }

  EXPECT_THAT(reported_, ElementsAre(Expression::kSmile));
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
