// Tests src/linux/v4l2_session.cc with libv4l2 and poll mocked: the fakes at
// the end of this file stand in for them at link time, so no camera is
// opened (#83).

#include "src/linux/v4l2_session.h"

#include <fcntl.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/types.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/frame.h"
#include "src/linux/v4l2_hotplug.h"
#include "src/session_camera.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::StrictMock;

// HotplugMonitor is V4l2Session's own collaborator for #111; this fakes it,
// so V4l2SessionTest needs no real inotify watch. v4l2_hotplug_test.cc holds
// V4l2InotifyMonitor, the real one, to the same interface.
class MockHotplugMonitor : public HotplugMonitor {
 public:
  MOCK_METHOD(void, Start,
              (std::function<void(std::string)> on_disconnected,
               std::function<void(std::string)> on_connected),
              (override));
};

// The libv4l2 calls and the poll the session makes, as a mock. v4l2_ioctl's
// two requests get a method each.
class MockV4l2 {
 public:
  MOCK_METHOD(int, Open, (const char* file, int flags));
  MOCK_METHOD(int, Close, (int fd));
  MOCK_METHOD(int, QueryCap, (int fd, v4l2_capability* cap));
  MOCK_METHOD(int, SetFormat, (int fd, v4l2_format* format));
  MOCK_METHOD(int, SetRate, (int fd, v4l2_streamparm* rate));
  MOCK_METHOD(ssize_t, Read, (int fd, void* buffer, std::size_t size));
  MOCK_METHOD(int, Poll, (pollfd * fds, nfds_t count, int timeout));
};

MockV4l2* v4l2 = nullptr;

// Probed first, so the device-choice tests below do not have to mock every
// node V4l2Session tries before it (#13, item 10 of #61).
constexpr const char* kDevice = "/dev/video0";
constexpr const char* kBuiltInCard = "FaceTime HD Camera";
constexpr const char* kBuiltInBusInfo = "platform:soc-cam0";
constexpr int kFd = 7;
constexpr int kFramesPerSecond = 15;
constexpr std::uint64_t kAttempt = 4;
constexpr auto kPatience = std::chrono::seconds(10);
constexpr std::uint32_t kMaxDimension = 4096;
constexpr std::uint32_t kMaxImage = 64 * 1024 * 1024;
constexpr auto kCapture =
    static_cast<std::uint32_t>(V4L2_BUF_TYPE_VIDEO_CAPTURE);

// A frame the session handed on, copied while its pixels were valid.
struct Received {
  int width;
  int height;
  std::size_t bytes_per_row;
  std::vector<std::uint8_t> bgra;
  std::int64_t timestamp_ms;
  std::uint64_t attempt;
};

// The format a driver settles on, in RGB24. The parameters follow
// v4l2_pix_format's own field order.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
constexpr v4l2_pix_format Rgb(std::uint32_t width, std::uint32_t height,
                              std::uint32_t bytes_per_line,
                              std::uint32_t size) {
  v4l2_pix_format pix{};
  pix.width = width;
  pix.height = height;
  pix.pixelformat = V4L2_PIX_FMT_RGB24;
  pix.bytesperline = bytes_per_line;
  pix.sizeimage = size;
  return pix;
}

// A 2x2 RGB24 image with two bytes of padding at the end of each row.
constexpr std::uint32_t kSide = 2;
constexpr std::uint32_t kLine = 8;
constexpr v4l2_pix_format kSmall = Rgb(kSide, kSide, kLine, kLine* kSide);

// Answers VIDIOC_S_FMT with `pix`, as a driver adjusts the format it is given.
auto Settles(v4l2_pix_format pix) {
  return [pix](int, v4l2_format* format) {
    format->fmt.pix = pix;
    return 0;
  };
}

