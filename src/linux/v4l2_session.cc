#include "src/linux/v4l2_session.h"

#include <fcntl.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace facepalm {
namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kMaxDimension = 4096;
constexpr std::size_t kMaxImage = std::size_t{64} * 1024 * 1024;
constexpr std::uint8_t kOpaqueAlpha = 255;

void CopyBgra(const v4l2_pix_format& format, const std::uint8_t* rgb,
              std::uint8_t* bgra) {
  for (std::size_t y = 0; y < format.height; ++y) {
    const auto* row = rgb + (y * format.bytesperline);
    for (std::size_t x = 0; x < format.width; ++x) {
      *bgra++ = row[(3 * x) + 2];
      *bgra++ = row[(3 * x) + 1];
      *bgra++ = row[3 * x];
      *bgra++ = kOpaqueAlpha;
    }
  }
}

}  // namespace

V4l2Session::V4l2Session(std::string device,
                         std::function<void(const Frame&)> on_frame,
                         std::function<void()> on_session_failed)
    : device_(std::move(device)),
      on_frame_(std::move(on_frame)),
      on_session_failed_(std::move(on_session_failed)) {}

V4l2Session::~V4l2Session() { Stop(); }

// Configure checks device permissions on the first approved open attempt.
CaptureSession::Permission V4l2Session::CurrentPermission() {
  return Permission::kGranted;
}

void V4l2Session::RequestPermission(std::function<void(bool)> done) {
  done(true);
}

std::optional<std::string> V4l2Session::Configure() {
  Stop();
  fd_ = v4l2_open(device_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  return fd_ >= 0 ? std::optional(device_) : std::nullopt;
}

CaptureSession::FrameRates V4l2Session::SupportedFrameRates() { return {}; }

// CaptureSession::Start's own signature, shared with the macOS session.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool V4l2Session::Start(int frames_per_second, std::uint64_t attempt) {
  if (fd_ < 0 || capture_.joinable()) {
    return false;
  }
  v4l2_format format{};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = kWidth;
  format.fmt.pix.height = kHeight;
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
  format.fmt.pix.field = V4L2_FIELD_ANY;
  const int configured = v4l2_ioctl(fd_, VIDIOC_S_FMT, &format);
  const auto pix = format.fmt.pix;
  const auto min_bytes =
      static_cast<std::size_t>(pix.bytesperline) * pix.height;
  if (configured < 0 || pix.pixelformat != V4L2_PIX_FMT_RGB24 ||
      pix.width == 0 || pix.width > kMaxDimension || pix.height == 0 ||
      pix.height > kMaxDimension || pix.bytesperline < pix.width * 3 ||
      pix.sizeimage < min_bytes || pix.sizeimage > kMaxImage) {
    Stop();
    return false;
  }
  v4l2_streamparm rate{};
  rate.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  rate.parm.capture.timeperframe.numerator = 1;
  rate.parm.capture.timeperframe.denominator = frames_per_second;
  static_cast<void>(v4l2_ioctl(fd_, VIDIOC_S_PARM, &rate));
  const int fd = fd_;
  capture_ = std::jthread([this, fd, pix, min_bytes,
                           attempt](const std::stop_token& stop) {
    std::vector<std::uint8_t> rgb(pix.sizeimage);
    const std::size_t stride = static_cast<std::size_t>(pix.width) * 4;
    std::vector<std::uint8_t> bgra(stride * pix.height);
    std::int64_t last = 0;
    while (!stop.stop_requested()) {
      const auto count = v4l2_read(fd, rgb.data(), rgb.size());
      if (std::cmp_greater_equal(count, min_bytes)) {
        CopyBgra(pix, rgb.data(), bgra.data());
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
        last = std::max(last + 1, now.count());
        on_frame_({.width = static_cast<int>(pix.width),
                   .height = static_cast<int>(pix.height),
                   .bytes_per_row = stride,
                   .bgra = bgra.data(),
                   .timestamp_ms = last,
                   .attempt = attempt});
      } else {
        if (count < 0 && errno != EAGAIN && errno != EINTR) {
          break;
        }
        pollfd input{.fd = fd, .events = POLLIN, .revents = 0};
        const int ready = poll(&input, 1, 100);
        constexpr std::uint16_t kGone = POLLERR | POLLHUP | POLLNVAL;
        if ((ready < 0 && errno != EINTR) ||
            (static_cast<std::uint16_t>(input.revents) & kGone) != 0) {
          break;
        }
      }
    }
    std::ranges::fill(rgb, 0);
    std::ranges::fill(bgra, 0);
    if (!stop.stop_requested()) {
      on_session_failed_();
    }
  });
  return true;
}

void V4l2Session::Stop() {
  if (capture_.joinable()) {
    capture_.request_stop();
    capture_.join();
  }
  if (fd_ >= 0) {
    v4l2_close(std::exchange(fd_, -1));
  }
}

}  // namespace facepalm
