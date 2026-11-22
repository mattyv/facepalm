#include "src/calibration.h"

#include <atomic>
#include <optional>
#include <string>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
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

class MockCalibrationStore : public CalibrationStore {
 public:
  MOCK_METHOD(bool, Save,
              (const CalibrationProfile& profile, std::string& error),
              (override));
};

class CalibrationSessionTest : public ::testing::Test {
 protected:
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
  EXPECT_THAT(status->total, Eq(6));  // The resting pose, plus 5 points.
  EXPECT_TRUE(status->resting_step);
  EXPECT_THAT(error, IsEmpty());
}

TEST_F(StartTest, CountsEveryScreensTargets) {
  std::string error;
  const std::optional<CalibrationController::Status> status =
      session_.Start(2, error);

  ASSERT_TRUE(status.has_value());
  EXPECT_THAT(status->total, Eq(11));  // The resting pose, plus 2*5 points.
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
  session_.Update(Pose(0, 0));
  std::string first_next_error;
  ASSERT_TRUE(session_.Next(first_next_error).has_value());

  // The next step has not been captured, even though the last one was.
  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("no pose captured yet for this step"));
}

TEST_F(NoAdvanceWithoutCaptureTest, RefusesNextWithNoSessionRunning) {
  std::string error;
  EXPECT_THAT(session_.Next(error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("no calibration session is running"));
}

using CalibrationWalksEveryTargetTest = CalibrationSessionTest;

// Walks every one of a screen's five points in order.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest, WalksEachScreensFivePointsInOrder) {
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());

  session_.Update(Pose(1, -1));
  std::string resting_error;
  const auto after_resting = session_.Next(resting_error);
  ASSERT_TRUE(after_resting.has_value());
  EXPECT_FALSE(after_resting->resting_step);
  EXPECT_THAT(after_resting->index, Eq(1));
  EXPECT_THAT(after_resting->screen, Eq(0));
  EXPECT_THAT(after_resting->point, Eq(CalibrationPoint::kCentre));

  for (const CalibrationPoint point :
       {CalibrationPoint::kTopLeft, CalibrationPoint::kTopRight,
        CalibrationPoint::kBottomLeft, CalibrationPoint::kBottomRight}) {
    session_.Update(Pose(10, 10));
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

  session_.Update(Pose(2, -2));
  session_.Update(Pose(4, -4));  // Resting: averages to (3, -3).
  std::string error;
  ASSERT_TRUE(session_.Next(error).has_value()) << error;
  for (const double yaw : {10, 20, 30, 40, 50}) {
    session_.Update(Pose(yaw, yaw));
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

// Walks every step before checking the last one finishes the session.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationWalksEveryTargetTest, TheLastStepFinishesTheSession) {
  ON_CALL(store_, Save).WillByDefault(Return(true));
  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value());
  for (int step = 0; step < 5; ++step) {
    session_.Update(Pose(0, 0));
    std::string error;
    ASSERT_TRUE(session_.Next(error).has_value()) << error;
  }

  session_.Update(Pose(0, 0));
  std::string error;
  const auto status = session_.Next(error);

  ASSERT_TRUE(status.has_value());
  EXPECT_FALSE(status->running);
  EXPECT_TRUE(status->done);
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
  for (int step = 0; step < 5; ++step) {
    session_.Update(Pose(0, 0));
    std::string step_error;
    ASSERT_TRUE(session_.Next(step_error).has_value()) << step_error;
  }
  session_.Update(Pose(0, 0));

  std::string error;
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
  session_.Update(Pose(5, 5));

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
  session_.Update(Pose(5, 5));

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
  session_.Update(Pose(5, 5));

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
  ON_CALL(store_, Save).WillByDefault(Return(true));
  std::atomic<bool> stop{false};
  std::thread frames([this, &stop] {
    while (!stop.load()) {
      session_.Update(Pose(1, 1));
    }
  });

  std::string start_error;
  ASSERT_TRUE(session_.Start(1, start_error).has_value()) << start_error;
  bool done = false;
  while (!done) {
    std::string error;
    const std::optional<CalibrationController::Status> status =
        session_.Next(error);
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

}  // namespace
}  // namespace facepalm
// NOLINTEND(bugprone-unchecked-optional-access)