// Answers VIDIOC_QUERYCAP with `card` and `bus_info` (item 10 of #61), as a
// driver reports a node's identity.
auto Capability(std::string card, std::string bus_info) {
  return [card = std::move(card), bus_info = std::move(bus_info)](
             int, v4l2_capability* cap) {
    std::memset(cap, 0, sizeof(*cap));
    std::memcpy(cap->card, card.c_str(), card.size());
    std::memcpy(cap->bus_info, bus_info.c_str(), bus_info.size());
    return 0;
  };
}

// The built-in camera's own capabilities, as most tests' one device
// reports them.
auto BuiltInCapability() { return Capability(kBuiltInCard, kBuiltInBusInfo); }

// Fills the read buffer with `bytes` and returns how many there were. A
// failed allocation would end the test run, which is fine.
auto Delivers(const std::vector<std::uint8_t>& bytes) {
  // NOLINTNEXTLINE(bugprone-exception-escape)
  return [bytes](int, void* buffer, std::size_t) {
    std::memcpy(buffer, bytes.data(), bytes.size());
    return static_cast<ssize_t>(bytes.size());
  };
}

// The errno the fakes below leave for the call they are standing in for. A
// mock's action sets it, and the fake applies it once the mock has returned,
// so nothing in GoogleMock can change errno on the way out.
thread_local int failure = 0;

// Fails with `error`, as a system call does.
auto FailsWith(int error) {
  return [error] {
    failure = error;
    return -1;
  };
}

// A poll that waits a moment and finds nothing to read.
int NothingYet() {
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return 0;
}

// A poll that reports `events` on its one descriptor.
auto Reports(int events) {
  return [events](pollfd* fds, nfds_t, int) {
    fds[0].revents = static_cast<std::int16_t>(events);
    return 1;
  };
}

class V4l2SessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    v4l2 = &mock_;
    session_ = std::make_unique<V4l2Session>(
        /*device_id=*/"",
        [this](const Frame& frame) {
          const std::size_t size =
              frame.bytes_per_row * static_cast<std::size_t>(frame.height);
          const std::scoped_lock lock(mutex_);
          frames_.push_back(
              {.width = frame.width,
               .height = frame.height,
               .bytes_per_row = frame.bytes_per_row,
               .bgra = std::vector<std::uint8_t>(frame.bgra, frame.bgra + size),
               .timestamp_ms = frame.timestamp_ms,
               .attempt = frame.attempt});
          changed_.notify_all();
        },
        [this] {
          const std::scoped_lock lock(mutex_);
          ++failures_;
          changed_.notify_all();
        },
        [](const std::string&) {}, [](const std::string&) {}, hotplug_);
  }

  void TearDown() override {
    session_.reset();
    v4l2 = nullptr;
  }

  // Configures the session over kFd, which is closed when it stops. The
  // default choice (an empty device_id, #13) matches the first node
  // probed, kDevice, which reports the built-in camera's capabilities.
  void Configured() {
    EXPECT_CALL(mock_, Open(StrEq(kDevice), _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(BuiltInCapability());
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    ASSERT_THAT(session_->Configure(), Optional(std::string(kBuiltInBusInfo)));
  }

  // Expects the driver to settle on `pix` and take any frame rate.
  void Settling(v4l2_pix_format pix) {
    EXPECT_CALL(mock_, SetFormat(kFd, _)).WillOnce(Settles(pix));
    EXPECT_CALL(mock_, SetRate(kFd, _)).WillOnce(Return(0));
  }

  // Waits until `count` frames have arrived.
  bool WaitForFrames(std::size_t count) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, kPatience,
                             [&] { return frames_.size() >= count; });
  }

  // Waits until the session has reported a failure.
  bool WaitForFailure() {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, kPatience, [&] { return failures_ > 0; });
  }

  // Stops the session, and says how many failures it reported.
  int StoppedFailures() {
    session_->Stop();
    const std::scoped_lock lock(mutex_);
    return failures_;
  }

  ::testing::StrictMock<MockV4l2> mock_;
  // Nice, not strict: almost no test here cares that V4l2Session asks it to
  // watch (#111); StartWatchingWiresHotplugCallbacksToTheGivenOnes and
  // ConstructingDoesNotStartWatchingUntilToldTo below do, each with its own
  // strict instance.
  NiceMock<MockHotplugMonitor> hotplug_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<Received> frames_;
  int failures_ = 0;
  std::unique_ptr<V4l2Session> session_;
};

