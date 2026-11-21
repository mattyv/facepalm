#ifdef __linux__
#include <pthread.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#else
#include <dispatch/dispatch.h>
#endif

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/approved_feed.h"
#include "src/camera_controller.h"
#include "src/client_token.h"
#include "src/config.h"
#include "src/daemon_control.h"
#include "src/expression_reader.h"
#include "src/face_analyzer.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/frame_pipeline.h"
#include "src/gesture_reader.h"
#include "src/look_away.h"
#include "src/look_away_reader.h"
#include "src/screen_context.h"
#include "src/screen_context_reader.h"
#include "src/screen_context_reporter.h"
#include "src/screen_source.h"
#ifdef __linux__
#include "src/linux/screen_context_linux.h"
#include "src/linux/v4l2_session.h"
#else
#include "src/macos/avfoundation_session.h"
#include "src/macos/screen_context_macos.h"
#endif
#include "src/feeling_store.h"
#include "src/reaction_feed.h"
#include "src/service.h"
#include "src/session_camera.h"
#include "src/unix_socket.h"

namespace facepalm {
namespace {
constexpr int kFeedCapacity = 100;
#ifdef __linux__
// The queue processes effects serially just like the macOS dispatch queues.
class SerialQueue {
 public:
  Post Poster() {
    return [this](const std::function<void()>& task) {
      {
        const std::scoped_lock lock(mutex_);
        pending_.push(task);
      }
      ready_.notify_one();
    };
  }

 private:
  void Run(std::stop_token stop) {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(mutex_);
        if (!ready_.wait(lock, stop, [this] { return !pending_.empty(); })) {
          return;
        }
        task = std::move(pending_.front());
        pending_.pop();
      }
      task();
    }
  }

  std::mutex mutex_;
  std::condition_variable_any ready_;
  std::queue<std::function<void()>> pending_;
  std::jthread worker_{[this](std::stop_token stop) { Run(stop); }};
};
#else
Post OnQueue(dispatch_queue_t queue) {
  return [queue](const std::function<void()>& task) {
    const std::function<void()> owned = task;
    dispatch_async(queue, ^{
      owned();
    });
  };
}

#endif

// The platform's ScreenSource (#61 item 11): AVFoundation's sibling,
// AppKit, on macOS; X11, through XWayland where one runs, on Linux.
#ifdef __linux__
std::unique_ptr<ScreenSource> MakeScreenSource() {
  return std::make_unique<LinuxScreenSource>();
}
#else
std::unique_ptr<ScreenSource> MakeScreenSource() {
  return std::make_unique<MacosScreenSource>();
}
#endif

// Forwards to Engine's own long-lived ScreenContextReader (#61 item 11):
// MakeAnalyzers rebuilds `readers` on every camera open, but the pose it
// caches, and FeedClients' access to it, both need to outlive any one
// camera session.
class ScreenContextReaderForwarder : public FaceReader {
 public:
  explicit ScreenContextReaderForwarder(ScreenContextReader& target)
      : target_(target) {}
  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override {
    target_.OnFace(face, width, height, timestamp_ms, reactions);
  }
  void OnNoFace() override { target_.OnNoFace(); }

 private:
  ScreenContextReader& target_;
};

