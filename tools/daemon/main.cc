#ifdef __linux__
#include <pthread.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#else
#include <dispatch/dispatch.h>
#endif

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <utility>

#include "src/approved_feed.h"
#include "src/camera_controller.h"
#include "src/client_approvals.h"
#include "src/client_token.h"
#include "src/config.h"
#include "src/daemon_control.h"
#include "src/daemon_options.h"
#include "src/face_analyzer.h"
#include "src/face_model.h"
#include "src/face_readers.h"
#include "src/feed_clients.h"
#include "src/frame_pipeline.h"
#ifdef __linux__
#include "src/linux/v4l2_session.h"
#else
#include "src/macos/avfoundation_session.h"
#endif
#include "src/feeling_store.h"
#include "src/reaction_feed.h"
#include "src/service.h"
#include "src/session_camera.h"
#include "src/unix_socket.h"
#include "src/user_paths.h"

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
  void Run(const std::stop_token& stop) {
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
  std::jthread worker_{[this](const std::stop_token& stop) { Run(stop); }};
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

// The daemon owns all queues and resources until DaemonControl exits. Its
// methods only forward: the logic is in DaemonControl, which follows
// specs/DaemonAdmission.tla, and in the sources it wires together.
class Engine : public DaemonController, public DaemonPipeline {
 public:
  Engine(std::unique_ptr<Listener> listener, Approvals& approvals,
         std::string model, Config config,
         std::unique_ptr<FeelingStore> feelings, Post control, Post camera,
         Post inference, [[maybe_unused]] std::string device = {})
      : control_queue_(std::move(control)),
        model_(std::move(model)),
        config_(config),
        feelings_(std::move(feelings)),
        feed_(kFeedCapacity, *feelings_),
        clients_(
            [this](std::string summary, std::string detail,
                   std::int64_t published_ms) {
              feed_.React(std::move(summary), std::move(detail), published_ms);
            },
            UnixMilliseconds),
        approved_(
            feed_,
            [this](bool joined) {
              control_queue_([this, joined] { control_.Subscription(joined); });
            },
            [this](const std::string& session) {
              feed_.DisconnectAndForget(session);
            }),
        service_(std::move(listener), approved_, approvals),
        control_(
            *this, *this, [this] { service_.Stop(); },
            [](bool failed) {
              if (failed) {
                std::fputs("facepalm-daemon: stopped after a failure\n",
                           stderr);
              }
              std::_Exit(failed ? 1 : 0);
            }),
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
                        control_.Fail();
                      });
                    }}) {
  }

  void Start() { service_.Start(); }
  void Stop() { control_.Stop(); }
  void Subscribe() override { controller_.Subscribe("approved"); }
  void Leave() override { controller_.Leave("approved"); }
  void Revoke() override { controller_.RevokeConsent(); }
  void Opened() override { controller_.OnOpened(); }
  void OpenFailed() override { controller_.OnOpenFailed(); }
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
    FramePipeline::Analyzers analyzers;
    analyzers.push_back(std::make_unique<FaceAnalyzer>(
        std::move(model), MakeFaceReaders(config_)));
    return analyzers;
  }

  Post control_queue_;
  std::string model_;
  Config config_;
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

// Approves or revokes a client, for the approve and revoke commands.
int ChangeApproval(const DaemonOptions& options) {
  const std::unique_ptr<ApprovalsIo> io = PosixApprovalsIo();
  ClientApprovals approvals(
      options.clients,
      [](const std::string& path) { return LoadClientToken(path); }, *io);
  if (options.command == DaemonOptions::Command::kRevoke) {
    if (!approvals.Revoke(options.client)) {
      std::fprintf(stderr, "no approved client called %s\n",
                   options.client.c_str());
      return 1;
    }
    return 0;
  }
  std::string error;
  const std::optional<std::string> path =
      approvals.Approve(options.client, error);
  if (!path) {
    std::fprintf(stderr, "cannot approve %s: %s\n", options.client.c_str(),
                 error.c_str());
    return 1;
  }
  std::printf("%s\n", path->c_str());
  return 0;
}

}  // namespace
}  // namespace facepalm

// The process reports startup failures, including allocation failures.
// NOLINTNEXTLINE(bugprone-exception-escape,misc-const-correctness)
int main(int argc, char** argv) {
  const char* home = std::getenv("HOME");
  const auto options = facepalm::ParseDaemonOptions(
      std::span<const char* const>(argv + 1, argc - 1),
      facepalm::PathsFor(home != nullptr ? home : "", facepalm::kThisPlatform));
  if (!options) {
    std::fputs(facepalm::kDaemonUsage.data(), stderr);
    return 2;
  }
  if (options->command != facepalm::DaemonOptions::Command::kRun) {
    return facepalm::ChangeApproval(*options);
  }
  const std::unique_ptr<facepalm::ApprovalsIo> approvals_io =
      facepalm::PosixApprovalsIo();
  facepalm::ClientApprovals approvals(
      options->clients,
      [](const std::string& path) { return facepalm::LoadClientToken(path); },
      *approvals_io);
  const std::string socket_directory =
      std::filesystem::path(options->socket).parent_path();
  if (!socket_directory.empty() &&
      !approvals_io->MakePrivateDirectory(socket_directory)) {
    std::fprintf(stderr, "cannot make %s\n", socket_directory.c_str());
    return 1;
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
  facepalm::Engine engine(std::move(listener), approvals, options->model,
                          config, std::move(feelings), control.Poster(),
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
  auto* engine = new facepalm::Engine(
      std::move(listener), approvals, options->model, config,
      std::move(feelings), facepalm::OnQueue(control),
      facepalm::OnQueue(
          dispatch_queue_create("facepalm.camera", DISPATCH_QUEUE_SERIAL)),
      facepalm::OnQueue(
          dispatch_queue_create("facepalm.inference", DISPATCH_QUEUE_SERIAL)));
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