// #115: constructing a V4l2Session must not itself start the watcher
// thread, which could then call back into a sibling (such as Engine's
// camera_ in tools/daemon/main.cc) before its own constructor runs; a
// StrictMock's Start catches that regression at once.
TEST_F(V4l2SessionTest, ConstructingDoesNotStartWatchingUntilToldTo) {
  StrictMock<MockHotplugMonitor> hotplug;

  const V4l2Session session(
      /*device_id=*/"", [](const Frame&) {}, [] {}, [](const std::string&) {},
      [](const std::string&) {}, hotplug);
}

// specs/CameraLifecycle.tla's DeviceLost, DeviceBack, OtherDeviceLost and
// OtherDeviceBack (#111): StartWatching asks the given HotplugMonitor to
// start watching with exactly the callbacks the constructor was given, the
// way AvFoundationSession wires AVFoundation's own notifications straight
// to SessionCamera's OnDeviceDisconnected/OnDeviceConnected.
TEST_F(V4l2SessionTest, StartWatchingWiresHotplugCallbacksToTheGivenOnes) {
  StrictMock<MockHotplugMonitor> hotplug;
  std::function<void(std::string)> started_disconnected;
  std::function<void(std::string)> started_connected;
  EXPECT_CALL(hotplug, Start(_, _))
      .WillOnce([&](std::function<void(std::string)> on_disconnected,
                    std::function<void(std::string)> on_connected) {
        started_disconnected = std::move(on_disconnected);
        started_connected = std::move(on_connected);
      });
  std::optional<std::string> disconnected;
  std::optional<std::string> connected;

  V4l2Session session(
      /*device_id=*/"", [](const Frame&) {}, [] {},
      [&](const std::string& id) { disconnected = id; },
      [&](const std::string& id) { connected = id; }, hotplug);
  session.StartWatching();

  ASSERT_TRUE(started_disconnected);
  ASSERT_TRUE(started_connected);
  started_disconnected("lost");
  started_connected("back");
  EXPECT_THAT(disconnected, Optional(std::string("lost")));
  EXPECT_THAT(connected, Optional(std::string("back")));
}

TEST_F(V4l2SessionTest, PermissionIsLeftToTheDevice) {
  EXPECT_THAT(session_->CurrentPermission(),
              Eq(CaptureSession::Permission::kGranted));
  std::optional<bool> granted;
  session_->RequestPermission([&granted](bool answer) { granted = answer; });
  EXPECT_THAT(granted, Optional(true));
}

TEST_F(V4l2SessionTest, ReportsNoFrameRates) {
  EXPECT_THAT(session_->SupportedFrameRates(), IsEmpty());
}

TEST_F(V4l2SessionTest, ConfigureOpensTheDeviceWithoutBlocking) {
  EXPECT_CALL(mock_, Open(StrEq(kDevice), O_RDWR | O_NONBLOCK | O_CLOEXEC))
      .WillOnce(Return(kFd));
  EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(BuiltInCapability());
  EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));

  EXPECT_THAT(session_->Configure(), Optional(std::string(kBuiltInBusInfo)));
}

TEST_F(V4l2SessionTest, ConfigureGivesNothingWhenNoNodeOpens) {
  EXPECT_CALL(mock_, Open(_, _)).Times(AnyNumber()).WillRepeatedly(Return(-1));

  EXPECT_THAT(session_->Configure(), Eq(std::nullopt));
  EXPECT_FALSE(session_->Start(kFramesPerSecond, kAttempt));
}

