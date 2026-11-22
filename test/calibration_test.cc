#include "src/calibration.h"

#include <atomic>
#include <optional>
#include <string>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/eye_gaze.h"
#include "src/head_pose.h"

// Each test but the naming and nearest-screen ones stands for the property
// of specs/Calibration.tla it is named for.

// GoogleTest assertions check each optional before access.
// NOLINTBEGIN(bugprone-unchecked-optional-access)
namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;

HeadRotation Pose(double yaw, double pitch) {
  return {.yaw_degrees = yaw, .pitch_degrees = pitch};
}

EyeSignal Eye(double horizontal, double vertical, bool blink = false) {
  return {.horizontal = horizontal, .vertical = vertical, .blink = blink};
}

class MockCalibrationStore : public CalibrationStore {
 public:
  MOCK_METHOD(bool, Save,
              (const CalibrationProfile& profile, std::string& error),
              (override));
};

class CalibrationSessionTest : public ::testing::Test {
 protected:
  // Feeds a gains step that always passes with the default Settings
  // (min_head_travel_degrees 10, min_axis_degrees 4): a real fixate-and-
  // turn, 20 degrees of yaw with the eyes compensating at a gain of 20.
  // Returns the status the daemon would reply with for the first screen's
  // centre step. ASSERT_* inside a non-void helper needs its caller to
  // check HasFailure, as every call site here does.
  std::optional<CalibrationController::Status> PassGainsStep(
      std::string& error) {
    for (int step = -10; step <= 10; step += 2) {
      session_.Update(Pose(step, 0), Eye(-step / 20.0, 0));
    }
    return session_.Next(error);
  }

  NiceMock<MockCalibrationStore> store_;
  CalibrationSession session_{store_};
};

TEST(NamingTest, NamesEveryPoint) {
  EXPECT_THAT(Name(CalibrationPoint::kCentre), Eq("centre"));
  EXPECT_THAT(Name(CalibrationPoint::kTopLeft), Eq("top_left"));
  EXPECT_THAT(Name(CalibrationPoint::kTopRight), Eq("top_right"));
  EXPECT_THAT(Name(CalibrationPoint::kBottomLeft), Eq("bottom_left"));
  EXPECT_THAT(Name(CalibrationPoint::kBottomRight), Eq("bottom_right"));
}

TEST(NamingTest, NamesRoundTrip) {
  for (const CalibrationPoint point : kCalibrationPoints) {
    EXPECT_THAT(CalibrationPointNamed(Name(point)), Eq(point));
  }
}

TEST(NamingTest, RefusesAnUnknownName) {
  EXPECT_THAT(CalibrationPointNamed("middle"), Eq(std::nullopt));
}

using StartTest = CalibrationSessionTest;

TEST_F(StartTest, StartsAtTheRestingStep) {
  std::string error;
  const std::optional<CalibrationController::Status> status =
      session_.Start(1, error);

  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->running);
  EXPECT_FALSE(status->done);
  EXPECT_THAT(status->index, Eq(0));
  EXPECT_THAT(status->total, Eq(7));  // Resting, gains, plus 5 points.
  EXPECT_TRUE(status->resting_step);
  EXPECT_FALSE(status->gains_step);
  EXPECT_THAT(error, IsEmpty());
}

TEST_F(StartTest, MovesToTheGainsStepAfterResting) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});

  std::string error;
  const std::optional<CalibrationController::Status> status =
      session_.Next(error);
  ASSERT_TRUE(status.has_value()) << error;
  EXPECT_FALSE(status->resting_step);
  EXPECT_TRUE(status->gains_step);
  EXPECT_THAT(status->index, Eq(1));
}

TEST_F(StartTest, CountsEveryScreensTargets) {
  std::string error;
  const std::optional<CalibrationController::Status> status =
      session_.Start(2, error);

  ASSERT_TRUE(status.has_value());
  EXPECT_THAT(status->total, Eq(12));  // Resting, gains, plus 2*5 points.
}

