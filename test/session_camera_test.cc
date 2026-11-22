#include "src/session_camera.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using FrameRateRange = CaptureSession::FrameRateRange;
using FrameRates = CaptureSession::FrameRates;
using Permission = CaptureSession::Permission;

class MockCaptureSession : public CaptureSession {
 public:
  MOCK_METHOD(Permission, CurrentPermission, (), (override));
  MOCK_METHOD(void, RequestPermission, (std::function<void(bool)>), (override));
  MOCK_METHOD(std::optional<std::string>, Configure, (), (override));
  MOCK_METHOD(FrameRates, SupportedFrameRates, (), (override));
  MOCK_METHOD(bool, Start, (int, std::uint64_t), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(std::vector<CameraDevice>, List, (), (override));
};

class MockCameraReports : public CameraReports {
 public:
  MOCK_METHOD(void, OnOpened, (), (override));
  MOCK_METHOD(void, OnOpenFailed, (OpenFailure), (override));
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
constexpr std::uint64_t kAttempt = 1;
// The unique ID of the camera the fake session configures.
const std::string& CameraId() {
  static const auto* const id = new std::string("built-in");
  return *id;
}
const FrameRates& CameraRates() {
  static const auto* const rates =
      new FrameRates{FrameRateRange{.min = 10, .max = 30}};
  return *rates;
}

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
        .WillByDefault(Return(CameraRates()));
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
    EXPECT_CALL(capture_, Configure()).WillOnce(Return(CameraId()));
    EXPECT_CALL(capture_, Start(kFramesPerSecond, kAttempt))
        .WillOnce(Return(true));
    EXPECT_CALL(reports_, OnOpened());
  }
  camera_.Open(kAttempt);
  Settle();
  EXPECT_EQ(ran_on_, (std::vector<std::string>{"session", "control"}));
}

TEST_F(SessionCameraTest, DoesNothingUntilItsQueueRuns) {
  camera_.Open(kAttempt);
  camera_.Close();
  EXPECT_TRUE(ran_on_.empty());
}

TEST_F(SessionCameraTest, ReportsOpenFailedWhenPermissionIsDenied) {
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kDenied));
  EXPECT_CALL(reports_, OnOpenFailed(OpenFailure::kPermissionDenied));
  camera_.Open(kAttempt);
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
  camera_.Open(kAttempt);
  Settle();
  ASSERT_TRUE(answer);
  EXPECT_CALL(capture_, Configure()).WillOnce(Return(CameraId()));
  EXPECT_CALL(capture_, Start(kFramesPerSecond, kAttempt))
      .WillOnce(Return(true));
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
  EXPECT_CALL(reports_, OnOpenFailed(OpenFailure::kPermissionDenied));
  camera_.Open(kAttempt);
  Settle();
}

TEST_F(SessionCameraTest, ReportsOpenFailedWithoutACamera) {
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kGranted));
  EXPECT_CALL(capture_, Configure()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(reports_, OnOpenFailed(OpenFailure::kNoDevice));
  camera_.Open(kAttempt);
  Settle();
}

TEST_F(SessionCameraTest, ReportsOpenFailedWhenTheSessionDoesNotRun) {
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kGranted));
  EXPECT_CALL(capture_, Configure()).WillOnce(Return(CameraId()));
  EXPECT_CALL(capture_, Start(kFramesPerSecond, kAttempt))
      .WillOnce(Return(false));
  EXPECT_CALL(reports_, OnOpenFailed(OpenFailure::kStartFailed));
  camera_.Open(kAttempt);
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

// Opens the camera on CameraId(), then forgets the calls that took.
void OpenOn(SessionCamera& camera, MockCaptureSession& capture,
            MockCameraReports& reports, const std::function<void()>& settle) {
  EXPECT_CALL(capture, CurrentPermission())
      .WillOnce(Return(Permission::kGranted));
  EXPECT_CALL(capture, Configure()).WillOnce(Return(CameraId()));
  EXPECT_CALL(capture, Start(_, _)).WillOnce(Return(true));
  EXPECT_CALL(reports, OnOpened());
  camera.Open(kAttempt);
  settle();
  testing::Mock::VerifyAndClearExpectations(&capture);
  testing::Mock::VerifyAndClearExpectations(&reports);
}