TEST_F(V4l2SessionTest, ConfigureGivesNothingWhenNoNodeMatchesTheChoice) {
  session_ = std::make_unique<V4l2Session>(
      "usb-0000:00:14.0-1", [](const Frame&) {}, [] {},
      [](const std::string&) {}, [](const std::string&) {}, hotplug_);
  // The remaining three nodes (kProbedNodes in v4l2_session.cc) do not
  // open at all; declared first, so the more specific expectation below
  // matches the first call. Without RetiresOnSaturation, GoogleMock
  // never falls through to this one once the other is satisfied: a
  // saturated expectation with no explicit retirement stays active, and
  // a further matching call over-saturates it instead.
  EXPECT_CALL(mock_, Open(_, _)).Times(3).WillRepeatedly(Return(-1));
  EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd)).RetiresOnSaturation();
  EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(BuiltInCapability());
  EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));

  EXPECT_THAT(session_->Configure(), Eq(std::nullopt));
}

TEST_F(V4l2SessionTest, ConfigureSkipsANodeThatDoesNotQuery) {
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd + 1));
    EXPECT_CALL(mock_, QueryCap(kFd + 1, _)).WillOnce(BuiltInCapability());
  }
  // The match stays open until the session stops, in TearDown.
  EXPECT_CALL(mock_, Close(kFd + 1)).WillOnce(Return(0));

  EXPECT_THAT(session_->Configure(), Optional(std::string(kBuiltInBusInfo)));
}

TEST_F(V4l2SessionTest, ConfigureMatchesTheChosenDeviceAmongSeveral) {
  session_ = std::make_unique<V4l2Session>(
      "usb-0000:00:14.0-1", [](const Frame&) {}, [] {},
      [](const std::string&) {}, [](const std::string&) {}, hotplug_);
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(BuiltInCapability());
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd + 1));
    EXPECT_CALL(mock_, QueryCap(kFd + 1, _))
        .WillOnce(Capability("USB Camera", "usb-0000:00:14.0-1"));
  }
  // The match stays open until the session stops, in TearDown.
  EXPECT_CALL(mock_, Close(kFd + 1)).WillOnce(Return(0));

  EXPECT_THAT(session_->Configure(),
              Optional(std::string("usb-0000:00:14.0-1")));
}

TEST_F(V4l2SessionTest, ConfiguringAgainClosesTheDeviceFirst) {
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(BuiltInCapability());
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd + 1));
    EXPECT_CALL(mock_, QueryCap(kFd + 1, _)).WillOnce(BuiltInCapability());
    EXPECT_CALL(mock_, Close(kFd + 1)).WillOnce(Return(0));
  }

  EXPECT_THAT(session_->Configure(), Optional(std::string(kBuiltInBusInfo)));
  EXPECT_THAT(session_->Configure(), Optional(std::string(kBuiltInBusInfo)));
}

TEST_F(V4l2SessionTest, ListsEveryDiscoverableDevice) {
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(BuiltInCapability());
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd + 1));
    EXPECT_CALL(mock_, QueryCap(kFd + 1, _))
        .WillOnce(Capability("USB Camera", "usb-0000:00:14.0-1"));
    EXPECT_CALL(mock_, Close(kFd + 1)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(-1));
  }

  const std::vector<CameraDevice> devices = session_->List();
  ASSERT_THAT(devices, SizeIs(2));
  EXPECT_THAT(devices[0].id, Eq(kBuiltInBusInfo));
  EXPECT_THAT(devices[0].name, Eq(kBuiltInCard));
  EXPECT_TRUE(devices[0].built_in);
  EXPECT_THAT(devices[1].id, Eq("usb-0000:00:14.0-1"));
  EXPECT_THAT(devices[1].name, Eq("USB Camera"));
  EXPECT_FALSE(devices[1].built_in);
}

