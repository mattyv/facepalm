// Opens the camera through CameraController for a set time and reports what
// capturing cost: frames received and the process's CPU time. It is the macOS
// camera's smoke test, and the CPU half of #11's measurement; energy comes
// from powermetrics, run alongside with sudo.
//
// Every frame goes through FramePipeline, on queues as the service will run
// them. Given a face landmarker .task bundle, the pipeline also runs the face
// model, prints each frame's reaction as it arrives, and reports what the
// model cost (#18, #21). When face_detection_short_range.tflite sits beside
// the bundle, it also runs Nodding Pigeon and prints nods and shakes (#34).
// That needs a build with FACEPALM_MEDIAPIPE_DYLIB.
//
// Given a directory as well, it saves every frame there as a numbered PNG,
// frame_000001.png and on, with timestamps.txt listing each frame's capture
// time in milliseconds: a recording for tools/fixtures to replay (#21). The
// frames are images of the user, so keep them out of the repository.
//
// Its settings come from ~/.config/facepalm/facepalm.conf (src/config.h); a
// frame rate given here wins over the file's.
//
// Usage: camera_probe [seconds] [frames per second] [model.task] [directory]

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "src/camera_controller.h"
#include "src/config.h"
#include "src/face_detector.h"
#include "src/face_model.h"
#include "src/frame.h"
#include "src/frame_pipeline.h"
#include "src/gesture_analyzer.h"
#include "src/head_pose_analyzer.h"
#include "src/macos/avfoundation_session.h"
#include "src/session_camera.h"

namespace facepalm {
namespace {

constexpr int kDefaultSeconds = 10;
constexpr double kMicrosecondsPerSecond = 1e6;
constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr double kPercent = 100;

constexpr double kPercentile95 = 0.95;

// Saves frames as PNGs on a queue of its own, so the capture queue only
// copies them.
class Recorder {
 public:
  explicit Recorder(std::string directory)
      : directory_(std::move(directory)),
        queue_(
            dispatch_queue_create("facepalm.record", DISPATCH_QUEUE_SERIAL)) {}

  // Copies `frame` and queues it to be written. Capture queue only.
  void Record(const Frame& frame) {
    const auto pixels = std::make_shared<std::vector<std::uint8_t>>(
        frame.bgra, frame.bgra + (frame.bytes_per_row *
                                  static_cast<std::size_t>(frame.height)));
    const int index = ++count_;
    const Frame copy{.width = frame.width,
                     .height = frame.height,
                     .bytes_per_row = frame.bytes_per_row,
                     .bgra = nullptr,
                     .timestamp_ms = frame.timestamp_ms};
    dispatch_async(queue_, ^{
      Write(index, copy, *pixels);
    });
  }

 private:
  // Writes one frame's PNG and appends its capture time.
  void Write(int index, const Frame& frame,
             const std::vector<std::uint8_t>& bgra) {
    constexpr std::size_t kBitsPerChannel = 8;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        const_cast<std::uint8_t*>(bgra.data()),
        static_cast<std::size_t>(frame.width),
        static_cast<std::size_t>(frame.height), kBitsPerChannel,
        frame.bytes_per_row, space,
        static_cast<std::uint32_t>(kCGImageAlphaNoneSkipFirst) |
            static_cast<std::uint32_t>(kCGBitmapByteOrder32Little));
    CGColorSpaceRelease(space);
    if (context == nullptr) {
      return;
    }
    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    std::array<char, kNameLength> name{};
    std::snprintf(name.data(), name.size(), "%s/frame_%06d.png",
                  directory_.c_str(), index);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(name.data()),
        static_cast<CFIndex>(std::strlen(name.data())),
        /*isDirectory=*/static_cast<Boolean>(false));
    CGImageDestinationRef destination =
        url == nullptr ? nullptr
                       : CGImageDestinationCreateWithURL(
                             url, CFSTR("public.png"), 1, nullptr);
    if (destination != nullptr) {
      CGImageDestinationAddImage(destination, image, nullptr);
      CGImageDestinationFinalize(destination);
      CFRelease(destination);
    }
    if (url != nullptr) {
      CFRelease(url);
    }
    CGImageRelease(image);
    FILE* times = std::fopen((directory_ + "/timestamps.txt").c_str(), "a");
    if (times != nullptr) {
      std::fprintf(times, "%d %lld\n", index,
                   static_cast<long long>(frame.timestamp_ms));
      std::fclose(times);
    }
  }

  static constexpr std::size_t kNameLength = 4096;
  std::string directory_;
  dispatch_queue_t queue_;
  int count_ = 0;  // Capture queue only.
};