// Only interpreted gesture and expression events enter the feed. Each also
// carries screen context (#61 item 11) when Config::screen_context_enabled:
// `screen_source` is nullptr otherwise, and `pose_reader` goes unread.
class FeedClients : public Clients {
 public:
  FeedClients(ReactionFeed& feed, const Config& config,
              ScreenContextReader& pose_reader, ScreenSource* screen_source)
      : feed_(feed), pose_reader_(pose_reader) {
    if (screen_source != nullptr) {
      reporter_.emplace(
          *screen_source,
          GazeSettings{.other_screen_degrees =
                           config.screen_context_other_screen_degrees,
                       .away_degrees = config.looking_away_degrees},
          config.screen_context_window_titles_enabled);
    }
  }
  void Deliver(const ClientId& /*client*/, const Reaction& reaction) override {
    constexpr std::string_view kGesture = R"({"gesture":")";
    constexpr std::string_view kExpression = R"({"expression":")";
    const std::string_view event(reaction);
    const bool gesture = event.starts_with(kGesture);
    const bool expression = event.starts_with(kExpression);
    if ((!gesture && !expression) || !event.ends_with("\"}")) {
      return;
    }
    const auto prefix = gesture ? kGesture : kExpression;
    const std::string name(
        event.substr(prefix.size(), event.size() - prefix.size() - 2));
    const auto published =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    std::optional<std::string> context;
    if (reporter_) {
      context =
          reporter_->Json(pose_reader_.Pose(), pose_reader_.RestingPose());
    }
    feed_.React(std::string(gesture ? "Gesture: " : "Expression: ") + name,
                reaction, published.count(), std::move(context));
  }

 private:
  ReactionFeed& feed_;
  ScreenContextReader& pose_reader_;
  std::optional<ScreenContextReporter> reporter_;
};

// The daemon owns all queues and resources until the stopped callback exits.
class Engine : public DaemonController, public DaemonPipeline {
 public:
  Engine(std::unique_ptr<Listener> listener, std::string token,
         std::string model, Config config,
         std::unique_ptr<FeelingStore> feelings, Post control, Post camera,
         Post inference, [[maybe_unused]] std::string device = {})
      : control_queue_(std::move(control)),
        model_(std::move(model)),
        config_(config),
        screen_source_(config.screen_context_enabled ? MakeScreenSource()
                                                     : nullptr),
        screen_context_reader_(ExpressionTracker::Settings{
            .rise = config.expressions_smallest_rise,
            .pose_degrees = config.expressions_pose_degrees,
            .pose_range = config.expressions_pose_range,
            .settle_ms = config.expressions_settle_ms,
            .spreads = config.expressions_spreads,
            .hold_ms = config.expressions_hold_ms}),
        feelings_(std::move(feelings)),
        feed_(kFeedCapacity, *feelings_),
        clients_(feed_, config_, screen_context_reader_, screen_source_.get()),
        approved_(
            feed_, std::move(token),
            [this](bool joined) {
              control_queue_([this, joined] { control_.Subscription(joined); });
            },
            [this](const std::string& session) {
              feed_.DisconnectAndForget(session);
            }),
        service_(std::move(listener), approved_),
        control_(
            *this, *this, [this] { service_.Stop(); },
            [this] { std::_Exit(failed_ ? 1 : 0); }),
#ifdef __linux__
        capture_(
            std::move(device),
            [this](const Frame& frame) { pipeline_.OnFrame(frame); },
            [this] { camera_.OnSessionFailed(); }),
#else
        capture_(
            [this](const Frame& frame) { pipeline_.OnFrame(frame); },
            [this] { camera_.OnSessionFailed(); },
            [this](const std::string& id) { camera_.OnDeviceDisconnected(id); },
            [this](const std::string& id) { camera_.OnDeviceConnected(id); }),
#endif
        camera_(capture_, control_, std::move(camera), control_queue_,
                config.frames_per_second),
        controller_(camera_, clients_, true),
        pipeline_(
            controller_, control_queue_, std::move(inference),
            FramePipeline::Callbacks{
                .make_analyzers =
                    [this](std::string& error) { return MakeAnalyzers(error); },
                .on_error =
                    [this](const std::string&) {
                      control_queue_([this] {
                        std::fprintf(stderr,
                                     "facepalm-daemon: model failure\n");
                        failed_ = true;
                        control_.Stop();
                      });
                    }}) {
  }

