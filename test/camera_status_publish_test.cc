#include "src/camera_status_publish.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/camera_controller.h"

namespace facepalm {
namespace {

using ::testing::Eq;
using State = CameraController::State;

TEST(DecideCameraStatusOnOpenedTest,
     PublishesOnWhenItWasOpeningAndReachedOpen) {
  EXPECT_THAT(DecideCameraStatusOnOpened(/*was_opening=*/true, State::kOpen),
              Eq(CameraStatusAction::kPublishOn));
}

TEST(DecideCameraStatusOnOpenedTest, DoesNothingWhenItWasNotOpening) {
  EXPECT_THAT(DecideCameraStatusOnOpened(/*was_opening=*/false, State::kOpen),
              Eq(CameraStatusAction::kNone));
}

TEST(DecideCameraStatusOnOpenedTest,
     DoesNothingWhenItWasOpeningButDidNotReachOpen) {
  EXPECT_THAT(DecideCameraStatusOnOpened(/*was_opening=*/true, State::kClosed),
              Eq(CameraStatusAction::kNone));
  EXPECT_THAT(DecideCameraStatusOnOpened(/*was_opening=*/true, State::kOpening),
              Eq(CameraStatusAction::kNone));
  EXPECT_THAT(DecideCameraStatusOnOpened(/*was_opening=*/true, State::kClosing),
              Eq(CameraStatusAction::kNone));
}

TEST(DecideCameraStatusOnOpenFailedTest, PublishesOffWhenItWasOpening) {
  EXPECT_THAT(DecideCameraStatusOnOpenFailed(/*was_opening=*/true),
              Eq(CameraStatusAction::kPublishOff));
}

TEST(DecideCameraStatusOnOpenFailedTest, DoesNothingWhenItWasNotOpening) {
  EXPECT_THAT(DecideCameraStatusOnOpenFailed(/*was_opening=*/false),
              Eq(CameraStatusAction::kNone));
}

}  // namespace
}  // namespace facepalm