struct ProbeOptions {
  int seconds = kDefaultSeconds;
  int frames_per_second = Config::kDefaultFramesPerSecond;
  Config config;
  std::unique_ptr<FaceModel> model;        // Runs on every frame when set.
  std::unique_ptr<FaceDetector> detector;  // Nodding Pigeon's, when set.
  std::unique_ptr<Recorder> recorder;      // Saves every frame when set.
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

// Prints each reaction the controller delivers, as one line.
class PrintedClients : public Clients {
 public:
  void Deliver(const ClientId& /*client*/, const Reaction& reaction) override {
    std::printf("reaction %s\n", reaction.c_str());
  }
};

// What the face model did, written on the inference queue and read when the
// camera closes.
struct ModelStats {
  std::mutex mutex;
  std::vector<double> detect_ms;  // Under mutex.
  int faces = 0;                  // Under mutex.
};

// A face model that times each detect into `stats`.
class TimedFaceModel : public FaceModel {
 public:
  TimedFaceModel(std::unique_ptr<FaceModel> model, ModelStats& stats)
      : model_(std::move(model)), stats_(stats) {}

  [[nodiscard]] Result Detect(std::span<const std::uint8_t> rgba, int width,
                              int height, std::int64_t timestamp_ms, Face& face,
                              std::string& error) override {
    const auto start = std::chrono::steady_clock::now();
    const Result result =
        model_->Detect(rgba, width, height, timestamp_ms, face, error);
    const std::chrono::duration<double, std::milli> took =
        std::chrono::steady_clock::now() - start;
    const std::scoped_lock lock(stats_.mutex);
    stats_.detect_ms.push_back(took.count());
    stats_.faces += result == Result::kFace ? 1 : 0;
    return result;
  }

 private:
  std::unique_ptr<FaceModel> model_;
  ModelStats& stats_;
};

// Forwards the camera's reports to the controller, and measures between the
// camera opening and closing.
class Probe : public CameraReports {
 public:
  Probe(ProbeOptions options, dispatch_queue_t control)
      : seconds_(options.seconds),
        recorder_(std::move(options.recorder)),
        has_model_(options.model != nullptr),
        model_(std::move(options.model)),
        detector_(std::move(options.detector)),
        config_(options.config),
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
        controller_(camera_, clients_, /*consent=*/true),
        pipeline_(
            controller_, OnQueue(control),
            OnQueue(dispatch_queue_create("facepalm.inference",
                                          DISPATCH_QUEUE_SERIAL)),
            FramePipeline::Callbacks{
                .make_analyzers =
                    [this](std::string& error) { return TakeAnalyzers(error); },
                .on_error =
                    [](const std::string& error) {
                      std::fprintf(stderr, "camera_probe: %s\n", error.c_str());
                    },
            }) {}

  void Start() { controller_.Subscribe("camera_probe"); }

  void OnOpened() override {
    controller_.OnOpened();
    if (has_model_) {
      pipeline_.OnCameraOpen();
    }
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

  // The process exits from here with the queues still running, and the probe
  // is never destroyed, so no queued task outlives what it points to (#21).
  void OnClosed() override {
    controller_.OnClosed();
    pipeline_.OnCameraClosed();
    const double cpu = cpu_at_close_ - cpu_at_open_;
    std::printf(
        "seconds %d\nframes %d\nframes_per_second %.1f\ncpu_seconds %.3f\n"
        "cpu_percent_of_one_core %.1f\ntimestamps_out_of_order %d\n",
        seconds_, frames_at_close_,
        static_cast<double>(frames_at_close_) / seconds_, cpu,
        kPercent * cpu / seconds_, out_of_order_.load());
    if (has_model_) {
      ReportModel();
    }
    std::exit(0);
  }

  void OnDeviceLost() override {
    controller_.OnDeviceLost();
    pipeline_.OnCameraClosed();
    std::fprintf(stderr, "camera_probe: the camera was lost\n");
    std::exit(1);
  }

  void OnDeviceBack() override { controller_.OnDeviceBack(); }

 private:
  // Runs on the capture queue. Checks that capture times increase, then
  // hands the frame to the pipeline, which copies it out as RGBA.
  void OnFrame(const Frame& frame) {
    if (frames_ > 0 && frame.timestamp_ms <= last_timestamp_ms_) {
      ++out_of_order_;
    }
    last_timestamp_ms_ = frame.timestamp_ms;
    ++frames_;
    if (recorder_ != nullptr) {
      recorder_->Record(frame);
    }
    pipeline_.OnFrame(frame);
  }

  // The pipeline's analyser factory: head pose over the face model loaded
  // at start, timed, and Nodding Pigeon when its detector was found; once.
  FramePipeline::Analyzers TakeAnalyzers(std::string& error) {
    FramePipeline::Analyzers analyzers;
    if (model_ == nullptr) {
      error = "the face model was already used; camera_probe opens once";
      return analyzers;
    }
    if (config_.head_pose_enabled) {
      analyzers.push_back(std::make_unique<HeadPoseAnalyzer>(
          std::make_unique<TimedFaceModel>(std::move(model_), stats_)));
    }
    if (config_.gestures_enabled && detector_ != nullptr) {
      analyzers.push_back(std::make_unique<GestureAnalyzer>(
          std::move(detector_),
          GestureAnalyzer::Thresholds{.motion = config_.motion_threshold,
                                      .gesture = config_.gesture_threshold}));
    }
    return analyzers;
  }

  // Prints how many frames the model saw, how many had a face, and how long
  // detect took.
  void ReportModel() {
    const std::scoped_lock lock(stats_.mutex);
    std::vector<double> sorted = stats_.detect_ms;
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
        sorted.size(), stats_.faces,
        sorted.empty() ? 0.0 : total / static_cast<double>(sorted.size()),
        at(kPercentile95), at(1), threads_at_close_, PeakMegabytes());
  }