TEST_F(StartTest, RefusesTooFewOrTooManyScreens) {
  std::string low_error;
  EXPECT_THAT(session_.Start(0, low_error), Eq(std::nullopt));
  EXPECT_THAT(low_error, Eq("calibration takes 1 to 8 screens"));

  std::string high_error;
  EXPECT_THAT(session_.Start(9, high_error), Eq(std::nullopt));
  EXPECT_THAT(high_error, Eq("calibration takes 1 to 8 screens"));
}

TEST_F(StartTest, RefusesToStartTwice) {
  std::string first_error;
  ASSERT_TRUE(session_.Start(1, first_error).has_value());

  std::string error;
  EXPECT_THAT(session_.Start(1, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("a calibration session is already running"));
}

using NoAdvanceWithoutCaptureTest = CalibrationSessionTest;

TEST_F(NoAdvanceWithoutCaptureTest, RefusesNextBeforeAnyPose) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());

  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("no pose captured yet for this step"));
}

TEST_F(NoAdvanceWithoutCaptureTest, EachStepNeedsItsOwnCapture) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string first_next_error;
  ASSERT_TRUE(session_.Next(first_next_error).has_value());

  // The next step (gains) has not been captured, even though resting was.
  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("no pose captured yet for this step"));
}

TEST_F(NoAdvanceWithoutCaptureTest, RefusesNextWithNoSessionRunning) {
  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("no calibration session is running"));
}

using NoSaveWithWeakStepTest = CalibrationSessionTest;

TEST_F(NoSaveWithWeakStepTest, RefusesAGainsStepWithTooLittleHeadTravel) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string resting_error;
  ASSERT_TRUE(session_.Next(resting_error).has_value());

  // Barely any head travel: well under the default min_head_travel_degrees.
  session_.Update(Pose(0, 0), Eye(0, 0));
  session_.Update(Pose(0.5, 0), Eye(-0.01, 0));

  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error,
              Eq("turn your head further while you keep looking at the dot"));
}

TEST_F(NoSaveWithWeakStepTest, ARefusedGainsStepCanBeRetried) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string resting_error;
  ASSERT_TRUE(session_.Next(resting_error).has_value());
  session_.Update(Pose(0, 0), Eye(0, 0));
  std::string weak_error;
  ASSERT_THAT(session_.Next(weak_error), Eq(std::nullopt));

  std::string error;
  const std::optional<CalibrationController::Status> status =
      PassGainsStep(error);
  ASSERT_TRUE(status.has_value()) << error;
  EXPECT_FALSE(status->gains_step);
  EXPECT_THAT(status->screen, Eq(0));
  EXPECT_THAT(status->point, Eq(CalibrationPoint::kCentre));
}

TEST_F(NoSaveWithWeakStepTest, RefusesACornerTooCloseToItsOwnCentre) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string resting_error;
  ASSERT_TRUE(session_.Next(resting_error).has_value());
  std::string gains_error;
  ASSERT_TRUE(PassGainsStep(gains_error).has_value()) << gains_error;
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string centre_error;
  ASSERT_TRUE(session_.Next(centre_error).has_value()) << centre_error;

  // The corner's gaze barely differs from the centre just stored: under
  // the default min_spread_degrees.
  session_.Update(Pose(0.5, 0), EyeSignal{});

  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error,
              Eq("look further toward that corner, away from the centre"));
}

TEST_F(NoSaveWithWeakStepTest, ARefusedCornerCanBeRetried) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string resting_error;
  ASSERT_TRUE(session_.Next(resting_error).has_value());
  std::string gains_error;
  ASSERT_TRUE(PassGainsStep(gains_error).has_value()) << gains_error;
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string centre_error;
  ASSERT_TRUE(session_.Next(centre_error).has_value()) << centre_error;
  session_.Update(Pose(0.5, 0), EyeSignal{});
  std::string weak_error;
  ASSERT_THAT(session_.Next(weak_error), Eq(std::nullopt));

  session_.Update(Pose(20, 20), EyeSignal{});
  std::string error;
  const std::optional<CalibrationController::Status> status =
      session_.Next(error);
  ASSERT_TRUE(status.has_value()) << error;
  EXPECT_THAT(status->point, Eq(CalibrationPoint::kTopRight));
}

