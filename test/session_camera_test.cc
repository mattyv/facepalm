#include "src/session_camera.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using FrameRates = CaptureSession::FrameRates;
using Permission = CaptureSession::Permission;

class MockCaptureSession : public CaptureSession {
 public:
  MOCK_METHOD(Permission, CurrentPermission, (), (override));
  MOCK_METHOD(void, RequestPermission, (std::function<void(bool)>), (override));
  MOCK_METHOD(bool, Configure, (), (override));
  MOCK_METHOD(FrameRates, SupportedFrameRates, (), (override));
  MOCK_METHOD(bool, Start, (int), (override));
  MOCK_METHOD(void, Stop, (), (override));
};

class MockCameraReports : public CameraReports {
 public:
  MOCK_METHOD(void, OnOpened, (), (override));
  MOCK_METHOD(void, OnOpenFailed, (), (override));
  MOCK_METHOD(void, OnClosed, (), (override));
  MOCK_METHOD(void, OnDeviceLost, (), (override));
  MOCK_METHOD(void, OnDeviceBack, (), (override));
};

// A serial queue the test runs by hand, and names, so a test can check which
// queue each step ran on.
class FakeQueue {
 public:
  explicit FakeQueue(std::string name, std::vector<std::string>& log)
      : name_(std::move(name)), log_(log) {}

  Post AsPost() {
    return [this](std::function<void()> task) {
      tasks_.push_back(std::move(task));
    };
  }

  // Runs every task queued so far, and any they queue.
  void Drain() {
    while (!tasks_.empty()) {
      const std::function<void()> task = std::move(tasks_.front());
      tasks_.pop_front();
      log_.push_back(name_);
      task();
    }
  }

  [[nodiscard]] bool Empty() const { return tasks_.empty(); }

 private:
  std::string name_;
  std::vector<std::string>& log_;
  std::deque<std::function<void()>> tasks_;
};

constexpr int kFramesPerSecond = 15;
constexpr FrameRates kCameraRates{.min = 10, .max = 30};

class SessionCameraTest : public testing::Test {
 protected:
  // Runs both queues until neither has work left.
  void Settle() {
    while (!session_.Empty() || !control_.Empty()) {
      session_.Drain();
      control_.Drain();
    }
  }

  std::vector<std::string> ran_on_;
  FakeQueue session_{"session", ran_on_};
  FakeQueue control_{"control", ran_on_};
  void SetUp() override {
    ON_CALL(capture_, SupportedFrameRates())
        .WillByDefault(Return(kCameraRates));
    EXPECT_CALL(capture_, SupportedFrameRates()).Times(testing::AnyNumber());
  }

  StrictMock<MockCaptureSession> capture_;
  StrictMock<MockCameraReports> reports_;
  SessionCamera camera_{capture_, reports_, session_.AsPost(),
                        control_.AsPost(), kFramesPerSecond};
};

TEST_F(SessionCameraTest, OpensWithPermissionAtTheChosenFrameRate) {
  {
    InSequence order;
    EXPECT_CALL(capture_, CurrentPermission())
        .WillOnce(Return(Permission::kGranted));
    EXPECT_CALL(capture_, Configure()).WillOnce(Return(true));
    EXPECT_CALL(capture_, Start(kFramesPerSecond)).WillOnce(Return(true));
    EXPECT_CALL(reports_, OnOpened());
  }
  camera_.Open();
  Settle();
  EXPECT_EQ(ran_on_, (std::vector<std::string>{"session", "control"}));
}

TEST_F(SessionCameraTest, DoesNothingUntilItsQueueRuns) {
  camera_.Open();
  camera_.Close();
  EXPECT_TRUE(ran_on_.empty());
}

TEST_F(SessionCameraTest, ReportsOpenFailedWhenPermissionIsDenied) {
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kDenied));
  EXPECT_CALL(reports_, OnOpenFailed());
  camera_.Open();
  Settle();
}

