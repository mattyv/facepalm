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

TEST(CameraFailureReasonTest, MapsTypedSelectionAndStartFailures) {
  EXPECT_EQ(CameraFailureReason(OpenFailure::kNoDevice, std::nullopt),
            ServiceReason::kNoCandidate);
  EXPECT_EQ(
      CameraFailureReason(OpenFailure::kNoDevice, ConfigureError::kNoCandidate),
      ServiceReason::kNoCandidate);
  EXPECT_EQ(CameraFailureReason(OpenFailure::kNoDevice,
                                ConfigureError::kPermissionDenied),
            ServiceReason::kPermissionDenied);
  EXPECT_EQ(
      CameraFailureReason(OpenFailure::kNoDevice, ConfigureError::kQueryFailed),
      ServiceReason::kQueryFailed);
  EXPECT_EQ(
      CameraFailureReason(OpenFailure::kNoDevice, ConfigureError::kUnsupported),
      ServiceReason::kUnsupported);
  EXPECT_EQ(CameraFailureReason(OpenFailure::kPermissionDenied, std::nullopt),
            ServiceReason::kPermissionDenied);
  EXPECT_EQ(CameraFailureReason(OpenFailure::kStartFailed, std::nullopt),
            ServiceReason::kStartFailed);
  EXPECT_EQ(CameraFailureReason(OpenFailure::kNoDevice,
                                static_cast<ConfigureError>(255)),
            ServiceReason::kNoCandidate);
}

TEST(CameraFailureReasonTest, RepeatedModelFailuresKeepTheFirstSnapshot) {
  ServicePhase phase = ServicePhase::kActive;
  ServiceReason reason = ServiceReason::kNone;
  std::string detail;
  RecordModelFailure(phase, reason, detail, "first analyzer failed");
  EXPECT_EQ(phase, ServicePhase::kFailed);
  EXPECT_EQ(reason, ServiceReason::kModelFailure);
  EXPECT_EQ(detail, "first analyzer failed");
  RecordModelFailure(phase, reason, detail, "later analyzer failed");
  EXPECT_EQ(phase, ServicePhase::kFailed);
  EXPECT_EQ(reason, ServiceReason::kModelFailure);
  EXPECT_EQ(detail, "first analyzer failed");
}

}  // namespace
}  // namespace facepalm