using CalibrationWalksEveryTargetTest = CalibrationSessionTest;

// Walks every one of a screen's five points in order.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest, WalksEachScreensFivePointsInOrder) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());

  session_.Update(Pose(1, -1), EyeSignal{});
  std::string resting_error;
  ASSERT_TRUE(session_.Next(resting_error).has_value());
  std::string gains_error;
  const auto after_gains = PassGainsStep(gains_error);
  ASSERT_TRUE(after_gains.has_value()) << gains_error;
  EXPECT_THAT(after_gains->index, Eq(2));
  EXPECT_THAT(after_gains->screen, Eq(0));
  EXPECT_THAT(after_gains->point, Eq(CalibrationPoint::kCentre));

  // Each capture (the loop's first is the centre) sits well clear of the
  // one before, so the spread check never refuses one (#137).
  double yaw = 10;
  for (const CalibrationPoint point :
       {CalibrationPoint::kTopLeft, CalibrationPoint::kTopRight,
        CalibrationPoint::kBottomLeft, CalibrationPoint::kBottomRight}) {
    session_.Update(Pose(yaw, yaw), EyeSignal{});
    yaw += 10;
    std::string error;
    const auto status = session_.Next(error);
    ASSERT_TRUE(status.has_value()) << error;
    if (point != CalibrationPoint::kBottomRight) {
      EXPECT_THAT(status->point, Eq(point));
    }
  }
}

// Walks every target so the averages and the saved screen's points are
// checked together.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest, SavesTheAveragePoseOfEachTarget) {
  CalibrationProfile captured;
  EXPECT_CALL(store_, Save)
      .WillOnce([&](const CalibrationProfile& profile, std::string&) {
        captured = profile;
        return true;
      });
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());

  session_.Update(Pose(2, -2), EyeSignal{});
  session_.Update(Pose(4, -4), EyeSignal{});  // Resting: averages to (3, -3).
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  ASSERT_TRUE(PassGainsStep(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40, 50}) {
    session_.Update(Pose(yaw, yaw), EyeSignal{});
    ASSERT_TRUE(session_.Next(error).has_value()) << error;
  }

  EXPECT_THAT(captured.resting.yaw_degrees, Eq(3));
  EXPECT_THAT(captured.resting.pitch_degrees, Eq(-3));
  ASSERT_THAT(captured.screens.size(), Eq(1));
  const ScreenCalibration& screen = captured.screens[0];
  EXPECT_THAT(screen.points[static_cast<std::size_t>(CalibrationPoint::kCentre)]
                  .yaw_degrees,
              Eq(10));
  EXPECT_THAT(
      screen.points[static_cast<std::size_t>(CalibrationPoint::kBottomRight)]
          .yaw_degrees,
      Eq(50));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest, SavesTheEstimatedGainsAndRestingEye) {
  CalibrationProfile captured;
  EXPECT_CALL(store_, Save)
      .WillOnce([&](const CalibrationProfile& profile, std::string&) {
        captured = profile;
        return true;
      });
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), Eye(0.2, 0));
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  ASSERT_TRUE(PassGainsStep(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40, 50}) {
    session_.Update(Pose(yaw, yaw), EyeSignal{});
    ASSERT_TRUE(session_.Next(error).has_value()) << error;
  }

  EXPECT_THAT(captured.resting_eye.horizontal, Eq(0.2));
  EXPECT_NEAR(captured.gains.horizontal, 20, 1e-9);
}

