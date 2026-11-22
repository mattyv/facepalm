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
#include <string>
#include <utility>
#include <vector>

namespace facepalm {
namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kMaxDimension = 4096;
constexpr std::size_t kMaxImage = std::size_t{64} * 1024 * 1024;
constexpr std::uint8_t kOpaqueAlpha = 255;
// /dev/video0 through /dev/video3 (item 10 of #61): most systems put each
// camera on a low-numbered node, and opendir has no seam for a test to
// replace, as v4l2_open, v4l2_ioctl and v4l2_close do, so this probes a
// bounded range of names instead of listing the directory.
constexpr int kProbedNodes = 4;

// A v4l2_capability's own bus_info identifies the physical device: it
// stays the same across a reboot, unlike the node number a camera happens
// to land on. "platform:" names one wired directly to the board, as
// opposed to "usb-..." for one plugged in (#13).
bool IsBuiltIn(const std::string& bus_info) {
  return bus_info.starts_with("platform:");
}

// Queries `fd`'s capabilities as a CameraDevice, or nothing when the ioctl
// fails.
std::optional<CameraDevice> Queried(int fd) {
  v4l2_capability cap{};
  if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
    return std::nullopt;
  }
  const std::string bus_info(reinterpret_cast<const char*>(cap.bus_info));
  return CameraDevice{
      .id = bus_info,
      .name = std::string(reinterpret_cast<const char*>(cap.card)),
      .built_in = IsBuiltIn(bus_info),
  };
}

// The path of the node at `index`, from 0 to kProbedNodes - 1.
std::string NodePath(int index) { return "/dev/video" + std::to_string(index); }

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

// specs/CameraLifecycle.tla's DeviceLost, DeviceBack, OtherDeviceLost and
// OtherDeviceBack (#111): `hotplug` is asked to watch for them, the way
// AVFoundation's own notifications already do for AvFoundationSession.
// Starting the watch itself waits for StartWatching (#115).
V4l2Session::V4l2Session(
    std::string device_id, std::function<void(const Frame&)> on_frame,
    std::function<void()> on_session_failed,
    std::function<void(const std::string&)> on_disconnected,
    std::function<void(const std::string&)> on_connected,
    HotplugMonitor& hotplug)
    : device_id_(std::move(device_id)),
      on_frame_(std::move(on_frame)),
      on_session_failed_(std::move(on_session_failed)),
      on_disconnected_(std::move(on_disconnected)),
      on_connected_(std::move(on_connected)),
      hotplug_(hotplug) {}

V4l2Session::~V4l2Session() { Stop(); }

// #115: deferred from the constructor, so the watcher thread this starts
// cannot call back into a sibling the constructor's own caller had not
// yet constructed.
void V4l2Session::StartWatching() {
  hotplug_.Start(std::move(on_disconnected_), std::move(on_connected_));
}

// Configure checks device permissions on the first approved open attempt.
CaptureSession::Permission V4l2Session::CurrentPermission() {
  return Permission::kGranted;
}

void V4l2Session::RequestPermission(std::function<void(bool)> done) {
  done(true);
}

// Probes each node in turn (item 10 of #61), keeping the first one open
// whose capabilities match the configured choice: the first built-in
// camera found, with none chosen, or the one whose id equals device_id_.
// A node that opens but does not match, or does not query, is closed
// before the next is tried.
std::optional<std::string> V4l2Session::Configure() {
  Stop();
  for (int index = 0; index < kProbedNodes; ++index) {
    const int fd =
        v4l2_open(NodePath(index).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }
    const std::optional<CameraDevice> device = Queried(fd);
    const bool matches =
        device.has_value() &&
        (device_id_.empty() ? device->built_in : device->id == device_id_);
    if (matches) {
      fd_ = fd;
      return device->id;
    }
    v4l2_close(fd);
  }
  return std::nullopt;
}

CaptureSession::FrameRates V4l2Session::SupportedFrameRates() { return {}; }

// Every camera V4l2Session can discover right now (item 10 of #61),
// independent of which one is configured; safe to call from any thread, as
// CameraDirectory documents.
std::vector<CameraDevice> V4l2Session::List() {
  std::vector<CameraDevice> devices;
  for (int index = 0; index < kProbedNodes; ++index) {
    const int fd =
        v4l2_open(NodePath(index).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }
    if (const std::optional<CameraDevice> device = Queried(fd)) {
      devices.push_back(*device);
    }
    v4l2_close(fd);
  }
  return devices;
}

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