TEST_F(V4l2SessionTest, ListsNothingWhenNoNodeOpens) {
  EXPECT_CALL(mock_, Open(_, _)).Times(AnyNumber()).WillRepeatedly(Return(-1));

  EXPECT_THAT(session_->List(), IsEmpty());
}

TEST_F(V4l2SessionTest, ListSkipsANodeThatDoesNotQuery) {
  // Declared first (see ConfigureGivesNothingWhenNoNodeMatchesTheChoice),
  // so the sequenced block below matches the first call, then retires
  // rather than over-saturating on the later calls this one should
  // handle.
  EXPECT_CALL(mock_, Open(_, _)).Times(3).WillRepeatedly(Return(-1));
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd)).RetiresOnSaturation();
    EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
  }

  EXPECT_THAT(session_->List(), IsEmpty());
}

TEST_F(V4l2SessionTest, StartFailsWithoutADevice) {
  EXPECT_FALSE(session_->Start(kFramesPerSecond, kAttempt));
}

// Checks every field Start asks the driver to configure.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(V4l2SessionTest, StartAsksForVgaRgbAtTheFrameRate) {
  Configured();
  v4l2_format asked{};
  v4l2_streamparm rate{};
  EXPECT_CALL(mock_, SetFormat(kFd, _))
      .WillOnce([&asked](int fd, v4l2_format* format) {
        asked = *format;
        return Settles(kSmall)(fd, format);
      });
  EXPECT_CALL(mock_, SetRate(kFd, _))
      .WillOnce([&rate](int, v4l2_streamparm* given) {
        rate = *given;
        return -1;
      });
  EXPECT_CALL(mock_, Read(kFd, _, _)).WillRepeatedly(FailsWith(EAGAIN));
  EXPECT_CALL(mock_, Poll(_, _, _)).WillRepeatedly(NothingYet);

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  EXPECT_THAT(asked.type, Eq(kCapture));
  EXPECT_THAT(asked.fmt.pix.width, Eq(640U));
  EXPECT_THAT(asked.fmt.pix.height, Eq(480U));
  EXPECT_THAT(asked.fmt.pix.pixelformat, Eq(V4L2_PIX_FMT_RGB24));
  EXPECT_THAT(asked.fmt.pix.field,
              Eq(static_cast<std::uint32_t>(V4L2_FIELD_ANY)));
  EXPECT_THAT(rate.type, Eq(kCapture));
  EXPECT_THAT(rate.parm.capture.timeperframe.numerator, Eq(1U));
  EXPECT_THAT(rate.parm.capture.timeperframe.denominator,
              Eq(static_cast<std::uint32_t>(kFramesPerSecond)));
}

TEST_F(V4l2SessionTest, StartFailsWhileCapturing) {
  Configured();
  Settling(kSmall);
  EXPECT_CALL(mock_, Read(_, _, _)).WillRepeatedly(FailsWith(EAGAIN));
  EXPECT_CALL(mock_, Poll(_, _, _)).WillRepeatedly(NothingYet);

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  EXPECT_FALSE(session_->Start(kFramesPerSecond, kAttempt));
}

TEST_F(V4l2SessionTest, CopiesEachFrameAsBgraStampedWithItsAttempt) {
  const std::vector<std::uint8_t> small_rgb = {1, 2, 3, 4,  5,  6,  0xEE, 0xEE,
                                               7, 8, 9, 10, 11, 12, 0xEE, 0xEE};
  Configured();
  Settling(kSmall);
  EXPECT_CALL(mock_, Read(kFd, _, kSmall.sizeimage))
      .WillOnce(Delivers(small_rgb))
      .WillOnce(Delivers(small_rgb))
      .WillRepeatedly(FailsWith(EAGAIN));
  EXPECT_CALL(mock_, Poll(_, _, _)).WillRepeatedly(NothingYet);

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  ASSERT_TRUE(WaitForFrames(2));
  EXPECT_THAT(StoppedFailures(), Eq(0));

  const Received& first = frames_[0];
  EXPECT_THAT(first.width, Eq(2));
  EXPECT_THAT(first.height, Eq(2));
  EXPECT_THAT(first.bytes_per_row, Eq(8U));
  EXPECT_THAT(first.attempt, Eq(kAttempt));
  EXPECT_THAT(first.bgra, ElementsAre(3, 2, 1, 255, 6, 5, 4, 255,  //
                                      9, 8, 7, 255, 12, 11, 10, 255));
  EXPECT_THAT(frames_[1].timestamp_ms, Gt(first.timestamp_ms));
}