TEST_F(CalibrationWalksEveryTargetTest, BlinkFramesNeverJoinTheEyeAverage) {
  CalibrationProfile captured;
  EXPECT_CALL(store_, Save)
      .WillOnce([&](const CalibrationProfile& profile, std::string&) {
        captured = profile;
        return true;
      });
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), Eye(0.2, 0));
  // A blink, with an eye signal that would skew the average if it counted:
  // the head still updates (captured_ stays true from it alone), but the
  // eye signal does not.
  session_.Update(Pose(0, 0), Eye(0.9, 0, /*blink=*/true));
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  ASSERT_TRUE(PassGainsStep(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40, 50}) {
    session_.Update(Pose(yaw, yaw), EyeSignal{});
    ASSERT_TRUE(session_.Next(error).has_value()) << error;
  }

  EXPECT_THAT(captured.resting_eye.horizontal, Eq(0.2));
}

TEST_F(CalibrationWalksEveryTargetTest, BlinkFramesNeverJoinTheGainsStep) {
  CalibrationProfile captured;
  EXPECT_CALL(store_, Save)
      .WillOnce([&](const CalibrationProfile& profile, std::string&) {
        captured = profile;
        return true;
      });
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  // A blink mid-step, with a head and eye pair that would corrupt the
  // slope away from 20 if it counted: on the wrong side of the real
  // fixate-and-turn line entirely.
  session_.Update(Pose(100, 0), Eye(100, 0, /*blink=*/true));
  ASSERT_TRUE(PassGainsStep(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40, 50}) {
    session_.Update(Pose(yaw, yaw), EyeSignal{});
    ASSERT_TRUE(session_.Next(error).has_value()) << error;
  }

  EXPECT_NEAR(captured.gains.horizontal, 20, 1e-9);
}

// A step where every frame blinks captures nothing for the eye signal, so
// it falls back to a centred (0, 0) average rather than crashing or
// leaving a stale value (#137): one indivisible walk through a whole
// session, the same as the other full-session tests above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest,
       AllBlinkFramesLeaveTheRestingEyeCentred) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), Eye(0.9, 0.9, /*blink=*/true));
  CalibrationProfile captured;
  EXPECT_CALL(store_, Save)
      .WillOnce([&](const CalibrationProfile& profile, std::string&) {
        captured = profile;
        return true;
      });
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  ASSERT_TRUE(PassGainsStep(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40, 50}) {
    // Every point's own frame blinks too, so the composed gaze falls back
    // to head pose alone and the corner-vs-centre spread check still
    // passes on the head offset alone.
    session_.Update(Pose(yaw, yaw), Eye(0.9, 0.9, /*blink=*/true));
    ASSERT_TRUE(session_.Next(error).has_value()) << error;
  }

  EXPECT_THAT(captured.resting_eye.horizontal, Eq(0));
  EXPECT_THAT(captured.resting_eye.vertical, Eq(0));
}

// #137's own bound on the gains step's own sample buffer: a session
// walked well past kMaxGainsSamples still estimates gains correctly
// rather than growing its buffer without limit.
TEST_F(CalibrationWalksEveryTargetTest, TheGainsStepStopsBufferingPastItsCap) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  constexpr int kWellPastTheCap = 1100;
  for (int sample = 0; sample < kWellPastTheCap; ++sample) {
    const double step = (sample % 21) - 10;
    session_.Update(Pose(step, 0), Eye(-step / 20.0, 0));
  }
  const auto status = session_.Next(error);
  ASSERT_TRUE(status.has_value()) << error;
  EXPECT_FALSE(status->gains_step);
}

