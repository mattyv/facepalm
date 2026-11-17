// Opens the camera through CameraController for a set time and reports what
// capturing cost: frames received and the process's CPU time. It is the macOS
// camera's smoke test, and the CPU half of #11's measurement; energy comes
// from powermetrics, run alongside with sudo.
//
// Given a face landmarker .task bundle, it also runs the face model on every
// frame, as the pipeline will, and reports what that costs (#18). That needs
// a build with FACEPALM_MEDIAPIPE_DYLIB set.
//
// Usage: camera_probe [seconds] [frames per second] [model.task]

#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/frame.h"
#include "src/macos/avfoundation_session.h"
#include "src/session_camera.h"

namespace facepalm {
namespace {

constexpr int kDefaultSeconds = 10;
constexpr double kMicrosecondsPerSecond = 1e6;
constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr int kDefaultFramesPerSecond = 15;
constexpr double kPercent = 100;

constexpr double kPercentile95 = 0.95;

struct ProbeOptions {
  int seconds = kDefaultSeconds;
  int frames_per_second = kDefaultFramesPerSecond;
  FaceModel* model = nullptr;  // Runs on every frame when set.
};

// The process's thread count now.
int Threads() {
  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t count = 0;
  if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
    return -1;
  }
  for (mach_msg_type_number_t i = 0; i < count; ++i) {
    mach_port_deallocate(mach_task_self(), threads[i]);
  }
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                count * sizeof(thread_act_t));
  return static_cast<int>(count);
}

// The process's peak resident memory so far, in megabytes. macOS reports
// ru_maxrss in bytes.
double PeakMegabytes() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
  return static_cast<double>(usage.ru_maxrss) / kBytesPerMegabyte;
}

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
        model_(options.model),
        control_(control),
        capture_(
            [this](const Frame& frame) { OnFrame(frame); },
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
          threads_at_close_ = Threads();
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
        "cpu_percent_of_one_core %.1f\ntimestamps_out_of_order %d\n",
        seconds_, frames_at_close_,
        static_cast<double>(frames_at_close_) / seconds_, cpu,
        kPercent * cpu / seconds_, out_of_order_.load());
    if (model_ != nullptr) {
      ReportModel();
    }
    std::exit(0);
  }

  void OnDeviceLost() override {
    controller_.OnDeviceLost();
    std::fprintf(stderr, "camera_probe: the camera was lost\n");
    std::exit(1);
  }

  void OnDeviceBack() override { controller_.OnDeviceBack(); }

 private:
  // Runs on the capture queue. Converts each frame as the face model will,
  // so the CPU figure includes it, and checks that capture times increase.
  void OnFrame(const Frame& frame) {
    ToRgba(frame, rgba_);
    if (frames_ > 0 && frame.timestamp_ms <= last_timestamp_ms_) {
      ++out_of_order_;
    }
    last_timestamp_ms_ = frame.timestamp_ms;
    ++frames_;
    if (model_ != nullptr) {
      Detect(frame);
    }
  }

  // Runs on the capture queue: the face model on one frame, timed. The
  // capture session drops frames that arrive while it runs.
  void Detect(const Frame& frame) {
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    const FaceModel::Result result = model_->Detect(
        rgba_, frame.width, frame.height, frame.timestamp_ms, face_, error);
    const std::chrono::duration<double, std::milli> took =
        std::chrono::steady_clock::now() - start;
    const std::scoped_lock lock(stats_mutex_);
    detect_ms_.push_back(took.count());
    if (result == FaceModel::Result::kFace) {
      ++faces_;
    } else if (result == FaceModel::Result::kFailed) {
      std::fprintf(stderr, "camera_probe: detect failed: %s\n", error.c_str());
    }
  }

  // Prints how many frames the model saw, how many had a face, and how long
  // detect took.
  void ReportModel() {
    const std::scoped_lock lock(stats_mutex_);
    std::vector<double> sorted = detect_ms_;
    std::ranges::sort(sorted);
    double total = 0;
    for (const double ms : sorted) {
      total += ms;
    }
    const auto at = [&sorted](double fraction) {
      return sorted.empty()
                 ? 0.0
                 : sorted[static_cast<std::size_t>(
                       fraction * static_cast<double>(sorted.size() - 1))];
    };
    std::printf(
        "detects %zu\nfaces %d\ndetect_ms_mean %.2f\ndetect_ms_p95 %.2f\n"
        "detect_ms_max %.2f\nthreads %d\npeak_resident_mb %.1f\n",
        sorted.size(), faces_,
        sorted.empty() ? 0.0 : total / static_cast<double>(sorted.size()),
        at(kPercentile95), at(1), threads_at_close_, PeakMegabytes());
  }

  int seconds_;
  FaceModel* model_;
  std::mutex stats_mutex_;
  std::vector<double> detect_ms_;  // Under stats_mutex_.
  int faces_ = 0;                  // Under stats_mutex_.
  Face face_{};                    // Capture queue only.
  int threads_at_close_ = 0;
  dispatch_queue_t control_;
  NoClients clients_;
  std::atomic<int> frames_ = 0;
  int frames_at_close_ = 0;
  std::vector<std::uint8_t> rgba_;      // Capture queue only.
  std::int64_t last_timestamp_ms_ = 0;  // Capture queue only.
  std::atomic<int> out_of_order_ = 0;
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

// Loads the face model at `path`, reporting its create time and the threads
// after it, or returns null when there is no path. Exits when it cannot load
// one, or when this build has no face model.
facepalm::FaceModel* LoadModel(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }
#ifdef FACEPALM_WITH_FACE_MODEL
  std::string error;
  const auto start = std::chrono::steady_clock::now();
  // ponytail: leaked on purpose, like the probe.
  facepalm::FaceModel* model =
      facepalm::MediaPipeFaceModel::Create(path, error).release();
  const std::chrono::duration<double, std::milli> took =
      std::chrono::steady_clock::now() - start;
  if (model == nullptr) {
    std::fprintf(stderr, "camera_probe: %s\n", error.c_str());
    std::exit(1);
  }
  std::printf("model_create_ms %.1f\nthreads_after_create %d\n", took.count(),
              facepalm::Threads());
  return model;
#else
  std::fprintf(stderr,
               "camera_probe: built without the face model; configure with "
               "FACEPALM_MEDIAPIPE_DYLIB\n");
  std::exit(2);
#endif
}

}  // namespace

// The standard fixes main's signature, and an allocation failure may end the
// tool.
// NOLINTNEXTLINE(misc-const-correctness,bugprone-exception-escape)
int main(int argc, char** argv) {
  const int seconds =
      PositiveArgument(argc > 1 ? argv[1] : nullptr, facepalm::kDefaultSeconds);
  const int frames_per_second = PositiveArgument(
      argc > 2 ? argv[2] : nullptr, facepalm::kDefaultFramesPerSecond);
  if (seconds == 0 || frames_per_second == 0) {
    std::fprintf(
        stderr,
        "usage: camera_probe [seconds] [frames per second] [model.task]\n");
    return 2;
  }
  const facepalm::ProbeOptions options{
      .seconds = seconds,
      .frames_per_second = frames_per_second,
      .model = LoadModel(argc > 3 ? argv[3] : nullptr),
  };
  dispatch_queue_t control =
      dispatch_queue_create("facepalm.control", DISPATCH_QUEUE_SERIAL);
  // ponytail: leaked on purpose; the process exits from a report.
  auto* probe = new facepalm::Probe(options, control);
  dispatch_async(control, ^{
    probe->Start();
  });
  dispatch_main();
}