// Checks every field the short read's wait polls and times.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(V4l2SessionTest, AShortReadWaitsForTheDevice) {
  Configured();
  Settling(kSmall);
  pollfd waited{};
  int timeout = 0;
  {
    InSequence order;
    EXPECT_CALL(mock_, Read(_, _, _))
        .WillOnce(Return(static_cast<ssize_t>(kSmall.sizeimage - 1)));
    EXPECT_CALL(mock_, Poll(_, 1, _))
        .WillOnce([&](pollfd* fds, nfds_t, int wait) {
          waited = fds[0];
          timeout = wait;
          return 0;
        });
    EXPECT_CALL(mock_, Read(_, _, _)).WillOnce(FailsWith(EIO));
  }

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  ASSERT_TRUE(WaitForFailure());
  EXPECT_THAT(StoppedFailures(), Eq(1));
  EXPECT_THAT(waited.fd, Eq(kFd));
  EXPECT_THAT(waited.events, Eq(POLLIN));
  EXPECT_THAT(timeout, Eq(100));
  EXPECT_TRUE(frames_.empty());
}

TEST_F(V4l2SessionTest, AReadErrorEndsCaptureAndReportsAFailure) {
  Configured();
  Settling(kSmall);
  EXPECT_CALL(mock_, Read(_, _, _)).WillOnce(FailsWith(EIO));

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  ASSERT_TRUE(WaitForFailure());
  EXPECT_THAT(StoppedFailures(), Eq(1));
}

// A read that fails for a reason to wait out.
class ReadToWaitOutTest : public V4l2SessionTest,
                          public ::testing::WithParamInterface<int> {};

TEST_P(ReadToWaitOutTest, WaitsForTheDevice) {
  Configured();
  Settling(kSmall);
  {
    InSequence order;
    EXPECT_CALL(mock_, Read(_, _, _)).WillOnce(FailsWith(GetParam()));
    EXPECT_CALL(mock_, Poll(_, _, _)).WillOnce(Reports(POLLHUP));
  }

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  ASSERT_TRUE(WaitForFailure());
  EXPECT_THAT(StoppedFailures(), Eq(1));
}

INSTANTIATE_TEST_SUITE_P(EachReason, ReadToWaitOutTest,
                         ::testing::Values(EAGAIN, EINTR));

TEST_F(V4l2SessionTest, AnInterruptedWaitReadsAgain) {
  Configured();
  Settling(kSmall);
  {
    InSequence order;
    EXPECT_CALL(mock_, Read(_, _, _)).WillOnce(FailsWith(EAGAIN));
    EXPECT_CALL(mock_, Poll(_, _, _)).WillOnce(FailsWith(EINTR));
    EXPECT_CALL(mock_, Read(_, _, _)).WillOnce(FailsWith(EAGAIN));
    EXPECT_CALL(mock_, Poll(_, _, _)).WillOnce(Reports(POLLIN));
    EXPECT_CALL(mock_, Read(_, _, _)).WillOnce(FailsWith(EIO));
  }

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  ASSERT_TRUE(WaitForFailure());
  EXPECT_THAT(StoppedFailures(), Eq(1));
}