  void Start() { service_.Start(); }
  void Stop() { control_.Stop(); }
  void Subscribe() override { controller_.Subscribe("approved"); }
  void Leave() override { controller_.Leave("approved"); }
  void Revoke() override { controller_.RevokeConsent(); }
  void Opened() override { controller_.OnOpened(); }
  void OpenFailed() override {
    const bool opening =
        controller_.CurrentState() == CameraController::State::kOpening;
    controller_.OnOpenFailed();
    if (opening) {
      std::fprintf(stderr, "facepalm-daemon: camera could not open\n");
      failed_ = true;
      control_.Stop();
    }
  }
  void Closed() override { controller_.OnClosed(); }
  void DeviceLost() override { controller_.OnDeviceLost(); }
  void DeviceBack() override { controller_.OnDeviceBack(); }
  CameraController::State State() const override {
    return controller_.CurrentState();
  }
  void Open() override { pipeline_.OnCameraOpen(); }
  void Close(std::function<void()> done) override {
    pipeline_.OnCameraClosed(std::move(done));
  }

 private:
  FramePipeline::Analyzers MakeAnalyzers(std::string& error) {
    auto model = MediaPipeFaceModel::Create(model_, error);
    if (!model) {
      return {};
    }
    std::vector<std::unique_ptr<FaceReader>> readers;
    if (config_.gestures_enabled) {
      readers.push_back(std::make_unique<GestureReader>(
          config_.gesture_confidence_threshold));
    }
    if (config_.expressions_enabled) {
      readers.push_back(
          std::make_unique<ExpressionReader>(ExpressionTracker::Settings{
              .rise = config_.expressions_smallest_rise,
              .pose_degrees = config_.expressions_pose_degrees,
              .pose_range = config_.expressions_pose_range,
              .settle_ms = config_.expressions_settle_ms,
              .spreads = config_.expressions_spreads,
              .hold_ms = config_.expressions_hold_ms}));
    }
    if (config_.looking_away_enabled) {
      readers.push_back(std::make_unique<LookAwayReader>(
          ExpressionTracker::Settings{
              .rise = config_.expressions_smallest_rise,
              .pose_degrees = config_.expressions_pose_degrees,
              .pose_range = config_.expressions_pose_range,
              .settle_ms = config_.expressions_settle_ms,
              .spreads = config_.expressions_spreads,
              .hold_ms = config_.expressions_hold_ms},
          LookAwayDetector::Settings{.degrees = config_.looking_away_degrees,
                                     .hold_ms = config_.looking_away_hold_ms}));
    }
    if (config_.screen_context_enabled) {
      readers.push_back(std::make_unique<ScreenContextReaderForwarder>(
          screen_context_reader_));
    }
    FramePipeline::Analyzers analyzers;
    analyzers.push_back(
        std::make_unique<FaceAnalyzer>(std::move(model), std::move(readers)));
    return analyzers;
  }

  bool failed_ = false;  // Control queue only.
  Post control_queue_;
  std::string model_;
  Config config_;
  // Outlives any one camera session, unlike MakeAnalyzers' own readers
  // (#61 item 11); screen_source_ is nullptr with screen_context disabled.
  std::unique_ptr<ScreenSource> screen_source_;
  ScreenContextReader screen_context_reader_;
  std::unique_ptr<FeelingStore> feelings_;  // The person's own reads (#35).
  ReactionFeed feed_;
  FeedClients clients_;
  ApprovedFeed approved_;
  Service service_;
  DaemonControl control_;
#ifdef __linux__
  V4l2Session capture_;
#else
  AvFoundationSession capture_;
#endif
  SessionCamera camera_;
  CameraController controller_;
  FramePipeline pipeline_;
};

struct Options {
  bool consent = false;
  std::string socket;
  std::string model;
  std::string token;
  std::string config;
  std::string feelings_file;
};

std::optional<Options> ParseOptions(int argc, char* const* argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--consent" && !options.consent) {
      options.consent = true;
      continue;
    }
    if (i + 1 >= argc) {
      return std::nullopt;
    }
    std::string* value = nullptr;
    if (arg == "--socket") {
      value = &options.socket;
    } else if (arg == "--model") {
      value = &options.model;
    } else if (arg == "--client-token-file") {
      value = &options.token;
    } else if (arg == "--config") {
      value = &options.config;
    } else if (arg == "--feelings-file") {
      value = &options.feelings_file;
    }
    if (value == nullptr || !value->empty()) {
      return std::nullopt;
    }
    *value = argv[++i];
  }
  if (!options.consent || options.socket.empty() || options.model.empty() ||
      options.token.empty()) {
    return std::nullopt;
  }
  return options;
}
}  // namespace
}  // namespace facepalm