// Walks every step before checking the last one finishes the session.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest, TheLastStepFinishesTheSession) {
  ON_CALL(store_, Save).WillByDefault(Return(true));
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  ASSERT_TRUE(PassGainsStep(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40}) {
    session_.Update(Pose(yaw, yaw), EyeSignal{});
    ASSERT_TRUE(session_.Next(error).has_value()) << error;
  }

  session_.Update(Pose(50, 50), EyeSignal{});
  const auto status = session_.Next(error);

  ASSERT_TRUE(status.has_value()) << error;
  EXPECT_FALSE(status->running);
  EXPECT_TRUE(status->done);
  EXPECT_THAT(status->screens, Eq(1));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest, OnFinishedIsToldTheSavedProfile) {
  ON_CALL(store_, Save).WillByDefault(Return(true));
  CalibrationProfile told;
  bool told_at_all = false;
  CalibrationSession session(store_, CalibrationSettings{},
                             [&](const CalibrationProfile& profile) {
                               told = profile;
                               told_at_all = true;
                             });
  std::string start_error;
  ASSERT_TRUE(session.Start(1, start_error).has_value());
  session.Update(Pose(0, 0), EyeSignal{});
  std::string error;
  ASSERT_TRUE(session.Next(error).has_value()) << error;
  for (int step = -10; step <= 10; step += 2) {
    session.Update(Pose(step, 0), Eye(-step / 20.0, 0));
  }
  ASSERT_TRUE(session.Next(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40, 50}) {
    session.Update(Pose(yaw, yaw), EyeSignal{});
    ASSERT_TRUE(session.Next(error).has_value()) << error;
  }

  EXPECT_TRUE(told_at_all);
  ASSERT_THAT(told.screens.size(), Eq(1));
}

// Walks every step, then checks a failed save leaves the capture in place
// to retry.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest, AFailedSaveLeavesTheStepRunning) {
  EXPECT_CALL(store_, Save)
      .WillOnce([](const CalibrationProfile&, std::string& error) {
        error = "disk full";
        return false;
      })
      .WillOnce(Return(true));
  ON_CALL(store_, Save).WillByDefault(Return(true));
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(0, 0), EyeSignal{});
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  ASSERT_TRUE(PassGainsStep(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40}) {
    session_.Update(Pose(yaw, yaw), EyeSignal{});
    ASSERT_TRUE(session_.Next(error).has_value()) << error;
  }
  session_.Update(Pose(50, 50), EyeSignal{});

  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("disk full"));

  // The capture was not lost: finishing can be retried.
  std::string retry_error;
  const auto status = session_.Next(retry_error);
  ASSERT_TRUE(status.has_value()) << retry_error;
  EXPECT_TRUE(status->done);
}

using NoSaveOnCancelTest = CalibrationSessionTest;

TEST_F(NoSaveOnCancelTest, CancellingNeverSaves) {
  EXPECT_CALL(store_, Save).Times(0);
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(5, 5), EyeSignal{});

  std::string error;
  EXPECT_TRUE(session_.Cancel(error));
  EXPECT_THAT(error, IsEmpty());
}

TEST_F(NoSaveOnCancelTest, CancellingLetsANewSessionStart) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  std::string cancel_error;
  ASSERT_TRUE(session_.Cancel(cancel_error));

  std::string error;
  EXPECT_TRUE(session_.Start(1, error).has_value());
}

TEST_F(NoSaveOnCancelTest, RefusesToCancelWithNoSessionRunning) {
  std::string error;
  EXPECT_FALSE(session_.Cancel(error));
  EXPECT_THAT(error, Eq("no calibration session is running"));
}

using NoRunningWithoutCameraTest = CalibrationSessionTest;

TEST_F(NoRunningWithoutCameraTest, RefusesToStartWithTheCameraDown) {
  session_.OnCameraLost();

  std::string error;
  EXPECT_THAT(session_.Start(1, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("the camera is not available"));
}

TEST_F(NoRunningWithoutCameraTest, LosingTheCameraCancelsARunningSession) {
  EXPECT_CALL(store_, Save).Times(0);
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  session_.Update(Pose(5, 5), EyeSignal{});

  session_.OnCameraLost();

  // The session is gone: neither Next nor Cancel finds one running.
  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("no calibration session is running"));
}

TEST_F(NoRunningWithoutCameraTest, LosingAnIdleCameraChangesNothing) {
  session_.OnCameraLost();

  std::string error;
  EXPECT_FALSE(session_.Cancel(error));
  EXPECT_THAT(error, Eq("no calibration session is running"));
}