TEST_F(SessionCameraTest, StopsAndReportsTheCameraInUseDisconnecting) {
  OpenOn(camera_, capture_, reports_, [this] { Settle(); });
  EXPECT_CALL(capture_, Stop());
  EXPECT_CALL(reports_, OnDeviceLost());
  camera_.OnDeviceDisconnected(CameraId());
  Settle();
}

TEST_F(SessionCameraTest, IgnoresAnotherCameraDisconnecting) {
  OpenOn(camera_, capture_, reports_, [this] { Settle(); });
  camera_.OnDeviceDisconnected("usb");
  Settle();
}

TEST_F(SessionCameraTest, StopsAndReportsTheSessionFailing) {
  EXPECT_CALL(capture_, Stop());
  EXPECT_CALL(reports_, OnDeviceLost());
  camera_.OnSessionFailed();
  Settle();
}

TEST_F(SessionCameraTest, ReportsTheCameraInUseComingBack) {
  OpenOn(camera_, capture_, reports_, [this] { Settle(); });
  ran_on_.clear();
  EXPECT_CALL(reports_, OnDeviceBack());
  camera_.OnDeviceConnected(CameraId());
  Settle();
  EXPECT_EQ(ran_on_, (std::vector<std::string>{"session", "control"}));
}

TEST_F(SessionCameraTest, IgnoresAnotherCameraConnecting) {
  OpenOn(camera_, capture_, reports_, [this] { Settle(); });
  camera_.OnDeviceConnected("usb");
  Settle();
}

// specs/CameraLifecycle.tla's WantedOpens, with GuardDiscoverAnyCamera
// (#39): with no camera picked yet, any camera coming counts.
TEST_F(SessionCameraTest, ReportsAnyCameraConnectingBeforeOneIsPicked) {
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kGranted));
  EXPECT_CALL(capture_, Configure()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(reports_, OnOpenFailed(OpenFailure::kNoDevice));
  camera_.Open(kAttempt);
  Settle();
  EXPECT_CALL(reports_, OnDeviceBack());
  camera_.OnDeviceConnected("usb");
  Settle();
}

// specs/CameraLifecycle.tla's NoCaptureWhenClosed, with
// GuardStartChecksAttempt (#38): a permission granted after the camera was
// closed starts nothing.
TEST_F(SessionCameraTest, StartsNothingWhenPermissionIsGrantedAfterClose) {
  std::function<void(bool)> answer;
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kUndetermined));
  EXPECT_CALL(capture_, RequestPermission(_))
      .WillOnce([&answer](std::function<void(bool)> done) {
        answer = std::move(done);
      });
  camera_.Open(kAttempt);
  Settle();
  EXPECT_CALL(capture_, Stop());
  EXPECT_CALL(reports_, OnClosed());
  camera_.Close();
  Settle();
  ASSERT_TRUE(answer);

  answer(/*granted=*/true);  // StrictMock: no Configure, Start or report.
  Settle();
}

// The same, with the camera reopened before the first answer comes: only
// the current attempt's answer starts it.
TEST_F(SessionCameraTest, StartsOnlyForTheCurrentAttempt) {
  std::vector<std::function<void(bool)>> answers;
  EXPECT_CALL(capture_, CurrentPermission())
      .Times(2)
      .WillRepeatedly(Return(Permission::kUndetermined));
  EXPECT_CALL(capture_, RequestPermission(_))
      .Times(2)
      .WillRepeatedly([&answers](std::function<void(bool)> done) {
        answers.push_back(std::move(done));
      });
  EXPECT_CALL(capture_, Stop());
  EXPECT_CALL(reports_, OnClosed());
  camera_.Open(kAttempt);
  camera_.Close();
  camera_.Open(kAttempt + 1);
  Settle();
  ASSERT_EQ(answers.size(), 2U);
  EXPECT_CALL(capture_, Configure()).WillOnce(Return(CameraId()));
  EXPECT_CALL(capture_, Start(kFramesPerSecond, kAttempt + 1))
      .WillOnce(Return(true));
  EXPECT_CALL(reports_, OnOpened());

  answers[0](/*granted=*/true);
  answers[1](/*granted=*/true);
  Settle();
}

