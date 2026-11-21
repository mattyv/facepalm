#ifndef FACEPALM_SRC_LINUX_V4L2_HOTPLUG_H_
#define FACEPALM_SRC_LINUX_V4L2_HOTPLUG_H_

#include <functional>
#include <stop_token>
#include <string>
#include <thread>

namespace facepalm {

// Watches whether a V4L2 node is attached, so V4l2Session can tell
// SessionCamera a camera connecting or disconnecting the way
// AvFoundationSession already does through AVFoundation's own
// notifications (#111, #13; specs/CameraLifecycle.tla's DeviceLost,
// DeviceBack, OtherDeviceLost and OtherDeviceBack). A seam: V4l2SessionTest
// fakes this interface, rather than inotify directly, so it needs no real
// watch; V4l2InotifyMonitor, below, is held to it by v4l2_hotplug_test.cc,
// which fakes inotify instead.
class HotplugMonitor {
 public:
  virtual ~HotplugMonitor() = default;

  // Starts watching. `on_disconnected` and `on_connected` each fire with a
  // node's id, found the way CaptureSession's own Configure and List do,
  // on any thread, once for each time one appears or disappears after this
  // call; never for one already attached when it is made, the way
  // AVFoundation's own notifications do not fire for those either. Called
  // at most once.
  virtual void Start(std::function<void(std::string)> on_disconnected,
                     std::function<void(std::string)> on_connected) = 0;
};

// The real monitor, over Linux's inotify on /dev (#111): cheaper than a
// udev monitor's netlink socket and needs no extra library, at the cost of
// reusing the same four-node probe item 10 of #61 already chose for
// Configure and List (docs/decisions.md) rather than discovering every
// video node udev knows about. Owns a thread from Start, stopped and
// joined on destruction.
class V4l2InotifyMonitor final : public HotplugMonitor {
 public:
  V4l2InotifyMonitor() = default;
  ~V4l2InotifyMonitor() override = default;
  V4l2InotifyMonitor(const V4l2InotifyMonitor&) = delete;
  V4l2InotifyMonitor& operator=(const V4l2InotifyMonitor&) = delete;

  void Start(std::function<void(std::string)> on_disconnected,
             std::function<void(std::string)> on_connected) override;

 private:
  static void Watch(const std::stop_token& stop,
                    const std::function<void(std::string)>& on_disconnected,
                    const std::function<void(std::string)>& on_connected);

  std::jthread watcher_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LINUX_V4L2_HOTPLUG_H_