TEST_F(NoRunningWithoutCameraTest, TheCameraComingBackAllowsStarting) {
  session_.OnCameraLost();
  session_.OnCameraUp();

  std::string error;
  EXPECT_TRUE(session_.Start(1, error).has_value());
}

using UpdateIgnoredWithoutASessionTest = CalibrationSessionTest;

TEST_F(UpdateIgnoredWithoutASessionTest, APoseWithNoSessionChangesNothing) {
  session_.Update(Pose(5, 5), EyeSignal{});

  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("no calibration session is running"));
}

// #128: CalibrationSession's state -- step_, captured_, the running
// sample, camera_up_ -- is shared between a connection's own thread
// (Start, Next, Cancel), the inference queue (Update) and the daemon's
// control queue (OnCameraLost, OnCameraUp), with no lock before this
// fix's mutex_. Walking a full session while a second thread floods it
// with poses the whole time shows a race between them under
// ThreadSanitizer (CI's `thread-sanitizer` job) rather than only in
// production under the right timing.
TEST_F(CalibrationSessionTest,
       UpdateRunsSafelyAlongsideStartAndNextOnAnotherThread) {
  // Every weak-step threshold at 0 (#137): this test's own job is the
  // race between Update (the frames thread) and Start/Next (this one)
  // under ThreadSanitizer, not the weak-step checks NoSaveWithWeakStepTest
  // above already covers; with a threshold above 0, this test's own
  // retry-on-refusal loop below would still be correct, but could spin on
  // a step whose two threads' timing happens to always lose the race for
  // a passing sample, which is exactly the kind of flakiness a test
  // should not carry.
  ON_CALL(store_, Save).WillByDefault(Return(true));
  CalibrationSession session(store_,
                             CalibrationSettings{.min_head_travel_degrees = 0,
                                                 .min_axis_degrees = 0,
                                                 .min_spread_degrees = 0});
  std::atomic<bool> stop{false};
  std::thread frames([&session, &stop] {
    int step = -10;
    while (!stop.load()) {
      session.Update(Pose(step, 1), Eye(-step / 20.0, 0));
      step = step >= 10 ? -10 : step + 1;
    }
  });

  std::string start_error;
  ASSERT_TRUE(session.Start(1, start_error).has_value()) << start_error;
  bool done = false;
  while (!done) {
    std::string error;
    const std::optional<CalibrationController::Status> status =
        session.Next(error);
    if (!status.has_value()) {
      continue;  // Not yet captured by the frames thread; retry.
    }
    done = status->done;
  }

  stop = true;
  frames.join();
}

using NearestCalibratedScreenTest = ::testing::Test;

TEST_F(NearestCalibratedScreenTest, NoScreensIsNoAnswer) {
  EXPECT_THAT(NearestCalibratedScreen({}, Pose(0, 0)), Eq(std::nullopt));
}

TEST_F(NearestCalibratedScreenTest, PicksTheScreenWhoseCentreIsClosest) {
  CalibrationProfile profile;
  ScreenCalibration left;
  left.points[static_cast<std::size_t>(CalibrationPoint::kCentre)] =
      Pose(-30, 0);
  ScreenCalibration right;
  right.points[static_cast<std::size_t>(CalibrationPoint::kCentre)] =
      Pose(30, 0);
  profile.screens = {left, right};

  EXPECT_THAT(NearestCalibratedScreen(profile, Pose(-25, 0)), Eq(0U));
  EXPECT_THAT(NearestCalibratedScreen(profile, Pose(25, 0)), Eq(1U));
}

