#include "src/screen_context.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "src/head_pose.h"

namespace facepalm {
namespace {

constexpr GazeSettings kSettings = {.other_screen_degrees = 7.5,
                                    .away_degrees = 15};
constexpr HeadRotation kRest = {};

HeadRotation Yawed(double degrees) { return {.yaw_degrees = degrees}; }
HeadRotation Pitched(double degrees) { return {.pitch_degrees = degrees}; }

TEST(NameTest, NamesEachGaze) {
  EXPECT_EQ(Name(Gaze::kThisScreen), "this_screen");
  EXPECT_EQ(Name(Gaze::kOtherScreen), "other_screen");
  EXPECT_EQ(Name(Gaze::kAway), "away");
  EXPECT_EQ(Name(Gaze::kUnknown), "unknown");
}

TEST(BeatGazeNameTest, NamesEachZoneForABeatsOwnReply) {
  EXPECT_EQ(BeatGazeName(Gaze::kThisScreen), "on_content");
  EXPECT_EQ(BeatGazeName(Gaze::kOtherScreen), "averted");
  EXPECT_EQ(BeatGazeName(Gaze::kAway), "away");
  EXPECT_EQ(BeatGazeName(Gaze::kUnknown), "unknown");
}

TEST(BeatGazeNameTest, EachNameGivesItsZone) {
  EXPECT_EQ(BeatGazeNamed("on_content"), Gaze::kThisScreen);
  EXPECT_EQ(BeatGazeNamed("averted"), Gaze::kOtherScreen);
  EXPECT_EQ(BeatGazeNamed("away"), Gaze::kAway);
  EXPECT_EQ(BeatGazeNamed("unknown"), Gaze::kUnknown);
}

TEST(BeatGazeNameTest, NoOtherNameIsAZone) {
  EXPECT_EQ(BeatGazeNamed("this_screen"), std::nullopt);
  EXPECT_EQ(BeatGazeNamed("On_content"), std::nullopt);
  EXPECT_EQ(BeatGazeNamed(""), std::nullopt);
}

TEST(NameTest, NamesEachContentHit) {
  EXPECT_EQ(Name(ContentHit::kYes), "yes");
  EXPECT_EQ(Name(ContentHit::kNo), "no");
  EXPECT_EQ(Name(ContentHit::kUnknown), "unknown");
}

TEST(SideOfTest, NoScreensOrOneIsNeitherSide) {
  const std::vector<ScreenRect> none;
  EXPECT_FALSE(SideOf(none).left);
  EXPECT_FALSE(SideOf(none).right);
  const std::vector<ScreenRect> one = {
      {.x = 0, .y = 0, .width = 1000, .height = 800, .main = true}};
  EXPECT_FALSE(SideOf(one).left);
  EXPECT_FALSE(SideOf(one).right);
}

TEST(SideOfTest, NoMainScreenIsNeitherSide) {
  const std::vector<ScreenRect> screens = {
      {.x = 0, .y = 0, .width = 1000, .height = 800},
      {.x = -1000, .y = 0, .width = 1000, .height = 800}};
  EXPECT_FALSE(SideOf(screens).left);
  EXPECT_FALSE(SideOf(screens).right);
}

TEST(SideOfTest, AScreenEntirelyLeftOfMainIsTheLeftSide) {
  const std::vector<ScreenRect> screens = {
      {.x = 0, .y = 0, .width = 1000, .height = 800, .main = true},
      {.x = -1000, .y = 0, .width = 1000, .height = 800}};
  const ScreenSide side = SideOf(screens);
  EXPECT_TRUE(side.left);
  EXPECT_FALSE(side.right);
}

TEST(SideOfTest, AScreenEntirelyRightOfMainIsTheRightSide) {
  const std::vector<ScreenRect> screens = {
      {.x = 0, .y = 0, .width = 1000, .height = 800, .main = true},
      {.x = 1000, .y = 0, .width = 1200, .height = 800}};
  const ScreenSide side = SideOf(screens);
  EXPECT_FALSE(side.left);
  EXPECT_TRUE(side.right);
}

TEST(SideOfTest, ScreensOnBothSidesAreBothSides) {
  // Two screens on each side, so a side already found true on an earlier
  // screen short-circuits on a later one, rather than re-checking it.
  const std::vector<ScreenRect> screens = {
      {.x = -1000, .y = 0, .width = 1000, .height = 800},
      {.x = -2000, .y = 0, .width = 1000, .height = 800},
      {.x = 0, .y = 0, .width = 1000, .height = 800, .main = true},
      {.x = 1000, .y = 0, .width = 1200, .height = 800},
      {.x = 2200, .y = 0, .width = 1200, .height = 800}};
  const ScreenSide side = SideOf(screens);
  EXPECT_TRUE(side.left);
  EXPECT_TRUE(side.right);
}

TEST(SideOfTest, AScreenOverlappingMainIsNeitherSide) {
  // Stacked above/below, or a mirrored duplicate: not left or right of it.
  const std::vector<ScreenRect> screens = {
      {.x = 0, .y = 0, .width = 1000, .height = 800, .main = true},
      {.x = 200, .y = 800, .width = 1000, .height = 800}};
  const ScreenSide side = SideOf(screens);
  EXPECT_FALSE(side.left);
  EXPECT_FALSE(side.right);
}

TEST(CoarseGazeFromTest, NoRestingPoseIsUnknown) {
  EXPECT_EQ(CoarseGazeFrom(Yawed(30), std::nullopt, {}, kSettings),
            Gaze::kUnknown);
}

TEST(CoarseGazeFromTest, AtRestIsThisScreen) {
  EXPECT_EQ(CoarseGazeFrom(kRest, kRest, {}, kSettings), Gaze::kThisScreen);
  EXPECT_EQ(CoarseGazeFrom(Yawed(7.5), kRest, {}, kSettings),
            Gaze::kThisScreen);
}

TEST(CoarseGazeFromTest, PastTheAwayDegreesIsAway) {
  EXPECT_EQ(CoarseGazeFrom(Yawed(15.1), kRest, {}, kSettings), Gaze::kAway);
  EXPECT_EQ(CoarseGazeFrom(Pitched(-15.1), kRest, {}, kSettings), Gaze::kAway);
}

TEST(CoarseGazeFromTest, MeasuredFromTheRestingPoseNotZero) {
  const HeadRotation resting = {.yaw_degrees = 30};
  EXPECT_EQ(CoarseGazeFrom(Yawed(32), resting, {}, kSettings),
            Gaze::kThisScreen);
  EXPECT_EQ(CoarseGazeFrom(Yawed(50), resting, {}, kSettings), Gaze::kAway);
}

TEST(CoarseGazeFromTest, YawWrapsAcrossTheWrapAt180Degrees) {
  const HeadRotation resting = {.yaw_degrees = 175};
  // -175 is 10 degrees from 175, not 350: between the two thresholds.
  EXPECT_NE(CoarseGazeFrom(Yawed(-175), resting, {}, kSettings),
            Gaze::kThisScreen);
  EXPECT_NE(CoarseGazeFrom(Yawed(-175), resting, {}, kSettings), Gaze::kAway);
}

TEST(CoarseGazeFromTest, BetweenTheThresholdsWithAScreenThatWayIsThatScreen) {
  // Positive yaw is a turn to the subject's left (src/reaction.h).
  EXPECT_EQ(CoarseGazeFrom(Yawed(10), kRest, {.left = true}, kSettings),
            Gaze::kOtherScreen);
  EXPECT_EQ(CoarseGazeFrom(Yawed(-10), kRest, {.right = true}, kSettings),
            Gaze::kOtherScreen);
}

TEST(CoarseGazeFromTest, BetweenTheThresholdsWithNoScreenThatWayIsUnknown) {
  EXPECT_EQ(CoarseGazeFrom(Yawed(10), kRest, {}, kSettings), Gaze::kUnknown);
  // A screen on the wrong side does not count.
  EXPECT_EQ(CoarseGazeFrom(Yawed(10), kRest, {.right = true}, kSettings),
            Gaze::kUnknown);
  EXPECT_EQ(CoarseGazeFrom(Yawed(-10), kRest, {.left = true}, kSettings),
            Gaze::kUnknown);
}

TEST(CoarseGazeFromTest, PitchAloneBetweenTheThresholdsIsUnknown) {
  // Pitch cannot point at a second screen in this model.
  EXPECT_EQ(CoarseGazeFrom(Pitched(-10), kRest, {.left = true, .right = true},
                           kSettings),
            Gaze::kUnknown);
}

constexpr ScreenRect kMain = {
    .x = 0, .y = 0, .width = 1000, .height = 800, .main = true};
constexpr ContentRect kCentre = {
    .x = 400, .y = 300, .width = 200, .height = 200};
constexpr ContentRect kTopLeftCorner = {
    .x = 0, .y = 0, .width = 100, .height = 100};

TEST(ContentHitFromTest, NoRestingPoseIsUnknown) {
  EXPECT_EQ(ContentHitFrom(kRest, std::nullopt, kMain, kCentre, kSettings),
            ContentHit::kUnknown);
}

TEST(ContentHitFromTest, PastTheConeIsUnknown) {
  EXPECT_EQ(ContentHitFrom(Yawed(10), kRest, kMain, kCentre, kSettings),
            ContentHit::kUnknown);
  EXPECT_EQ(ContentHitFrom(Pitched(-15.1), kRest, kMain, kCentre, kSettings),
            ContentHit::kUnknown);
}

TEST(ContentHitFromTest, RestingGazeIsInsideAContentCoveringTheCentre) {
  EXPECT_EQ(ContentHitFrom(kRest, kRest, kMain, kCentre, kSettings),
            ContentHit::kYes);
}

TEST(ContentHitFromTest, RestingGazeIsOutsideAContentAtTheCorner) {
  EXPECT_EQ(ContentHitFrom(kRest, kRest, kMain, kTopLeftCorner, kSettings),
            ContentHit::kNo);
}

// A content rectangle that lines up on x but sits above or below the
// resting gaze's own y, isolating each y bound of "inside" from the x
// check RestingGazeIsOutsideAContentAtTheCorner above already fails on.
TEST(ContentHitFromTest, RestingGazeIsOutsideAContentBelowIt) {
  constexpr ContentRect kBottomStrip = {
      .x = 400, .y = 700, .width = 200, .height = 50};
  EXPECT_EQ(ContentHitFrom(kRest, kRest, kMain, kBottomStrip, kSettings),
            ContentHit::kNo);
}

TEST(ContentHitFromTest, RestingGazeIsOutsideAContentAboveIt) {
  constexpr ContentRect kTopStrip = {
      .x = 400, .y = 0, .width = 200, .height = 50};
  EXPECT_EQ(ContentHitFrom(kRest, kRest, kMain, kTopStrip, kSettings),
            ContentHit::kNo);
}

TEST(ContentHitFromTest, MeasuredFromTheRestingPoseNotZero) {
  const HeadRotation resting = {.yaw_degrees = 30};
  EXPECT_EQ(ContentHitFrom(Yawed(30), resting, kMain, kCentre, kSettings),
            ContentHit::kYes);
}

TEST(ContentHitFromTest, PositiveYawMovesTowardTheScreensOwnLeft) {
  // Positive yaw is a turn to the subject's left (src/reaction.h), which
  // is the screen's own left: the lower end of its x.
  constexpr ContentRect kLeftEdge = {
      .x = 0, .y = 0, .width = 50, .height = 800};
  constexpr ContentRect kRightEdge = {
      .x = 950, .y = 0, .width = 50, .height = 800};
  EXPECT_EQ(ContentHitFrom(Yawed(7.5), kRest, kMain, kLeftEdge, kSettings),
            ContentHit::kYes);
  EXPECT_EQ(ContentHitFrom(Yawed(7.5), kRest, kMain, kRightEdge, kSettings),
            ContentHit::kNo);
}

TEST(ContentHitFromTest, NegativeYawMovesTowardTheScreensOwnRight) {
  constexpr ContentRect kRightEdge = {
      .x = 950, .y = 0, .width = 50, .height = 800};
  EXPECT_EQ(ContentHitFrom(Yawed(-7.5), kRest, kMain, kRightEdge, kSettings),
            ContentHit::kYes);
}

TEST(ContentHitFromTest, PositivePitchMovesTowardTheScreensOwnBottom) {
  // Positive pitch is looking down (src/eye_gaze.h).
  constexpr ContentRect kBottomEdge = {
      .x = 0, .y = 750, .width = 1000, .height = 50};
  EXPECT_EQ(ContentHitFrom(Pitched(7.5), kRest, kMain, kBottomEdge, kSettings),
            ContentHit::kYes);
}

// MacosScreenSource (src/macos/screen_context_macos.mm) is the only
// caller, flipping Cocoa's y-up frames and NSEvent's y-up mouse location
// into ScreenRect and MousePosition's own top-left-origin, y-down space
// (#168, #169). Each case below is a monitor arrangement a real desktop
// can have, named by where that screen sits relative to the primary one
// the flip is against.
TEST(YDownFromYUpTest, ThePrimaryScreenItselfFlipsToItsOwnTopAtZero) {
  EXPECT_DOUBLE_EQ(
      YDownFromYUp(/*y=*/0, /*height=*/800, /*primary_height=*/800), 0);
}

TEST(YDownFromYUpTest, APointOnThePrimaryFlipsTheSameWayWithNoHeight) {
  // NSEvent's mouse location has no height: a point 300 up from the
  // primary's own bottom is 500 down from its top, 800 tall.
  EXPECT_DOUBLE_EQ(
      YDownFromYUp(/*y=*/300, /*height=*/0, /*primary_height=*/800), 500);
}

TEST(YDownFromYUpTest, AShorterScreenLeftOfThePrimarySitsLowerInYDownSpace) {
  // Same y-up origin as the primary (bottom-aligned), but only 600 tall:
  // its own top sits 200 below the primary's, in y-down space. x is not
  // this function's concern -- left of the primary differs only in x,
  // which the caller carries over unflipped.
  EXPECT_DOUBLE_EQ(
      YDownFromYUp(/*y=*/0, /*height=*/600, /*primary_height=*/800), 200);
}

TEST(YDownFromYUpTest, AScreenAboveThePrimaryGetsANegativeY) {
  // Its own bottom edge touches the primary's top (y-up y = 800), 600
  // tall: it sits entirely above y-down's own origin.
  EXPECT_DOUBLE_EQ(
      YDownFromYUp(/*y=*/800, /*height=*/600, /*primary_height=*/800), -600);
}

TEST(YDownFromYUpTest, AScreenBelowThePrimaryStartsAtThePrimarysOwnHeight) {
  // Its own top edge touches the primary's bottom (y-up y + height = 0):
  // its y-down top sits right where the primary's own bottom does.
  EXPECT_DOUBLE_EQ(
      YDownFromYUp(/*y=*/-600, /*height=*/600, /*primary_height=*/800), 800);
}

}  // namespace
}  // namespace facepalm