TEST_F(V4l2SessionTest, AFailedWaitEndsCaptureAndReportsAFailure) {
  Configured();
  Settling(kSmall);
  EXPECT_CALL(mock_, Read(_, _, _)).WillOnce(FailsWith(EAGAIN));
  EXPECT_CALL(mock_, Poll(_, _, _)).WillOnce(FailsWith(EBADF));

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  ASSERT_TRUE(WaitForFailure());
  EXPECT_THAT(StoppedFailures(), Eq(1));
}

// A poll event that means the device has gone.
class DeviceGoneTest : public V4l2SessionTest,
                       public ::testing::WithParamInterface<int> {};

TEST_P(DeviceGoneTest, EndsCaptureAndReportsAFailure) {
  Configured();
  Settling(kSmall);
  EXPECT_CALL(mock_, Read(_, _, _)).WillOnce(FailsWith(EAGAIN));
  EXPECT_CALL(mock_, Poll(_, _, _)).WillOnce(Reports(GetParam()));

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  ASSERT_TRUE(WaitForFailure());
  EXPECT_THAT(StoppedFailures(), Eq(1));
}

INSTANTIATE_TEST_SUITE_P(EachEvent, DeviceGoneTest,
                         ::testing::Values(POLLERR, POLLHUP, POLLNVAL));

TEST_F(V4l2SessionTest, StoppingEndsCaptureWithoutAFailure) {
  Configured();
  Settling(kSmall);
  EXPECT_CALL(mock_, Read(_, _, _)).WillRepeatedly(FailsWith(EAGAIN));
  EXPECT_CALL(mock_, Poll(_, _, _)).WillRepeatedly(NothingYet);

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  EXPECT_THAT(StoppedFailures(), Eq(0));
  EXPECT_FALSE(session_->Start(kFramesPerSecond, kAttempt));
}

TEST_F(V4l2SessionTest, DestroyingTheSessionStopsCapture) {
  Configured();
  Settling(kSmall);
  EXPECT_CALL(mock_, Read(_, _, _)).WillRepeatedly(FailsWith(EAGAIN));
  EXPECT_CALL(mock_, Poll(_, _, _)).WillRepeatedly(NothingYet);
  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));

  session_.reset();
  EXPECT_THAT(failures_, Eq(0));
}

// A format the driver settles on that the session cannot capture in.
struct BadFormat {
  std::string name;
  v4l2_pix_format pix;
  int answer = 0;

  friend void PrintTo(const BadFormat& format, std::ostream* out) {
    *out << format.name;
  }
};

// kSmall with one field changed.
v4l2_pix_format Small(std::uint32_t v4l2_pix_format::* field,
                      std::uint32_t value) {
  v4l2_pix_format pix = kSmall;
  pix.*field = value;
  return pix;
}

class BadFormatTest : public V4l2SessionTest,
                      public ::testing::WithParamInterface<BadFormat> {};

TEST_P(BadFormatTest, StartClosesTheDeviceAndFails) {
  Configured();
  EXPECT_CALL(mock_, SetFormat(kFd, _))
      .WillOnce([bad = GetParam()](int fd, v4l2_format* format) {
        return Settles(bad.pix)(fd, format) + bad.answer;
      });

  EXPECT_FALSE(session_->Start(kFramesPerSecond, kAttempt));
  EXPECT_FALSE(session_->Start(kFramesPerSecond, kAttempt));
}

