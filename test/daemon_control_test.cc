#include "src/daemon_control.h"

#include <functional>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {
class MockDaemonController : public DaemonController {
 public:
  MOCK_METHOD(void, Subscribe, (), (override));
  MOCK_METHOD(void, Leave, (), (override));
  MOCK_METHOD(void, Revoke, (), (override));
  MOCK_METHOD(void, Opened, (), (override));
  MOCK_METHOD(void, OpenFailed, (), (override));
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

TEST(DaemonControlTest, CameraCloseWaitsForModelRetirementBeforeReopen) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  std::function<void()> retired;
  EXPECT_CALL(pipeline, Close).WillOnce([&](auto done) {
    retired = std::move(done);
  });
  daemon.OnClosed();
  EXPECT_CALL(controller, Closed);
  retired();
}

TEST(DaemonControlTest, StopRevokesConsentAndWaitsForPhysicalAndModelClose) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  bool service_stopped = false;
  bool stopped = false;
  DaemonControl daemon(
      controller, pipeline, [&] { service_stopped = true; },
      [&] { stopped = true; });
  EXPECT_CALL(controller, Revoke);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosing));
  daemon.Stop();
  EXPECT_TRUE(service_stopped);
  EXPECT_FALSE(stopped);
  std::function<void()> retired;
  EXPECT_CALL(pipeline, Close).WillOnce([&](auto done) {
    retired = std::move(done);
  });
  daemon.OnClosed();
  EXPECT_FALSE(stopped);
  EXPECT_CALL(controller, Closed);
  retired();
  EXPECT_TRUE(stopped);
}

TEST(DaemonControlTest, NoLateOpenCreatesModelAfterConsentIsRevoked) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, Opened);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosing));
  daemon.OnOpened();
}
TEST(DaemonControlTest, AFailedOpenDuringShutdownCanFinishRetirement) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  bool stopped = false;
  DaemonControl daemon(controller, pipeline, [] {}, [&] { stopped = true; });
  EXPECT_CALL(controller, Revoke);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosing));
  daemon.Stop();
  EXPECT_CALL(controller, OpenFailed);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosed));
  std::function<void()> retired;
  EXPECT_CALL(pipeline, Close).WillOnce([&](auto done) {
    retired = std::move(done);
  });
  daemon.OnOpenFailed();
  EXPECT_FALSE(stopped);
  retired();
  EXPECT_TRUE(stopped);
}

TEST(DaemonControlTest, DuplicateOpenedDoesNotCreateAnotherModel) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kOpen));
  EXPECT_CALL(controller, Opened);
  EXPECT_CALL(pipeline, Open).Times(0);
  daemon.OnOpened();
}

TEST(DaemonControlTest, SubscriptionsAndOpeningFollowTheController) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, Subscribe);
  daemon.Subscription(true);
  EXPECT_CALL(controller, Leave);
  daemon.Subscription(false);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kOpening))
      .WillOnce(testing::Return(CameraController::State::kOpen));
  EXPECT_CALL(controller, Opened);
  EXPECT_CALL(pipeline, Open);
  daemon.OnOpened();
}

TEST(DaemonControlTest, DeviceReturnWaitsForRetirement) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, DeviceLost);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosed));
  std::function<void()> retired;
  EXPECT_CALL(pipeline, Close).WillOnce([&](auto done) {
    retired = std::move(done);
  });
  daemon.OnDeviceLost();
  daemon.OnDeviceBack();
  EXPECT_CALL(controller, DeviceBack);
  retired();
  EXPECT_CALL(controller, DeviceBack);
  daemon.OnDeviceBack();
}

TEST(DaemonControlTest, StopWhileIdleAndRepeatedStop) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  bool stopped = false;
  DaemonControl daemon(controller, pipeline, [] {}, [&] { stopped = true; });
  EXPECT_CALL(controller, Revoke).Times(1);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosed));
  std::function<void()> retired;
  EXPECT_CALL(pipeline, Close).WillOnce([&](auto done) {
    retired = std::move(done);
  });
  daemon.Stop();
  daemon.Stop();
  daemon.Subscription(true);
  daemon.OnDeviceBack();
  retired();
  EXPECT_TRUE(stopped);
}

TEST(DaemonControlTest, DeviceLossDuringClosingWaitsForClosedReport) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, DeviceLost);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosing));
  EXPECT_CALL(pipeline, Close).Times(0);
  daemon.OnDeviceLost();
}

TEST(DaemonControlTest, DeferredRetirementIgnoresDuplicateCloseAndStop) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  bool stopped = false;
  DaemonControl daemon(controller, pipeline, [] {}, [&] { stopped = true; });
  EXPECT_CALL(controller, DeviceLost);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosed))
      .WillOnce(testing::Return(CameraController::State::kClosed));
  std::function<void()> retired;
  EXPECT_CALL(pipeline, Close).WillOnce([&](auto done) {
    retired = std::move(done);
  });
  daemon.OnDeviceLost();
  daemon.OnClosed();
  EXPECT_CALL(controller, Revoke);
  daemon.Stop();
  retired();
  EXPECT_TRUE(stopped);
  daemon.OnDeviceBack();
}

TEST(DaemonControlTest, OpenFailedOutsideShutdownDoesNotRetire) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, OpenFailed);
  daemon.OnOpenFailed();
}

TEST(DaemonControlTest, OpenFailedWhileRetirementRunsDoesNotRetireTwice) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, Revoke);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosed));
  std::function<void()> retired;
  EXPECT_CALL(pipeline, Close).WillOnce([&](auto done) {
    retired = std::move(done);
  });
  daemon.Stop();
  EXPECT_CALL(controller, OpenFailed);
  daemon.OnOpenFailed();
  retired();
}

TEST(DaemonControlTest, FailureWhilePhysicalClosePendingWaitsForClose) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, Revoke);
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kClosing))
      .WillOnce(testing::Return(CameraController::State::kClosing));
  daemon.Stop();
  EXPECT_CALL(controller, OpenFailed);
  EXPECT_CALL(pipeline, Close).Times(0);
  daemon.OnOpenFailed();
}

TEST(DaemonControlTest, RejectedOpenDoesNotCreateModel) {
  MockDaemonController controller;
  MockDaemonPipeline pipeline;
  DaemonControl daemon(controller, pipeline, [] {}, [] {});
  EXPECT_CALL(controller, State)
      .WillOnce(testing::Return(CameraController::State::kOpening))
      .WillOnce(testing::Return(CameraController::State::kClosing));
  EXPECT_CALL(controller, Opened);
  EXPECT_CALL(pipeline, Open).Times(0);
  daemon.OnOpened();
}

}  // namespace
}  // namespace facepalm
