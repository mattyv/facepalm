#include "src/linux/v4l2_session.h"

#include <fcntl.h>
#include <glob.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <poll.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <regex>
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
// The bus remains the public identifier for compatibility with saved choices.
bool IsBuiltIn(const std::string& bus_info) {
  return bus_info.starts_with("platform:");
}

std::vector<std::string> NodePaths() {
  glob_t matches{};
  const int result = glob("/dev/video[0-9]*", 0, nullptr, &matches);
  std::vector<std::string> paths;
  if (result == 0) {
    for (std::size_t i = 0; i < matches.gl_pathc; ++i) {
      const std::string path(matches.gl_pathv[i]);
      const auto suffix = path.substr(std::strlen("/dev/video"));
      if (std::ranges::all_of(
              suffix, [](unsigned char c) { return std::isdigit(c) != 0; })) {
        paths.push_back(path);
      }
    }
  } else if (result != GLOB_NOMATCH) {
    std::fprintf(stderr, "camera enumeration failed: glob=%d\n", result);
  }
  globfree(&matches);
  return paths;
}

// These single-plane RGB, JPEG and YUV formats are supported by libv4lconvert.
// Unknown and luma-only formats are not evidence of color; format labels do
// not establish which wavelengths a sensor captures. See the kernel's
// pixfmt-yuv-luma documentation and v4l-utils libv4lconvert.c.
bool KnownColorFormat(std::uint32_t format) {
  constexpr std::array<std::uint32_t, 10> kColorFormats = {
      V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_MJPEG,
      V4L2_PIX_FMT_JPEG,  V4L2_PIX_FMT_YUYV,  V4L2_PIX_FMT_UYVY,
      V4L2_PIX_FMT_YVYU,  V4L2_PIX_FMT_NV12,  V4L2_PIX_FMT_YUV420,
      V4L2_PIX_FMT_YVU420};
  return std::ranges::find(kColorFormats, format) != kColorFormats.end();
}

std::optional<CameraDevice> Queried(
    int fd, const std::string& path,
    std::optional<CaptureSession::ConfigureError>* error) {
  v4l2_capability cap{};
  if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
    if (error != nullptr) {
      *error = CaptureSession::ConfigureError::kQueryFailed;
    }
    std::fprintf(stderr, "camera %s: QUERYCAP failed errno=%d\n", path.c_str(),
                 errno);
    return std::nullopt;
  }
  const std::string bus(reinterpret_cast<const char*>(cap.bus_info),
                        strnlen(reinterpret_cast<const char*>(cap.bus_info),
                                sizeof(cap.bus_info)));
  const std::string name(
      reinterpret_cast<const char*>(cap.card),
      strnlen(reinterpret_cast<const char*>(cap.card), sizeof(cap.card)));
  const auto caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0
                        ? cap.device_caps
                        : cap.capabilities;
  std::fprintf(stderr,
               "camera %s: bus=%s name=%s capabilities=0x%x device_caps=0x%x\n",
               path.c_str(), bus.c_str(), name.c_str(), cap.capabilities,
               cap.device_caps);
  if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0 ||
      (caps & (static_cast<std::uint32_t>(V4L2_CAP_READWRITE) |
               static_cast<std::uint32_t>(V4L2_CAP_STREAMING))) == 0) {
    if (error != nullptr) {
      *error = CaptureSession::ConfigureError::kUnsupported;
    }
    std::fprintf(stderr,
                 "camera %s: rejected: not a readable video capture node\n",
                 path.c_str());
    return std::nullopt;
  }
  std::string lower = name;
  std::ranges::transform(lower, lower.begin(),
                         [](unsigned char c) { return std::tolower(c); });
  static const std::regex k_infrared(
      R"((^|[^a-z0-9])(ir|infrared)([^a-z0-9]|$))");
  if (std::regex_search(lower, k_infrared)) {
    if (error != nullptr) {
      *error = CaptureSession::ConfigureError::kUnsupported;
    }
    std::fprintf(stderr, "camera %s: rejected: infrared name\n", path.c_str());
    return std::nullopt;
  }
  bool color = false;
  v4l2_fmtdesc format{};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  while (v4l2_ioctl(fd, VIDIOC_ENUM_FMT, &format) == 0) {
    const auto f = format.pixelformat;
    std::fprintf(
        stderr, "camera %s: format=0x%08x description=%.32s flags=0x%x\n",
        path.c_str(), f, reinterpret_cast<const char*>(format.description),
        format.flags);
    if ((format.flags & V4L2_FMT_FLAG_EMULATED) == 0 && KnownColorFormat(f)) {
      color = true;
    }
    ++format.index;
  }
  if (!color) {
    if (error != nullptr) {
      *error = CaptureSession::ConfigureError::kUnsupported;
    }
    std::fprintf(stderr,
                 "camera %s: rejected: no recognized native color format "
                 "(unknown and monochrome formats excluded)\n",
                 path.c_str());
    return std::nullopt;
  }
  v4l2_format rgb{};
  rgb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  rgb.fmt.pix.width = kWidth;
  rgb.fmt.pix.height = kHeight;
  rgb.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
  rgb.fmt.pix.field = V4L2_FIELD_ANY;
  const int tried = v4l2_ioctl(fd, VIDIOC_TRY_FMT, &rgb);
  const int try_error = errno;
  if (tried != 0 && (try_error == ENOTTY || try_error == EINVAL)) {
    std::fprintf(stderr,
                 "camera %s: TRY_FMT unsupported errno=%d; capture will "
                 "negotiate RGB24\n",
                 path.c_str(), try_error);
  } else if (tried != 0 || rgb.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB24) {
    if (error != nullptr) {
      *error = CaptureSession::ConfigureError::kUnsupported;
    }
    std::fprintf(stderr,
                 "camera %s: rejected: RGB24 conversion unavailable errno=%d\n",
                 path.c_str(), try_error);
    return std::nullopt;
  }
  return CameraDevice{.id = bus, .name = name, .built_in = IsBuiltIn(bus)};
}

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

