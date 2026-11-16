// Opens the camera through CameraController for a set time and reports what
// capturing cost: frames received and the process's CPU time. It is the macOS
// camera's smoke test, and the CPU half of #11's measurement; energy comes
// from powermetrics, run alongside with sudo.
//
// Usage: camera_probe [seconds] [frames per second]

#include <dispatch/dispatch.h>
#include <sys/resource.h>

#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>

#include "src/camera_controller.h"
#include "src/macos/avfoundation_session.h"
#include "src/session_camera.h"

namespace facepalm {
namespace {

constexpr int kDefaultSeconds = 10;
constexpr double kMicrosecondsPerSecond = 1e6;
constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr int kDefaultFramesPerSecond = 15;
constexpr double kPercent = 100;

struct ProbeOptions {
  int seconds = kDefaultSeconds;
  int frames_per_second = kDefaultFramesPerSecond;
};

// Posts tasks to a dispatch queue.
Post OnQueue(dispatch_queue_t queue) {
  return [queue](const std::function<void()>& task) {
    // The block must own a copy: it runs after this call returns.
    const std::function<void()> owned = task;
    dispatch_async(queue, ^{
      owned();
    });
  };
}

// The process's user and system CPU time so far, in seconds.
double CpuSeconds() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  const auto seconds = [](const timeval& time) {
    return static_cast<double>(time.tv_sec) +
           (static_cast<double>(time.tv_usec) / kMicrosecondsPerSecond);
  };
  return seconds(usage.ru_utime) + seconds(usage.ru_stime);
}

class NoClients : public Clients {
 public:
  void Deliver(const ClientId& /*client*/,
               const Reaction& /*reaction*/) override {}
};

// Forwards the camera's reports to the controller, and measures between the
// camera opening and closing.
class Probe : public CameraReports {
 public:
  Probe(ProbeOptions options, dispatch_queue_t control)
      : seconds_(options.seconds),
        control_(control),
        capture_(
            [this](const Frame& /*frame*/) { ++frames_; },
            [this] { camera_.OnSessionFailed(); },
            [this](const std::string& id) { camera_.OnDeviceDisconnected(id); },
            [this](const std::string& id) { camera_.OnDeviceConnected(id); }),
        camera_(capture_, *this,
                OnQueue(dispatch_queue_create("facepalm.camera.session",
                                              DISPATCH_QUEUE_SERIAL)),
                OnQueue(control), options.frames_per_second),
        controller_(camera_, clients_, /*consent=*/true) {}

  void Start() { controller_.Subscribe("camera_probe"); }

  void OnOpened() override {
    controller_.OnOpened();
    frames_ = 0;
    cpu_at_open_ = CpuSeconds();
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, seconds_ * kNanosecondsPerSecond),
        control_, ^{
          frames_at_close_ = frames_;
          cpu_at_close_ = CpuSeconds();
          controller_.Leave("camera_probe");
        });
  }

  void OnOpenFailed() override {
    controller_.OnOpenFailed();
    std::fprintf(stderr,
                 "camera_probe: no camera, or camera permission denied\n");
    std::exit(1);
  }

  void OnClosed() override {
    controller_.OnClosed();
    const double cpu = cpu_at_close_ - cpu_at_open_;
    std::printf(
        "seconds %d\nframes %d\nframes_per_second %.1f\ncpu_seconds %.3f\n"
        "cpu_percent_of_one_core %.1f\n",
        seconds_, frames_at_close_,
        static_cast<double>(frames_at_close_) / seconds_, cpu,
        kPercent * cpu / seconds_);
    std::exit(0);
  }

  void OnDeviceLost() override {
    controller_.OnDeviceLost();
    std::fprintf(stderr, "camera_probe: the camera was lost\n");
    std::exit(1);
  }

  void OnDeviceBack() override { controller_.OnDeviceBack(); }

 private:
  int seconds_;
  dispatch_queue_t control_;
  NoClients clients_;
  std::atomic<int> frames_ = 0;
  int frames_at_close_ = 0;
  double cpu_at_open_ = 0;
  double cpu_at_close_ = 0;
  AvFoundationSession capture_;
  SessionCamera camera_;
  CameraController controller_;
};

}  // namespace
}  // namespace facepalm

namespace {

// Reads `text` as a positive whole number, or returns `fallback` when it is
// missing. Returns 0 when it is not a positive whole number.
int PositiveArgument(const char* text, int fallback) {
  if (text == nullptr) {
    return fallback;
  }
  const std::string_view view(text);
  int value = 0;
  const auto [end, error] =
      std::from_chars(view.data(), view.data() + view.size(), value);
  const bool whole = error == std::errc() && end == view.data() + view.size();
  return whole && value > 0 ? value : 0;
}

}  // namespace

// NOLINTNEXTLINE(misc-const-correctness): the standard fixes main's signature.
int main(int argc, char** argv) {
  const facepalm::ProbeOptions options{
      .seconds = PositiveArgument(argc > 1 ? argv[1] : nullptr,
                                  facepalm::kDefaultSeconds),
      .frames_per_second = PositiveArgument(argc > 2 ? argv[2] : nullptr,
                                            facepalm::kDefaultFramesPerSecond),
  };
  if (options.seconds == 0 || options.frames_per_second == 0) {
    std::fprintf(stderr, "usage: camera_probe [seconds] [frames per second]\n");
    return 2;
  }
  dispatch_queue_t control =
      dispatch_queue_create("facepalm.control", DISPATCH_QUEUE_SERIAL);
  // ponytail: leaked on purpose; the process exits from a report.
  auto* probe = new facepalm::Probe(options, control);
  dispatch_async(control, ^{
    probe->Start();
  });
  dispatch_main();
}