TEST_F(SessionCameraTest, ARefusalAfterCloseReportsNothing) {
  std::function<void(bool)> answer;
  EXPECT_CALL(capture_, CurrentPermission())
      .WillOnce(Return(Permission::kUndetermined));
  EXPECT_CALL(capture_, RequestPermission(_))
      .WillOnce([&answer](std::function<void(bool)> done) {
        answer = std::move(done);
      });
  EXPECT_CALL(capture_, Stop());
  EXPECT_CALL(reports_, OnClosed());
  camera_.Open(kAttempt);
  camera_.Close();
  Settle();

  answer(/*granted=*/false);
  Settle();
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
  ON_CALL(capture, Configure()).WillByDefault(Return(CameraId()));
  ON_CALL(capture, SupportedFrameRates())
      .WillByDefault(Return(std::move(rates)));
  int started = 0;
  ON_CALL(capture, Start(_, _))
      .WillByDefault([&started](int rate, std::uint64_t) {
        started = rate;
        return true;
      });
  SessionCamera camera(capture, reports, queue.AsPost(), queue.AsPost(),
                       wanted);
  camera.Open(kAttempt);
  queue.Drain();
  return started;
}

TEST(SessionCameraFrameRateTest, AsksForTheRateWantedWhenSupported) {
  EXPECT_EQ(StartedRate(20, {{.min = 15, .max = 30}}), 20);
}

TEST(SessionCameraFrameRateTest, AsksForTheSlowestSupportedWhenWantedIsSlower) {
  EXPECT_EQ(StartedRate(5, {{.min = 15, .max = 30}}), 15);
}

TEST(SessionCameraFrameRateTest, AsksForTheFastestSupportedWhenWantedIsFaster) {
  EXPECT_EQ(StartedRate(60, {{.min = 15, .max = 30}}), 30);
}

TEST(SessionCameraFrameRateTest, AsksForTheWantedRateWhenTheCameraReportsNone) {
  EXPECT_EQ(StartedRate(20, {}), 20);
}

// #43: two separate ranges, such as a camera with 15 and 30 fps presets,
// are not the same as one collapsed range of 15 to 30. Asking for 20, which
// neither range actually supports, picks the nearer range's own rate (15,
// not the unsupported 20 the old collapsed min/max would have asked for).
TEST(SessionCameraFrameRateTest, AsksForTheNearerOfTwoSeparateRanges) {
  const FrameRates two_ranges{FrameRateRange{.min = 15, .max = 15},
                              FrameRateRange{.min = 30, .max = 30}};
  EXPECT_EQ(StartedRate(20, two_ranges), 15);
  EXPECT_EQ(StartedRate(26, two_ranges), 30);
}

// A fixed rate such as 29.97 fps rounds outward to a range whose min is
// below its max, such as {29, 30}; the old collapsed form could instead
// produce {ceil 30, floor 29}, breaking std::clamp's ordered-bounds
// precondition (#43).
TEST(SessionCameraFrameRateTest, HandlesARangeRoundedOutwardFromAFixedRate) {
  EXPECT_EQ(StartedRate(15, {{.min = 29, .max = 30}}), 29);
  EXPECT_EQ(StartedRate(60, {{.min = 29, .max = 30}}), 30);
}

TEST(SessionCameraFrameRateTest, PicksTheClosestAmongSeveralRanges) {
  const FrameRates ranges{FrameRateRange{.min = 5, .max = 10},
                          FrameRateRange{.min = 40, .max = 60},
                          FrameRateRange{.min = 20, .max = 25}};
  EXPECT_EQ(StartedRate(22, ranges), 22);
  EXPECT_EQ(StartedRate(30, ranges), 25);
}

// A rate near INT_MIN, with a range far from it, once overflowed int while
// comparing two candidates' distance from it (#120: found by
// SessionCameraFuzz.ChosenRateWithinSupportedRange's fuzzing mode under
// UndefinedBehaviorSanitizer, not its own brief replay mode, since that
// never generates so extreme a value). Asserting on the result, not just
// that the call survives, is what UBSan's check of the subtraction itself
// stands in for here; this test's job is to keep this exact input from
// crashing or misbehaving again.
TEST(SessionCameraFrameRateTest, DoesNotOverflowOnARateNearIntMin) {
  const FrameRates ranges{FrameRateRange{.min = 60, .max = 60},
                          FrameRateRange{.min = 53, .max = 104},
                          FrameRateRange{.min = 4, .max = 20},
                          FrameRateRange{.min = 60, .max = 61}};
  EXPECT_EQ(NearestSupportedRate(-2147483647, ranges), 4);
}

}  // namespace
}  // namespace facepalm