// Probes discovered nodes, keeping the first usable color capture node
// matching the configured choice, or the first usable node by default.
// A node that opens but does not match, or does not query, is closed
// before the next is tried.
std::optional<std::string> V4l2Session::Configure() {
  Stop();
  configure_error_ = ConfigureError::kNoCandidate;
  for (const auto& path : NodePaths()) {
    const int fd = v4l2_open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      if (errno == EACCES || errno == EPERM) {
        configure_error_ = ConfigureError::kPermissionDenied;
      }
      std::fprintf(stderr, "camera %s: open failed errno=%d\n", path.c_str(),
                   errno);
      continue;
    }
    std::optional<ConfigureError> node_error;
    const auto device = Queried(fd, path, &node_error);
    if (node_error && configure_error_ != ConfigureError::kPermissionDenied) {
      configure_error_ = node_error;
    }
    if (device && (device_id_.empty() || device->id == device_id_)) {
      fd_ = fd;
      configure_error_.reset();
      return device->id;
    }
    if (device) {
      std::fprintf(stderr, "camera %s: rejected: device id does not match\n",
                   path.c_str());
    }
    v4l2_close(fd);
  }
  return std::nullopt;
}

std::optional<CaptureSession::ConfigureError> V4l2Session::LastConfigureError()
    const {
  return configure_error_;
}

CaptureSession::FrameRates V4l2Session::SupportedFrameRates() { return {}; }

// Every camera V4l2Session can discover right now (item 10 of #61),
// independent of which one is configured; safe to call from any thread, as
// CameraDirectory documents.
std::vector<CameraDevice> V4l2Session::List() {
  std::vector<CameraDevice> devices;
  for (const auto& path : NodePaths()) {
    const int fd = v4l2_open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      std::fprintf(stderr, "camera %s: open failed errno=%d\n", path.c_str(),
                   errno);
      continue;
    }
    if (const auto device = Queried(fd, path, nullptr)) {
      if (std::ranges::none_of(devices, [&](const auto& existing) {
            return existing.id == device->id;
          })) {
        devices.push_back(*device);
      }
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