// The process reports startup failures, including allocation failures.
// NOLINTNEXTLINE(bugprone-exception-escape,misc-const-correctness)
int main(int argc, char** argv) {
  const auto options = facepalm::ParseOptions(argc, argv);
  if (!options) {
    std::fprintf(
        stderr,
        "usage: facepalm-daemon --consent --socket PATH --model PATH "
        "--client-token-file PATH [--config PATH] [--feelings-file PATH]\n");
    return 2;
  }
  const auto token = facepalm::LoadClientToken(options->token);
  if (!token) {
    std::fprintf(stderr, "invalid client token file\n");
    return 2;
  }
  std::ifstream const model_file(options->model, std::ios::binary);
  if (!model_file) {
    std::fprintf(stderr, "cannot read model\n");
    return 2;
  }
  facepalm::Config config;
  if (!options->config.empty()) {
    std::ifstream const file(options->config);
    if (!file) {
      std::fprintf(stderr, "cannot read config\n");
      return 2;
    }
    std::stringstream contents;
    contents << file.rdbuf();
    std::string error;
    const auto parsed = facepalm::ParseConfig(contents.str(), error);
    if (!parsed) {
      std::fprintf(stderr, "invalid config\n");
      return 2;
    }
    config = *parsed;
  }
  std::string error;
  const std::string feelings_file =
      options->feelings_file.empty()
          ? facepalm::DefaultFeelingsPath(
                [](const char* name) { return std::getenv(name); })
          : options->feelings_file;
  auto feelings = feelings_file.empty()
                      ? nullptr
                      : facepalm::FeelingStore::Open(feelings_file, error);
  if (!feelings) {
    std::fprintf(stderr, "cannot open feelings file: %s\n", error.c_str());
    return 2;
  }
  auto listener = facepalm::UnixListener::Create(options->socket, error);
  if (!listener) {
    std::fprintf(stderr, "cannot open socket\n");
    return 1;
  }
#ifdef __linux__
  // Block before starting worker threads, so the main thread can route stop
  // requests onto the control queue without a signal-handler callback.
  sigset_t signals{};
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
    std::fprintf(stderr, "cannot configure daemon signals\n");
    return 1;
  }
  facepalm::SerialQueue control;
  facepalm::SerialQueue camera;
  facepalm::SerialQueue inference;
  const char* device = std::getenv("FACEPALM_CAMERA_DEVICE");
  facepalm::Engine engine(std::move(listener), *token, options->model, config,
                          std::move(feelings), control.Poster(),
                          camera.Poster(), inference.Poster(),
                          device != nullptr ? device : "/dev/video0");
  engine.Start();
  int signal_number = 0;
  if (sigwait(&signals, &signal_number) != 0) {
    std::_Exit(1);
  }
  control.Poster()([&engine] { engine.Stop(); });
  // DaemonControl exits after the camera, model and clients have retired.
  for (;;) {
    if (sigwait(&signals, &signal_number) != 0) {
      std::_Exit(1);
    }
  }
#else
  dispatch_queue_t control =
      dispatch_queue_create("facepalm.control", DISPATCH_QUEUE_SERIAL);
  auto* engine =
      new facepalm::Engine(std::move(listener), *token, options->model, config,
                           std::move(feelings), facepalm::OnQueue(control),
                           facepalm::OnQueue(dispatch_queue_create(
                               "facepalm.camera", DISPATCH_QUEUE_SERIAL)),
                           facepalm::OnQueue(dispatch_queue_create(
                               "facepalm.inference", DISPATCH_QUEUE_SERIAL)));
  for (int const sig : {SIGINT, SIGTERM}) {
    signal(sig, SIG_IGN);
    dispatch_source_t source =
        dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, sig, 0, control);
    dispatch_source_set_event_handler(source, ^{
      engine->Stop();
    });
    dispatch_resume(source);
  }
  engine->Start();
  dispatch_main();
#endif
}