INSTANTIATE_TEST_SUITE_P(
    EachCheck, BadFormatTest,
    ::testing::Values(
        BadFormat{.name = "Refused", .pix = kSmall, .answer = -1},
        BadFormat{
            .name = "NotRgb",
            .pix = Small(&v4l2_pix_format::pixelformat, V4L2_PIX_FMT_YUYV)},
        BadFormat{.name = "NoWidth", .pix = Small(&v4l2_pix_format::width, 0)},
        BadFormat{.name = "TooWide",
                  .pix = Rgb(kMaxDimension + 1, 1, (kMaxDimension + 1) * 3,
                             (kMaxDimension + 1) * 3)},
        BadFormat{.name = "NoHeight",
                  .pix = Small(&v4l2_pix_format::height, 0)},
        BadFormat{.name = "TooTall",
                  .pix = Rgb(1, kMaxDimension + 1, 3, 3 * (kMaxDimension + 1))},
        BadFormat{
            .name = "LinesTooShort",
            .pix = Small(&v4l2_pix_format::bytesperline, (kSide * 3) - 1)},
        BadFormat{
            .name = "ImageTooSmall",
            .pix = Small(&v4l2_pix_format::sizeimage, (kLine * kSide) - 1)},
        BadFormat{.name = "ImageTooLarge",
                  .pix = Rgb(kMaxDimension, kMaxDimension, kMaxDimension * 3,
                             kMaxImage + 1)}),
    [](const ::testing::TestParamInfo<BadFormat>& info) {
      return info.param.name;
    });

// A format at one of the limits the session allows.
struct Limit {
  std::string name;
  v4l2_pix_format pix;

  friend void PrintTo(const Limit& limit, std::ostream* out) {
    *out << limit.name;
  }
};

class LimitTest : public V4l2SessionTest,
                  public ::testing::WithParamInterface<Limit> {};

TEST_P(LimitTest, StartCapturesInIt) {
  Configured();
  Settling(GetParam().pix);
  EXPECT_CALL(mock_, Read(_, _, GetParam().pix.sizeimage))
      .WillOnce(FailsWith(EIO));

  ASSERT_TRUE(session_->Start(kFramesPerSecond, kAttempt));
  ASSERT_TRUE(WaitForFailure());
  EXPECT_THAT(StoppedFailures(), Eq(1));
}

INSTANTIATE_TEST_SUITE_P(
    EachLimit, LimitTest,
    ::testing::Values(Limit{.name = "Widest",
                            .pix = Rgb(kMaxDimension, 1, kMaxDimension * 3,
                                       kMaxDimension * 3)},
                      Limit{
                          .name = "Tallest",
                          .pix = Rgb(1, kMaxDimension, 3, 3 * kMaxDimension)}),
    [](const ::testing::TestParamInfo<Limit>& info) {
      return info.param.name;
    });

}  // namespace
}  // namespace facepalm

// The link-time seam: libv4l2 and poll, forwarding to the mock.
int v4l2_open(const char* file, int oflag, ...) {
  return facepalm::v4l2->Open(file, oflag);
}

int v4l2_close(int fd) { return facepalm::v4l2->Close(fd); }

// libv4l2's own signature.
// NOLINTNEXTLINE(google-runtime-int,bugprone-easily-swappable-parameters)
int v4l2_ioctl(int fd, unsigned long int request, ...) {
  std::va_list arguments;
  va_start(arguments, request);
  void* argument = va_arg(arguments, void*);
  va_end(arguments);
  switch (request) {
    case VIDIOC_S_FMT:
      return facepalm::v4l2->SetFormat(fd, static_cast<v4l2_format*>(argument));
    case VIDIOC_S_PARM:
      return facepalm::v4l2->SetRate(fd,
                                     static_cast<v4l2_streamparm*>(argument));
    case VIDIOC_QUERYCAP:
      return facepalm::v4l2->QueryCap(fd,
                                      static_cast<v4l2_capability*>(argument));
    default:
      ADD_FAILURE() << "unexpected ioctl " << request;
      return -1;
  }
}

ssize_t v4l2_read(int fd, void* buffer, size_t n) {
  const ssize_t count = facepalm::v4l2->Read(fd, buffer, n);
  errno = facepalm::failure;
  return count;
}

int poll(pollfd* fds, nfds_t nfds, int timeout) {
  const int ready = facepalm::v4l2->Poll(fds, nfds, timeout);
  errno = facepalm::failure;
  return ready;
}
