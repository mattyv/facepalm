#include "src/daemon_control.h"

#include <functional>
#include <optional>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Each test stands for the specs/DaemonAdmission.tla property it is named
// for.

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::Return;
using State = CameraController::State;

class MockDaemonController : public DaemonController {
 public:
  MOCK_METHOD(void, Subscribe, (), (override));
  MOCK_METHOD(void, Leave, (), (override));
  MOCK_METHOD(void, Revoke, (), (override));
  MOCK_METHOD(void, Opened, (), (override));
  MOCK_METHOD(void, OpenFailed, (OpenFailure), (override));
  MOCK_METHOD(void, Closed, (), (override));
  MOCK_METHOD(void, DeviceLost, (), (override));
  MOCK_METHOD(void, DeviceBack, (), (override));
  MOCK_METHOD(CameraController::State, State, (), (const, override));
};
class MockDaemonPipeline : public DaemonPipeline {
 public:
  MOCK_METHOD(void, Open, (), (override));
  MOCK_METHOD(void, Close, (std::function<void()>), (override));
};

class DaemonControlTest : public ::testing::Test {
 protected:
  // Holds the pipeline's next retirement until the test runs it.
  void ExpectRetirement() {
    EXPECT_CALL(pipeline_, Close).WillOnce([this](std::function<void()> done) {
      retired_ = std::move(done);
    });
  }

  MockDaemonController controller_;
  MockDaemonPipeline pipeline_;
  bool service_stopped_ = false;
  std::optional<bool> exited_;  // Whether the daemon exited as a failure.
  DaemonControl daemon_{controller_, pipeline_,
                        [this] { service_stopped_ = true; },
                        [this](bool failed) { exited_ = failed; }};
  std::function<void()> retired_;
};

using ReopenAfterRetireTest = DaemonControlTest;

TEST_F(ReopenAfterRetireTest, AClosedCameraCountsOnlyOnceTheModelIsGone) {
  ExpectRetirement();
  daemon_.OnClosed();
  EXPECT_CALL(controller_, Closed);
  retired_();
}

TEST_F(ReopenAfterRetireTest, ADeviceComingBackWaitsForTheRetirement) {
  EXPECT_CALL(controller_, DeviceLost);
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kClosed));
  ExpectRetirement();
  daemon_.OnDeviceLost();
  EXPECT_CALL(controller_, DeviceBack).Times(0);
  daemon_.OnDeviceBack();
  testing::Mock::VerifyAndClearExpectations(&controller_);

  EXPECT_CALL(controller_, DeviceBack);
  retired_();
  EXPECT_CALL(controller_, DeviceBack);
  daemon_.OnDeviceBack();
}

TEST_F(ReopenAfterRetireTest, OnlyTheOpeningCameraCreatesAModel) {
  EXPECT_CALL(controller_, State)
      .WillOnce(Return(State::kOpening))
      .WillOnce(Return(State::kOpen));
  EXPECT_CALL(controller_, Opened);
  EXPECT_CALL(pipeline_, Open);
  daemon_.OnOpened();

  EXPECT_CALL(controller_, State).WillOnce(Return(State::kOpen));
  EXPECT_CALL(controller_, Opened);
  EXPECT_CALL(pipeline_, Open).Times(0);
  daemon_.OnOpened();
}

TEST_F(ReopenAfterRetireTest, AnOpenedAfterClosingBeganCreatesNoModel) {
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kClosing));
  EXPECT_CALL(controller_, Opened);
  EXPECT_CALL(pipeline_, Open).Times(0);
  daemon_.OnOpened();
}

TEST_F(ReopenAfterRetireTest, AnOpeningThatClosesBeforeOpenCreatesNoModel) {
  EXPECT_CALL(controller_, State)
      .WillOnce(Return(State::kOpening))
      .WillOnce(Return(State::kClosing));
  EXPECT_CALL(controller_, Opened);
  EXPECT_CALL(pipeline_, Open).Times(0);
  daemon_.OnOpened();
}

TEST_F(ReopenAfterRetireTest, ADeviceLostWhileClosingWaitsForTheClose) {
  EXPECT_CALL(controller_, DeviceLost);
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kClosing));
  EXPECT_CALL(pipeline_, Close).Times(0);
  daemon_.OnDeviceLost();
}

using ExitCleanTest = DaemonControlTest;

TEST_F(ExitCleanTest, OneRetirementAtATime) {
  EXPECT_CALL(controller_, DeviceLost);
  EXPECT_CALL(controller_, State).WillRepeatedly(Return(State::kClosed));
  ExpectRetirement();
  daemon_.OnDeviceLost();
  daemon_.OnClosed();
  EXPECT_CALL(controller_, Revoke);
  daemon_.Stop();

  retired_();

  EXPECT_THAT(exited_, Eq(false));
}