  int seconds_;
  std::unique_ptr<Recorder> recorder_;
  bool has_model_;
  std::unique_ptr<FaceModel> model_;        // Until the pipeline takes it.
  std::unique_ptr<FaceDetector> detector_;  // Until the pipeline takes it.
  Config config_;
  ModelStats stats_;
  int threads_at_close_ = 0;
  dispatch_queue_t control_;
  PrintedClients clients_;
  std::atomic<int> frames_ = 0;
  int frames_at_close_ = 0;
  std::int64_t last_timestamp_ms_ = 0;  // Capture queue only.
  std::atomic<int> out_of_order_ = 0;
  double cpu_at_open_ = 0;
  double cpu_at_close_ = 0;
  AvFoundationSession capture_;
  SessionCamera camera_;
  CameraController controller_;
  FramePipeline pipeline_;
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
std::unique_ptr<facepalm::FaceModel> LoadModel(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }
#ifdef FACEPALM_WITH_FACE_MODEL
  std::string error;
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<facepalm::FaceModel> model =
      facepalm::MediaPipeFaceModel::Create(path, error);
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

// Reads ~/.config/facepalm/facepalm.conf, or $XDG_CONFIG_HOME's
// facepalm/facepalm.conf when that is set. A missing file gives the
// defaults; a bad one ends the tool, naming the line.
facepalm::Config LoadConfig() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  const std::string folder =
      xdg != nullptr && *xdg != '\0'
          ? std::string(xdg)
          : std::string(home == nullptr ? "" : home) + "/.config";
  const std::string path = folder + "/facepalm/facepalm.conf";
  const std::ifstream file(path);
  if (!file) {
    return {};
  }
  std::stringstream text;
  text << file.rdbuf();
  std::string error;
  const std::optional<facepalm::Config> config =
      facepalm::ParseConfig(text.str(), error);
  if (!config) {
    std::fprintf(stderr, "camera_probe: %s: %s\n", path.c_str(), error.c_str());
    std::exit(2);
  }
  return *config;
}

// Loads Nodding Pigeon's face detector, face_detection_short_range.tflite,
// from the folder that holds the face model at `model_path`. Returns null
// silently when there is no model or no dylib build, or with a note that
// gestures are off when the detector cannot be loaded.
std::unique_ptr<facepalm::FaceDetector> LoadDetector(const char* model_path) {
  if (model_path == nullptr) {
    return nullptr;
  }
#ifdef FACEPALM_WITH_FACE_MODEL
  const std::string model(model_path);
  const std::string::size_type slash = model.rfind('/');
  const std::string path =
      (slash == std::string::npos ? std::string()
                                  : model.substr(0, slash + 1)) +
      "face_detection_short_range.tflite";
  std::string error;
  auto detector = facepalm::MediaPipeFaceDetector::Create(path, error);
  if (detector == nullptr) {
    std::fprintf(stderr, "camera_probe: no gestures: %s\n", error.c_str());
  }
  return detector;
#else
  return nullptr;
#endif
}

}  // namespace

// The standard fixes main's signature, and an allocation failure may end the
// tool.
// NOLINTNEXTLINE(misc-const-correctness,bugprone-exception-escape)
int main(int argc, char** argv) {
  const int seconds =
      PositiveArgument(argc > 1 ? argv[1] : nullptr, facepalm::kDefaultSeconds);
  const facepalm::Config config = LoadConfig();
  const int frames_per_second =
      PositiveArgument(argc > 2 ? argv[2] : nullptr, config.frames_per_second);
  if (seconds == 0 || frames_per_second == 0) {
    std::fprintf(
        stderr,
        "usage: camera_probe [seconds] [frames per second] [model.task] "
        "[directory]\n");
    return 2;
  }
  facepalm::ProbeOptions options{
      .seconds = seconds,
      .frames_per_second = frames_per_second,
      .config = config,
      .model = LoadModel(argc > 3 ? argv[3] : nullptr),
      .detector = LoadDetector(argc > 3 ? argv[3] : nullptr),
      .recorder =
          argc > 4 ? std::make_unique<facepalm::Recorder>(argv[4]) : nullptr,
  };
  dispatch_queue_t control =
      dispatch_queue_create("facepalm.control", DISPATCH_QUEUE_SERIAL);
  // ponytail: leaked on purpose; the process exits from a report.
  auto* probe = new facepalm::Probe(std::move(options), control);
  dispatch_async(control, ^{
    probe->Start();
  });
  dispatch_main();
}
