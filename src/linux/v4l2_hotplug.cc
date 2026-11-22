#include "src/linux/v4l2_hotplug.h"

#include <fcntl.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace facepalm {
namespace {

// The four nodes item 10 of #61 already chose to probe for Configure and
// List (docs/decisions.md): most systems put each camera on a low-numbered
// node, and opendir has no seam a test can replace, as v4l2_open,
// v4l2_ioctl and v4l2_close do (duplicated from v4l2_session.cc rather than
// shared, so each file's test covers its own copy alone, #111).
constexpr int kProbedNodes = 4;
constexpr const char* kDevDirectory = "/dev";
// How long one wait for an inotify event blocks before checking
// stop_requested again, the same way SessionCamera's capture loop polls
// its device at a bounded interval (100ms there; this one need not notice
// quite as fast).
constexpr int kPollIntervalMs = 200;
constexpr std::size_t kEventBufferSize = 4096;

std::string NodePath(int index) { return "/dev/video" + std::to_string(index); }
std::string NodeName(int index) { return "video" + std::to_string(index); }

// The probed node `name` names, or nothing when it is not one of them: an
// unrelated entry under /dev is not a camera this session could ever pick.
std::optional<int> ProbedNode(const std::string& name) {
  for (int index = 0; index < kProbedNodes; ++index) {
    if (name == NodeName(index)) {
      return index;
    }
  }
  return std::nullopt;
}

// The node at `path`'s id (its v4l2_capability's own bus_info, #13), or
// nothing when it does not open or query: the same two reasons Configure
// and List already skip a node for.
std::optional<std::string> IdOf(const std::string& path) {
  const int fd = v4l2_open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    return std::nullopt;
  }
  v4l2_capability cap{};
  const bool queried = v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
  v4l2_close(fd);
  if (!queried) {
    return std::nullopt;
  }
  return std::string(reinterpret_cast<const char*>(cap.bus_info));
}

// Handles one inotify_event at the start of `event`: a probed node
// appearing or disappearing fires `on_connected`/`on_disconnected` with
// its id, exactly as Watch's own doc comment describes; anything else is
// ignored. Returns how many bytes the event occupied, so Watch can
// advance past it.
std::size_t HandleEvent(const std::byte* event,
                        std::map<std::string, std::string>& known,
                        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                        const std::function<void(std::string)>& on_disconnected,
                        const std::function<void(std::string)>& on_connected) {
  inotify_event header{};
  std::memcpy(&header, event, sizeof(header));
  const auto* name = reinterpret_cast<const char*>(event + sizeof(header));
  if (const std::optional<int> index = ProbedNode(name)) {
    const std::string path = NodePath(*index);
    if ((header.mask & IN_CREATE) != 0) {
      if (const std::optional<std::string> id = IdOf(path)) {
        known[path] = *id;
        on_connected(*id);
      }
    } else if ((header.mask & IN_DELETE) != 0) {
      const auto found = known.find(path);
      if (found != known.end()) {
        on_disconnected(found->second);
        known.erase(found);
      }
    } else if ((header.mask & IN_ATTRIB) != 0 && !known.contains(path)) {
      // udev applies a device node's permissions some moments after
      // creating it, so the open IN_CREATE tried may have found it not
      // yet readable (#115): IN_ATTRIB's own change in those permissions
      // is the retry, tried only for a node not already known, so one
      // already connected is not reopened or told about twice.
      if (const std::optional<std::string> id = IdOf(path)) {
        known[path] = *id;
        on_connected(*id);
      }
    }
  }
  return sizeof(header) + header.len;
}

}  // namespace

void V4l2InotifyMonitor::Start(std::function<void(std::string)> on_disconnected,
                               std::function<void(std::string)> on_connected) {
  watcher_ = std::jthread(
      [on_disconnected = std::move(on_disconnected),
       on_connected = std::move(on_connected)](const std::stop_token& stop) {
        Watch(stop, on_disconnected, on_connected);
      });
}

// specs/CameraLifecycle.tla's DeviceLost, DeviceBack, OtherDeviceLost and
// OtherDeviceBack (#111): watches /dev for a probed node appearing or
// disappearing, since Configure and List only see what is attached right
// now, with no notification for a change afterward, unlike AVFoundation's
// own observers on macOS. Only a change after this call fires a callback
// (`known` is seeded from an initial scan, silently); after that, a node
// that opens and queries fires `on_connected`, and one this watcher had
// identified disappearing fires `on_disconnected` with the same id, so
// SessionCamera's own OnDeviceConnected/OnDeviceDisconnected can tell
// whether it is the camera in use the same way it already does for macOS
// (#13). A node IN_CREATE could not yet open gets one retry, on IN_ATTRIB,
// since udev applies its permissions a moment after creating it (#115).
void V4l2InotifyMonitor::Watch(
    const std::stop_token& stop,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const std::function<void(std::string)>& on_disconnected,
    const std::function<void(std::string)>& on_connected) {
  const int notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (notify < 0) {
    return;
  }
  if (inotify_add_watch(notify, kDevDirectory,
                        IN_CREATE | IN_DELETE | IN_ATTRIB) < 0) {
    close(notify);
    return;
  }
  std::map<std::string, std::string> known;
  for (int index = 0; index < kProbedNodes; ++index) {
    if (const std::optional<std::string> id = IdOf(NodePath(index))) {
      known[NodePath(index)] = *id;
    }
  }
  std::array<std::byte, kEventBufferSize> buffer;
  while (!stop.stop_requested()) {
    pollfd input{.fd = notify, .events = POLLIN, .revents = 0};
    if (poll(&input, 1, kPollIntervalMs) <= 0) {
      continue;
    }
    const auto count = read(notify, buffer.data(), buffer.size());
    if (count <= 0) {
      break;
    }
    std::size_t offset = 0;
    while (std::cmp_less(offset, count)) {
      offset += HandleEvent(buffer.data() + offset, known, on_disconnected,
                            on_connected);
    }
  }
  close(notify);
}

}  // namespace facepalm