TEST_F(ExitCleanTest, StoppingWaitsForTheCameraAndTheModel) {
  EXPECT_CALL(controller_, Revoke);
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kClosing));
  daemon_.Stop();
  EXPECT_TRUE(service_stopped_);
  ExpectRetirement();
  daemon_.OnClosed();
  EXPECT_THAT(exited_, Eq(std::nullopt));

  EXPECT_CALL(controller_, Closed);
  retired_();

  EXPECT_THAT(exited_, Eq(false));
}

using StopEndsTest = DaemonControlTest;

TEST_F(StopEndsTest, StoppingWithTheCameraClosedRetiresAtOnce) {
  EXPECT_CALL(controller_, Revoke).Times(1);
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kClosed));
  ExpectRetirement();
  daemon_.Stop();
  daemon_.Stop();
  daemon_.Subscription(true);
  daemon_.OnDeviceBack();

  retired_();

  EXPECT_THAT(exited_, Eq(false));
}

TEST_F(StopEndsTest, AFailedModelStopsTheDaemonAsAFailure) {
  EXPECT_CALL(controller_, Revoke);
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kClosed));
  ExpectRetirement();
  daemon_.Fail();

  retired_();

  EXPECT_THAT(exited_, Eq(true));
}

TEST_F(StopEndsTest, ACameraThatFailsToOpenStopsTheDaemonAsAFailure) {
  EXPECT_CALL(controller_, State)
      .WillOnce(Return(State::kOpening))
      .WillOnce(Return(State::kClosed));
  EXPECT_CALL(controller_, OpenFailed(OpenFailure::kNoDevice));
  EXPECT_CALL(controller_, Revoke);
  ExpectRetirement();
  daemon_.OnOpenFailed(OpenFailure::kNoDevice);
  EXPECT_TRUE(service_stopped_);

  retired_();

  EXPECT_THAT(exited_, Eq(true));
}

TEST_F(StopEndsTest, AFailedOpenReportedAfterClosingBeganIsIgnored) {
  EXPECT_CALL(controller_, Revoke);
  EXPECT_CALL(controller_, State)
      .WillOnce(Return(State::kClosing))
      .WillOnce(Return(State::kClosing));
  daemon_.Stop();
  EXPECT_CALL(controller_, OpenFailed(OpenFailure::kNoDevice));
  EXPECT_CALL(pipeline_, Close).Times(0);
  daemon_.OnOpenFailed(OpenFailure::kNoDevice);
  EXPECT_THAT(exited_, Eq(std::nullopt));

  ExpectRetirement();
  daemon_.OnClosed();
  EXPECT_CALL(controller_, Closed);
  retired_();

  EXPECT_THAT(exited_, Eq(false));
}

// #52, item 10 of #61: a denied permission or a failed start does not stop
// the daemon, unlike a camera with no device to use. CameraController's own
// retry (specs/CameraLifecycle.tla's Retry) runs in this same process.
TEST_F(StopEndsTest, ADeniedPermissionDoesNotStopTheDaemon) {
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kOpening));
  EXPECT_CALL(controller_, OpenFailed(OpenFailure::kPermissionDenied));
  daemon_.OnOpenFailed(OpenFailure::kPermissionDenied);
  EXPECT_FALSE(service_stopped_);
  EXPECT_THAT(exited_, Eq(std::nullopt));
}

TEST_F(StopEndsTest, AFailedStartDoesNotStopTheDaemon) {
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kOpening));
  EXPECT_CALL(controller_, OpenFailed(OpenFailure::kStartFailed));
  daemon_.OnOpenFailed(OpenFailure::kStartFailed);
  EXPECT_FALSE(service_stopped_);
  EXPECT_THAT(exited_, Eq(std::nullopt));
}

using SubscriptionMatchesTest = DaemonControlTest;

TEST_F(SubscriptionMatchesTest, SubscriptionsFollowTheFeed) {
  EXPECT_CALL(controller_, Subscribe);
  daemon_.Subscription(true);
  EXPECT_CALL(controller_, Leave);
  daemon_.Subscription(false);
}

TEST_F(SubscriptionMatchesTest, ChangesAreIgnoredOnceStopping) {
  EXPECT_CALL(controller_, Revoke);
  EXPECT_CALL(controller_, State).WillOnce(Return(State::kClosing));
  daemon_.Stop();
  EXPECT_CALL(controller_, Subscribe).Times(0);
  EXPECT_CALL(controller_, Leave).Times(0);
  EXPECT_CALL(controller_, DeviceBack).Times(0);
  daemon_.Subscription(true);
  daemon_.Subscription(false);
  daemon_.OnDeviceBack();
}

using JoinOpensTest = DaemonControlTest;

TEST_F(JoinOpensTest, ADeviceComingBackIsLetIn) {
  EXPECT_CALL(controller_, DeviceBack);
  daemon_.OnDeviceBack();
}

}  // namespace
}  // namespace facepalm
