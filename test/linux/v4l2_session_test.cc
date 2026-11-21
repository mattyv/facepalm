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
#include "src/session_camera.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::Optional;
using ::testing::Return;
using ::testing::StrEq;

// The libv4l2 calls and the poll the session makes, as a mock. v4l2_ioctl's
// two requests get a method each.
class MockV4l2 {
 public:
  MOCK_METHOD(int, Open, (const char* file, int flags));
  MOCK_METHOD(int, Close, (int fd));
  MOCK_METHOD(int, SetFormat, (int fd, v4l2_format* format));
  MOCK_METHOD(int, SetRate, (int fd, v4l2_streamparm* rate));
  MOCK_METHOD(ssize_t, Read, (int fd, void* buffer, std::size_t size));
  MOCK_METHOD(int, Poll, (pollfd * fds, nfds_t count, int timeout));
};

MockV4l2* v4l2 = nullptr;

constexpr char kDevice[] = "/dev/video9";
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

// The format a driver settles on, in RGB24.
v4l2_pix_format Rgb(std::uint32_t width, std::uint32_t height,
                    std::uint32_t bytes_per_line, std::uint32_t size) {
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
const v4l2_pix_format kSmall = Rgb(kSide, kSide, kLine, kLine* kSide);
const std::vector<std::uint8_t> kSmallRgb = {1, 2, 3, 4,  5,  6,  0xEE, 0xEE,
                                             7, 8, 9, 10, 11, 12, 0xEE, 0xEE};

// Answers VIDIOC_S_FMT with `pix`, as a driver adjusts the format it is given.
auto Settles(v4l2_pix_format pix) {
  return [pix](int, v4l2_format* format) {
    format->fmt.pix = pix;
    return 0;
  };
}

// Fills the read buffer with `bytes` and returns how many there were.
auto Delivers(const std::vector<std::uint8_t>& bytes) {
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
        kDevice,
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
        });
  }

  void TearDown() override {
    session_.reset();
    v4l2 = nullptr;
  }

  // Configures the session over kFd, which is closed when it stops.
  void Configured() {
    EXPECT_CALL(mock_, Open(StrEq(kDevice), _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    ASSERT_THAT(session_->Configure(), Optional(std::string(kDevice)));
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
  std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<Received> frames_;
  int failures_ = 0;
  std::unique_ptr<V4l2Session> session_;
};

TEST_F(V4l2SessionTest, PermissionIsLeftToTheDevice) {
  EXPECT_THAT(session_->CurrentPermission(),
              Eq(CaptureSession::Permission::kGranted));
  std::optional<bool> granted;
  session_->RequestPermission([&granted](bool answer) { granted = answer; });
  EXPECT_THAT(granted, Optional(true));
}

TEST_F(V4l2SessionTest, ReportsNoFrameRates) {
  const auto rates = session_->SupportedFrameRates();
  EXPECT_THAT(rates.min, Eq(0));
  EXPECT_THAT(rates.max, Eq(0));
}

TEST_F(V4l2SessionTest, ConfigureOpensTheDeviceWithoutBlocking) {
  EXPECT_CALL(mock_, Open(StrEq(kDevice), O_RDWR | O_NONBLOCK | O_CLOEXEC))
      .WillOnce(Return(kFd));
  EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));

  EXPECT_THAT(session_->Configure(), Optional(std::string(kDevice)));
}

TEST_F(V4l2SessionTest, ConfigureGivesNothingWhenTheDeviceDoesNotOpen) {
  EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(-1));

  EXPECT_THAT(session_->Configure(), Eq(std::nullopt));
  EXPECT_FALSE(session_->Start(kFramesPerSecond, kAttempt));
}

TEST_F(V4l2SessionTest, ConfiguringAgainClosesTheDeviceFirst) {
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(_, _)).WillOnce(Return(kFd + 1));
    EXPECT_CALL(mock_, Close(kFd + 1)).WillOnce(Return(0));
  }

  EXPECT_THAT(session_->Configure(), Optional(std::string(kDevice)));
  EXPECT_THAT(session_->Configure(), Optional(std::string(kDevice)));
}

TEST_F(V4l2SessionTest, StartFailsWithoutADevice) {
  EXPECT_FALSE(session_->Start(kFramesPerSecond, kAttempt));
}

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
  Configured();
  Settling(kSmall);
  EXPECT_CALL(mock_, Read(kFd, _, kSmall.sizeimage))
      .WillOnce(Delivers(kSmallRgb))
      .WillOnce(Delivers(kSmallRgb))
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
        BadFormat{.name = "LinesTooShort",
                  .pix = Small(&v4l2_pix_format::bytesperline, kSide * 3 - 1)},
        BadFormat{.name = "ImageTooSmall",
                  .pix = Small(&v4l2_pix_format::sizeimage, kLine* kSide - 1)},
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

// NOLINTNEXTLINE(google-runtime-int): libv4l2's own signature.
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