TEST_F(SessionCameraTest, AsksForPermissionAndOpensWhenGranted) {
  std::function<void(bool)> answer;
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kUndetermined));
  EXPECT_CALL(capture_, RequestPermission(_))
      .WillOnce([&answer](std::function<void(bool)> done) {
        answer = std::move(done);
      });
  camera_.Open();
  Settle();
  ASSERT_TRUE(answer);
  EXPECT_CALL(capture_, Configure()).WillOnce(Return(true));
  EXPECT_CALL(capture_, Start(kFramesPerSecond)).WillOnce(Return(true));
  EXPECT_CALL(reports_, OnOpened());
  ran_on_.clear();
  answer(/*granted=*/true);
  Settle();
  // The answer can arrive on any thread; the start still runs on the session
  // queue.
  EXPECT_EQ(ran_on_, (std::vector<std::string>{"session", "control"}));
}

TEST_F(SessionCameraTest, ReportsOpenFailedWhenTheUserRefuses) {
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kUndetermined));
  EXPECT_CALL(capture_, RequestPermission(_))
      .WillOnce([](const std::function<void(bool)>& done) {
        done(/*granted=*/false);
      });
  EXPECT_CALL(reports_, OnOpenFailed());
  camera_.Open();
  Settle();
}

TEST_F(SessionCameraTest, ReportsOpenFailedWithoutACamera) {
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kGranted));
  EXPECT_CALL(capture_, Configure()).WillOnce(Return(false));
  EXPECT_CALL(reports_, OnOpenFailed());
  camera_.Open();
  Settle();
}

TEST_F(SessionCameraTest, ReportsOpenFailedWhenTheSessionDoesNotRun) {
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kGranted));
  EXPECT_CALL(capture_, Configure()).WillOnce(Return(true));
  EXPECT_CALL(capture_, Start(kFramesPerSecond)).WillOnce(Return(false));
  EXPECT_CALL(reports_, OnOpenFailed());
  camera_.Open();
  Settle();
}

TEST_F(SessionCameraTest, ReportsClosedOnceStopped) {
  {
    InSequence order;
    EXPECT_CALL(capture_, Stop());
    EXPECT_CALL(reports_, OnClosed());
  }
  camera_.Close();
  Settle();
  EXPECT_EQ(ran_on_, (std::vector<std::string>{"session", "control"}));
}

TEST_F(SessionCameraTest, StopsAndReportsALostDevice) {
  EXPECT_CALL(capture_, Stop());
  EXPECT_CALL(reports_, OnDeviceLost());
  camera_.OnDeviceLost();
  Settle();
}

TEST_F(SessionCameraTest, ReportsADeviceComingBack) {
  EXPECT_CALL(reports_, OnDeviceBack());
  camera_.OnDeviceBack();
  Settle();
  EXPECT_EQ(ran_on_, (std::vector<std::string>{"control"}));
}

// Opens a camera that supports `rates`, wanting `wanted` frames a second, and
// returns the rate it asked the session for.
int StartedRate(int wanted, FrameRates rates) {
  std::vector<std::string> ran_on;
  FakeQueue queue("session", ran_on);
  testing::NiceMock<MockCaptureSession> capture;
  testing::NiceMock<MockCameraReports> reports;
  ON_CALL(capture, CurrentPermission())
      .WillByDefault(Return(Permission::kGranted));
  ON_CALL(capture, Configure()).WillByDefault(Return(true));
  ON_CALL(capture, SupportedFrameRates()).WillByDefault(Return(rates));
  int started = 0;
  ON_CALL(capture, Start(_)).WillByDefault([&started](int rate) {
    started = rate;
    return true;
  });
  SessionCamera camera(capture, reports, queue.AsPost(), queue.AsPost(),
                       wanted);
  camera.Open();
  queue.Drain();
  return started;
}

TEST(SessionCameraFrameRateTest, AsksForTheRateWantedWhenSupported) {
  EXPECT_EQ(StartedRate(20, {.min = 15, .max = 30}), 20);
}

TEST(SessionCameraFrameRateTest, AsksForTheSlowestSupportedWhenWantedIsSlower) {
  EXPECT_EQ(StartedRate(5, {.min = 15, .max = 30}), 15);
}

TEST(SessionCameraFrameRateTest, AsksForTheFastestSupportedWhenWantedIsFaster) {
  EXPECT_EQ(StartedRate(60, {.min = 15, .max = 30}), 30);
}

TEST(SessionCameraFrameRateTest, AsksForTheWantedRateWhenTheCameraReportsNone) {
  EXPECT_EQ(StartedRate(20, {}), 20);
}

}  // namespace
}  // namespace facepalm