TEST_F(NearestCalibratedScreenTest, ComparesYawAcrossTheWrapAt180Degrees) {
  CalibrationProfile profile;
  ScreenCalibration near;
  near.points[static_cast<std::size_t>(CalibrationPoint::kCentre)] =
      Pose(175, 0);
  ScreenCalibration far;
  far.points[static_cast<std::size_t>(CalibrationPoint::kCentre)] = Pose(0, 0);
  profile.screens = {far, near};

  // -175 is 10 degrees from 175, not 350: the second screen is nearer.
  EXPECT_THAT(NearestCalibratedScreen(profile, Pose(-175, 0)), Eq(1U));
}

// #141's own screen-box test for LookAwayDetector's other_screen kind.
using CalibratedScreenBoxContainingTest = ::testing::Test;

// One screen whose four corners span yaw -40..-20, pitch -5..5.
ScreenCalibration ScreenSpanning(double left, double right, double top,
                                 double bottom) {
  ScreenCalibration screen;
  screen.points[static_cast<std::size_t>(CalibrationPoint::kTopLeft)] =
      Pose(left, top);
  screen.points[static_cast<std::size_t>(CalibrationPoint::kTopRight)] =
      Pose(right, top);
  screen.points[static_cast<std::size_t>(CalibrationPoint::kBottomLeft)] =
      Pose(left, bottom);
  screen.points[static_cast<std::size_t>(CalibrationPoint::kBottomRight)] =
      Pose(right, bottom);
  // The centre is never read by CalibratedScreenBoxContaining; given an
  // out-of-span value so a bug that read it would be caught.
  screen.points[static_cast<std::size_t>(CalibrationPoint::kCentre)] =
      Pose(1000, 1000);
  return screen;
}

TEST_F(CalibratedScreenBoxContainingTest, NoScreensIsNoAnswer) {
  EXPECT_THAT(CalibratedScreenBoxContaining({}, Pose(0, 0), 3),
              Eq(std::nullopt));
}

TEST_F(CalibratedScreenBoxContainingTest, InsideTheSpanOfItsFourCorners) {
  CalibrationProfile profile;
  profile.screens = {ScreenSpanning(-40, -20, -5, 5)};

  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(-30, 0), 0), Eq(0U));
}

TEST_F(CalibratedScreenBoxContainingTest, OutsideEveryBoxEvenWithTheMargin) {
  CalibrationProfile profile;
  profile.screens = {ScreenSpanning(-40, -20, -5, 5)};

  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(-10, 0), 3),
              Eq(std::nullopt));
  // Past the box's own left edge too, not only its right: both of yaw's
  // own bounds are read, not just the one the test above already covers.
  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(-50, 0), 3),
              Eq(std::nullopt));
}

TEST_F(CalibratedScreenBoxContainingTest,
       TheMarginWidensEveryBoxBySomeDegrees) {
  CalibrationProfile profile;
  profile.screens = {ScreenSpanning(-40, -20, -5, 5)};

  // 18 degrees is just past the box's own -20 edge, but within 3 degrees
  // of margin past it.
  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(-18, 0), 3), Eq(0U));
  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(-16, 0), 3),
              Eq(std::nullopt));
}

TEST_F(CalibratedScreenBoxContainingTest, PitchOutsideTheBoxIsNoMatchEither) {
  CalibrationProfile profile;
  profile.screens = {ScreenSpanning(-40, -20, -5, 5)};

  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(-30, 20), 0),
              Eq(std::nullopt));
  // Past the box's own top edge too, not only its bottom.
  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(-30, -20), 0),
              Eq(std::nullopt));
}

TEST_F(CalibratedScreenBoxContainingTest, PicksTheFirstBoxThatMatches) {
  CalibrationProfile profile;
  profile.screens = {ScreenSpanning(-40, -20, -5, 5),
                     ScreenSpanning(20, 40, -5, 5)};

  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(-30, 0), 0), Eq(0U));
  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(30, 0), 0), Eq(1U));
  EXPECT_THAT(CalibratedScreenBoxContaining(profile, Pose(0, 0), 0),
              Eq(std::nullopt));
}

}  // namespace
}  // namespace facepalm
// NOLINTEND(bugprone-unchecked-optional-access)
