#ifdef __linux__
#include <pthread.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#else
#include <dispatch/dispatch.h>
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
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
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#include "src/approved_feed.h"
#include "src/calibration.h"
#include "src/calibration_store.h"
#include "src/camera_controller.h"
#include "src/camera_demand.h"
#include "src/client_approvals.h"
#include "src/client_token.h"
#include "src/config.h"
#include "src/daemon_calibration.h"
#include "src/daemon_control.h"
#include "src/daemon_options.h"
#include "src/daemon_status.h"
#include "src/eye_gaze.h"
#include "src/eye_signs.h"
#include "src/face_analyzer.h"
#include "src/face_model.h"
#include "src/face_readers.h"
#include "src/feed_clients.h"
#include "src/frame_pipeline.h"
#include "src/screen_context.h"
#include "src/screen_context_reader.h"
#include "src/screen_context_reporter.h"
#include "src/screen_source.h"
#ifdef __linux__
#include "src/linux/preview_jpeg.h"
#include "src/linux/screen_context_linux.h"
#include "src/linux/v4l2_hotplug.h"
#include "src/linux/v4l2_session.h"
#else
#include "src/macos/avfoundation_session.h"
#include "src/macos/preview_jpeg.h"
#include "src/macos/screen_context_macos.h"
#endif
#include "src/feeling_store.h"
#include "src/preview_lease.h"
#include "src/preview_publisher.h"
#include "src/preview_readings.h"
#include "src/preview_request.h"
#include "src/reaction_feed.h"
#include "src/service.h"
#include "src/session_camera.h"
#include "src/unix_socket.h"
#include "src/user_paths.h"

namespace facepalm {
namespace {
constexpr int kFeedCapacity = 100;
// How long to wait before retrying a denied permission or a failed start
// (#52, item 10 of #61): a fixed interval, decided over a backoff that
// grows, since the usual cause (the person has not yet answered a System
// Settings prompt, or opened it) does not get more likely to clear
// sooner the fewer times it is checked, and a fixed interval is simpler
// to explain and to test.
constexpr std::chrono::seconds kRetryInterval{5};

// How often to recheck the camera preview's own request file for camera
// admission (#176, src/camera_demand.h), independent of whether the
// camera is already open: a frame can only arrive, and so OnFrame's own
// check of the same file can only run, once the camera is; comfortably
// under src/preview_request.h's own staleness window
// (kPreviewRequestStaleMs), the same cadence the menu's own refresh
// timer (tools/menu/main.mm, tools/menu/main_linux.cc) already uses.
constexpr std::chrono::milliseconds kPreviewDemandPollInterval{500};

// Schedules a retry on `control`, `kRetryInterval` later, on a thread of
// its own: the portable way to delay a task when the queue underneath
// (dispatch on macOS, the SerialQueue above on Linux) has no delay of its
// own to ask for.
CameraController::RetrySchedule RetryAfter(const Post& control) {
  return [control](std::function<void()> retry) {
    std::thread([control, retry = std::move(retry)] {
      std::this_thread::sleep_for(kRetryInterval);
      control(retry);
    }).detach();
  };
}
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

// Replaces `path` with `bytes` atomically (#37 item 8), as
// DaemonStatus::Publish does for the camera-in-use indicator: a
// temporary file in the same directory, renamed over it, so a reader
// never sees a half-written frame. Opened with an explicit 0600 (#114),
// not through an ofstream, whose own default mode leaks whatever the
// process umask allows: the preview frame is a snapshot of the camera,
// private to the user the same as every other file src/user_paths.h
// names. open()'s own mode argument only applies the first time O_CREAT
// actually creates the file (#129): an existing .tmp left by an earlier
// failed write -- or, before this fix, one created under a looser umask
// -- keeps whatever mode it already had through O_TRUNC, so fchmod()
// enforces 0600 unconditionally, every call, rather than only on a fresh
// create.
bool WriteFileAtomically(const std::string& path, std::string_view bytes) {
  constexpr mode_t kPrivateFile = 0600;
  const std::string temporary = path + ".tmp";
  const int fd =
      open(temporary.c_str(),
           O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, kPrivateFile);
  if (fd < 0) {
    return false;
  }
  if (fchmod(fd, kPrivateFile) != 0) {
    close(fd);
    std::remove(temporary.c_str());
    return false;
  }
  const bool written = write(fd, bytes.data(), bytes.size()) ==
                       static_cast<ssize_t>(bytes.size());
  close(fd);
  if (!written) {
    std::remove(temporary.c_str());
    return false;
  }
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

// The request file's own last-modified time, in milliseconds since the
// epoch, or nullopt when it cannot be read -- missing, same as any other
// error (#114): IsFreshPreviewRequest's own `modified_ms`, the one real
// collaborator that function is tested with a fake for.
std::optional<std::int64_t> FileModifiedMs(const std::string& path) {
  struct stat info{};
  if (stat(path.c_str(), &info) != 0) {
    return std::nullopt;
  }
#ifdef __APPLE__
  const auto seconds = info.st_mtimespec.tv_sec;
  const auto nanoseconds = info.st_mtimespec.tv_nsec;
#else
  const auto seconds = info.st_mtim.tv_sec;
  const auto nanoseconds = info.st_mtim.tv_nsec;
#endif
  constexpr std::int64_t kMsPerSecond = 1000;
  constexpr std::int64_t kNsPerMs = 1000000;
  return (static_cast<std::int64_t>(seconds) * kMsPerSecond) +
         (static_cast<std::int64_t>(nanoseconds) / kNsPerMs);
}

// `daemon_paths` is empty only when `--home` names a directory
// `UserPaths::PathsFor` does not recognize; the preview is then simply
// off, the same as if the menu never asked. A function of its own keeps
// this check out of main()'s own branching.
std::string PreviewRequestPath(
    const std::optional<facepalm::UserPaths>& daemon_paths) {
  return daemon_paths ? daemon_paths->preview_request : std::string{};
}

std::string PreviewFramePath(
    const std::optional<facepalm::UserPaths>& daemon_paths) {
  return daemon_paths ? daemon_paths->preview_frame : std::string{};
}

// The preview's own text (#134): same emptiness rule as the frame and
// the request above, when `--home` names no recognized directory.
std::string PreviewReadingsPath(
    const std::optional<facepalm::UserPaths>& daemon_paths) {
  return daemon_paths ? daemon_paths->preview_readings : std::string{};
}

// CalibrationSession's own tunable thresholds and gains, from Config
// (#137): pulled out of Engine's constructor so its own cognitive
// complexity (#64) stays under the project's threshold.
facepalm::CalibrationSettings CalibrationSettingsFor(
    const facepalm::Config& config) {
  return facepalm::CalibrationSettings{
      .default_gains = {.horizontal = config.eye_gaze_horizontal_gain,
                        .vertical = config.eye_gaze_vertical_gain},
      .min_head_travel_degrees = config.calibration_min_head_travel_degrees,
      .min_axis_degrees = config.calibration_min_axis_degrees,
      .min_spread_degrees = config.calibration_min_spread_degrees,
      .head_only_yaw_degrees = config.eye_gaze_head_only_yaw_degrees};
}

// A fresh copy of `*profile`, shared ownership, or nullptr with no
// profile given (#61 item 12): Engine's own, so a session finishing
// later can replace it wholesale (#136, #137, #167) rather than only
// borrowing main()'s local for the Engine's whole lifetime, or mutating
// it in place while reporter_/look_away_reader_ read their own
// snapshot of it on another thread.
std::shared_ptr<const facepalm::CalibrationProfile> SharedProfile(
    const facepalm::CalibrationProfile* profile) {
  if (profile == nullptr) {
    return nullptr;
  }
  return std::make_shared<const facepalm::CalibrationProfile>(*profile);
}

// Where a finished calibration session saves its profile (#116): empty,
// the same as a daemon run with no resolvable --home, leaves calibration
// unavailable, the way DaemonCalibration (src/daemon_calibration.h)
// already treats a nullptr controller.
std::string CalibrationStorePath(
    const std::optional<facepalm::UserPaths>& daemon_paths) {
  return daemon_paths ? daemon_paths->calibration : std::string{};
}

// The daemon owns all queues and resources until DaemonControl exits. Its
// methods only forward: the logic is in DaemonControl, which follows
// specs/DaemonAdmission.tla, and in the sources it wires together.
class Engine : public DaemonController, public DaemonPipeline {
 public:
  // `calibration_profile`, when not null, must outlive this Engine (#61
  // item 12): main() loads it, if one was saved, before either Engine or
  // its caller's event loop can return.
  Engine(std::unique_ptr<Listener> listener, Approvals& approvals,
         std::string model, const Config& config,
         std::unique_ptr<FeelingStore> feelings,
         const CalibrationProfile* calibration_profile, Post control,
         Post camera, Post inference, std::string status_path = {},
         std::string preview_request_path = {},
         std::string preview_frame_path = {},
         std::string preview_readings_path = {},
         std::string calibration_store_path = {})
      : control_queue_(std::move(control)),
        model_(std::move(model)),
        config_(config),
        preview_request_path_(std::move(preview_request_path)),
        preview_frame_path_(std::move(preview_frame_path)),
        preview_readings_path_(std::move(preview_readings_path)),
        // src/camera_demand.h's CameraDemand (#176): combines an approved
        // client subscribing with the camera preview's own live, fresh
        // request into the one admission signal DaemonControl::Subscription
        // already needs, firing only on that combined signal's own edge.
        // CameraStatus!Publish's own first-join/last-leave signal (#37
        // item 8) is this same edge too, generalized the same way, so the
        // indicator reads "Camera on" for a preview with no approved
        // client either.
        camera_demand_([this](bool wanted) {
          PublishStatus(wanted);
          control_.Subscription(wanted);
          // PreviewLease!CameraOpens / CameraCloses (#37 item 8, #176):
          // the same combined signal the camera's own subscription
          // already uses.
          if (wanted) {
            preview_lease_.CameraOpened();
          } else {
            preview_lease_.CameraClosed();
          }
        }),
        // PreviewLease!streaming_changed (#114, #134): once streaming
        // stops, the last frame published, and the readings text beside
        // it, are cleared, so neither ever answers the menu's next poll
        // as though the camera were still being shown.
        preview_lease_([this](bool streaming) {
          if (streaming) {
            return;
          }
          if (!preview_frame_path_.empty()) {
            std::remove(preview_frame_path_.c_str());
          }
          if (!preview_readings_path_.empty()) {
            std::remove(preview_readings_path_.c_str());
          }
          preview_readings_.Clear();
        }),
        preview_publisher_(
            [this] {
              // specs/PreviewLease.tla's Fresh (#114): the request must
              // not only exist but have been touched recently, so a menu
              // that quit without cleaning up, crashed, or whose session
              // logged out, stops streaming soon after, rather than
              // forever.
              return !preview_request_path_.empty() &&
                     IsFreshPreviewRequest(preview_request_path_,
                                           UnixMilliseconds(), FileModifiedMs);
            },
            [](const Frame& frame) -> std::optional<std::string> {
              constexpr float kPreviewQuality = 0.6F;
              return EncodePreviewJpeg(frame, kPreviewQuality);
            },
            [this](const std::string& bytes) {
              return !preview_frame_path_.empty() &&
                     WriteFileAtomically(preview_frame_path_, bytes);
            },
            preview_lease_),
        screen_source_(config.screen_context_enabled ? MakeScreenSource()
                                                     : nullptr),
        screen_context_reader_(
            ExpressionTracker::Settings{
                .rise = config.expressions_smallest_rise,
                .pose_degrees = config.expressions_pose_degrees,
                .pose_range = config.expressions_pose_range,
                .settle_ms = config.expressions_settle_ms,
                .spreads = config.expressions_spreads,
                .hold_ms = config.expressions_hold_ms,
                .eye_look_down_threshold =
                    config.expressions_eye_look_down_threshold,
                .eye_blink_threshold = config.expressions_eye_blink_threshold},
            EyeGazeTracker::Settings{
                .blink_threshold = config.eye_gaze_blink_threshold,
                .median_ms = config.eye_gaze_median_ms,
                .resting_ms = config.eye_gaze_resting_ms,
                .gap_ms = config.eye_gaze_gap_ms},
            // Every frame, regardless of whether it carries a reaction
            // (#142): SampleScreenContext is declared below, but this
            // callback only runs once the whole App is built, by which
            // time it exists.
            [this](const std::optional<HeadRotation>& pose,
                   const std::optional<HeadRotation>& resting,
                   const std::optional<EyeSignal>& eye,
                   const std::optional<EyeSignal>& eye_resting,
                   const std::optional<EyeSignSample>& eye_sign) {
              SampleScreenContext(pose, resting, eye, eye_resting, eye_sign);
            }),
        calibration_profile_(SharedProfile(calibration_profile)),
        // Outlives any one camera session too (#141, as screen_context_
        // reader_ does, #61 item 11): its own kGone tracking, and the
        // calibration profile OnCalibrationFinished swaps into it below,
        // would otherwise be lost every time the camera reopens.
        look_away_reader_(
            ExpressionTracker::Settings{
                .rise = config.expressions_smallest_rise,
                .pose_degrees = config.expressions_pose_degrees,
                .pose_range = config.expressions_pose_range,
                .settle_ms = config.expressions_settle_ms,
                .spreads = config.expressions_spreads,
                .hold_ms = config.expressions_hold_ms,
                .eye_look_down_threshold =
                    config.expressions_eye_look_down_threshold,
                .eye_blink_threshold = config.expressions_eye_blink_threshold},
            EyeGazeTracker::Settings{
                .blink_threshold = config.eye_gaze_blink_threshold,
                .median_ms = config.eye_gaze_median_ms,
                .resting_ms = config.eye_gaze_resting_ms,
                .gap_ms = config.eye_gaze_gap_ms},
            LookAwayDetector::Settings{
                .degrees = config.looking_away_degrees,
                .hold_ms = config.looking_away_hold_ms,
                .averted_ms = config.looking_away_averted_ms,
                .other_screen_margin_degrees =
                    config.looking_away_other_screen_margin_degrees},
            EyeGains{.horizontal = config.eye_gaze_horizontal_gain,
                     .vertical = config.eye_gaze_vertical_gain},
            config.eye_gaze_head_only_yaw_degrees, calibration_profile_),
        // std::in_place, not a temporary moved in: ScreenContextReporter
        // holds a mutex (SetProfile's own, #137), so it is neither
        // copyable nor movable.
        reporter_(
            screen_source_
                ? std::optional<ScreenContextReporter>(
                      std::in_place, *screen_source_,
                      GazeSettings{
                          .other_screen_degrees =
                              config.screen_context_other_screen_degrees,
                          .away_degrees = config.looking_away_degrees},
                      config.screen_context_window_titles_enabled,
                      calibration_profile_,
                      EyeGains{.horizontal = config.eye_gaze_horizontal_gain,
                               .vertical = config.eye_gaze_vertical_gain},
                      config.eye_gaze_head_only_yaw_degrees,
                      ScreenContextReporter::kDefaultMaxWindowMs,
                      EyeSignSettings{
                          .elevated_blinks_per_minute =
                              config.eye_signs_elevated_blinks_per_minute,
                          .long_blink_ms = config.eye_signs_long_blink_ms,
                          .widened_eyes_threshold =
                              config.eye_signs_widened_eyes_threshold,
                          .widened_eyes_hold_ms =
                              config.eye_signs_widened_eyes_hold_ms,
                          .min_window_ms = config.eye_signs_min_window_ms})
                : std::nullopt),
        feelings_(std::move(feelings)),
        status_path_(std::move(status_path)),
        feed_(
            kFeedCapacity, *feelings_, std::chrono::steady_clock::now,
            UnixMilliseconds,
            // #142, #144: BeatGazeWindow is declared below, already
            // built by the time a beat ever calls this.
            [this](std::int64_t start_ms, std::int64_t end_ms,
                   const std::optional<ContentRect>& content) {
              return BeatGazeWindow(start_ms, end_ms, content);
            },
            // #145/#165: BeatEyeSignsWindow is declared below too.
            [this](std::int64_t start_ms, std::int64_t end_ms) {
              return BeatEyeSignsWindow(start_ms, end_ms);
            }),
        clients_(
            [this](std::string summary, std::string detail,
                   std::int64_t published_ms,
                   std::optional<std::string> context) {
              // The camera preview's own readings (#134): the same
              // summary FeedClients::Deliver already built for the
              // reaction feed, held briefly in src/preview_readings.h.
              preview_readings_.OnReading(summary, published_ms);
              feed_.React(std::move(summary), std::move(detail), published_ms,
                          std::move(context));
            },
            UnixMilliseconds,
            [this] {
              return reporter_ ? std::make_optional(reporter_->Json(
                                     screen_context_reader_.Pose(),
                                     screen_context_reader_.RestingPose(),
                                     screen_context_reader_.EyeReading(),
                                     screen_context_reader_.EyeResting()))
                               : std::nullopt;
            }),
        approved_(
            feed_,
            // specs/PreviewLease.tla's `client_demand` (#176):
            // camera_demand_ is called only from the control queue, the
            // same convention CameraController and DaemonControl already
            // follow, so it is never touched from two threads at once
            // alongside the preview's own poll below.
            [this](bool joined) {
              control_queue_(
                  [this, joined] { camera_demand_.SetClientDemand(joined); });
            },
            [this](const std::string& session) {
              feed_.DisconnectAndForget(session);
            },
            // The camera preview's "latest beat's feeling" (#134).
            [this](const Feed::Interpretation& interpretation) {
              preview_readings_.OnBeat(interpretation.feeling,
                                       interpretation.beat);
            }),
        // Calibration (#116, #137): empty with no --home, like the preview
        // paths above. CalibrationSession takes its store by reference, so
        // both are owned here, kept alive as long as calibration_ forwards
        // to the controller they make together. on_finished is #136's own
        // fix: the daemon's own composition root swaps the freshly saved
        // profile into reporter_ at once (ScreenContextReporter::SetProfile),
        // rather than only on the next run.
        calibration_store_(calibration_store_path.empty()
                               ? nullptr
                               : std::make_unique<CalibrationFileStore>(
                                     std::move(calibration_store_path))),
        calibration_session_(
            calibration_store_ != nullptr
                ? std::make_unique<CalibrationSession>(
                      *calibration_store_, CalibrationSettingsFor(config),
                      [this](const CalibrationProfile& profile) {
                        OnCalibrationFinished(profile);
                      })
                : nullptr),
        calibration_(calibration_session_.get()),
        service_(std::move(listener), approved_, approvals,
                 calibration_.Controller(), &capture_),
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
            config_.camera_device_id,
            [this](const Frame& frame) {
              pipeline_.OnFrame(frame);
              preview_publisher_.OnFrame(frame);
              PublishPreviewReadings();
            },
            [this] { camera_.OnSessionFailed(); },
            [this](const std::string& id) { camera_.OnDeviceDisconnected(id); },
            [this](const std::string& id) { camera_.OnDeviceConnected(id); },
            hotplug_monitor_),
#else
        capture_(
            config_.camera_device_id,
            [this](const Frame& frame) {
              pipeline_.OnFrame(frame);
              preview_publisher_.OnFrame(frame);
              PublishPreviewReadings();
            },
            [this] { camera_.OnSessionFailed(); },
            [this](const std::string& id) { camera_.OnDeviceDisconnected(id); },
            [this](const std::string& id) { camera_.OnDeviceConnected(id); }),
#endif
        camera_(capture_, control_, std::move(camera), control_queue_,
                config.frames_per_second),
        controller_(camera_, clients_, true, RetryAfter(control_queue_)),
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

  // CameraStatus!Restart: republishes off before the service can take any
  // subscription (#37 item 8), so a status file left stale by a previous
  // crash does not linger once this one takes over. Calibration.tla's
  // CameraLost (#116): the camera is not yet open at startup, the way
  // CameraController's own kClosed is, overriding CalibrationSession's
  // default of an up camera, which is right only for a session built with
  // no camera signal at all (#96). PreviewLease!GuardStartClearsFrame
  // (#129): a frame a previous crash left behind, while still streaming,
  // is removed the same way, before anything can serve it as though the
  // camera were still being shown.
  void Start() {
    PublishStatus(false);
    if (!preview_frame_path_.empty()) {
      std::remove(preview_frame_path_.c_str());
    }
    // PreviewLease!GuardStartClearsFrame (#129, #134): the readings text
    // a previous crash left behind is removed the same way as the frame.
    if (!preview_readings_path_.empty()) {
      std::remove(preview_readings_path_.c_str());
    }
    calibration_.OnCameraLost();
    // #115 (Linux), #121 (macOS): every member capture_'s device-change
    // callbacks can reach, camera_ among them, is constructed by now;
    // starting any earlier, from capture_'s own constructor, could let
    // the Linux hotplug watcher thread, or macOS's own notification
    // center, call back into one that was not.
    capture_.StartWatching();
    service_.Start();
    StartPreviewDemandPoll();
  }
  void Stop() { control_.Stop(); }
  void Subscribe() override { controller_.Subscribe("approved"); }
  void Leave() override { controller_.Leave("approved"); }
  void Revoke() override { controller_.RevokeConsent(); }
  void Opened() override { controller_.OnOpened(); }
  void OpenFailed(OpenFailure reason) override {
    controller_.OnOpenFailed(reason);
  }
  void Closed() override { controller_.OnClosed(); }
  void DeviceLost() override { controller_.OnDeviceLost(); }
  void DeviceBack() override { controller_.OnDeviceBack(); }
  [[nodiscard]] CameraController::State State() const override {
    return controller_.CurrentState();
  }
  // Calibration.tla's CameraBack (#116): the model is only created once
  // the camera is really open, which is exactly when a session can start
  // capturing poses again.
  void Open() override {
    calibration_.OnCameraUp();
    pipeline_.OnCameraOpen();
  }
  // Calibration.tla's CameraLost (#116): signaled before the model that
  // fed CalibrationReader its poses is destroyed, so a session in
  // progress is cancelled rather than left running with nothing left to
  // capture.
  void Close(std::function<void()> done) override {
    calibration_.OnCameraLost();
    pipeline_.OnCameraClosed(std::move(done));
  }

 private:
  // CalibrationSession's own on_finished callback (#136, #137): swaps the
  // freshly saved profile into reporter_ at once
  // (ScreenContextReporter::SetProfile), rather than only on the next
  // run. A wholly fresh shared_ptr every time (#167), never the one
  // calibration_profile_ already names: reporter_ and look_away_reader_
  // may still be mid-read of that one, on the camera's own thread, with
  // no lock this method's own caller (a connection's thread, inside
  // CalibrationSession's mutex) takes, so the object a reading names
  // must never be mutated in place, only ever replaced wholesale.
  void OnCalibrationFinished(const CalibrationProfile& profile) {
    calibration_profile_ = std::make_shared<const CalibrationProfile>(profile);
    if (reporter_) {
      reporter_->SetProfile(calibration_profile_);
    }
    // #141's own kOtherScreen box test reads the same freshly saved
    // profile at once, rather than only on the next run.
    look_away_reader_.SetProfile(calibration_profile_);
  }

  // screen_context_reader_'s own OnSample callback (#142; eye_sign added
  // by #145/#165): every frame, regardless of whether it carries a
  // reaction, so a beat's window holds every frame's gaze and eye
  // signs. A named method, not the lambda itself, so its own branch does
  // not count toward Engine's constructor (where the lambda is built)'s
  // cognitive complexity.
  void SampleScreenContext(const std::optional<HeadRotation>& pose,
                           const std::optional<HeadRotation>& resting,
                           const std::optional<EyeSignal>& eye,
                           const std::optional<EyeSignal>& eye_resting,
                           const std::optional<EyeSignSample>& eye_sign) {
    if (reporter_) {
      reporter_->Sample(pose, resting, eye, eye_resting, UnixMilliseconds(),
                        eye_sign);
    }
  }

  // feed_'s own GazeWindowFn (#142, #144): what a beat's reply reports
  // for gaze and content. GazeWindow{} (Gaze::kUnknown, no content) with
  // screen context disabled.
  [[nodiscard]] GazeWindow BeatGazeWindow(
      std::int64_t start_ms, std::int64_t end_ms,
      const std::optional<ContentRect>& content) const {
    return reporter_ ? reporter_->Between(start_ms, end_ms, content)
                     : GazeWindow{};
  }

  // feed_'s own EyeSignsWindowFn (#145/#165): what a beat's own personal
  // read tunes on for blink rate, a long blink and widened eyes.
  // EyeSigns{} (every field unknown) with screen context disabled, the
  // same reporter_ gaze shares.
  [[nodiscard]] EyeSigns BeatEyeSignsWindow(std::int64_t start_ms,
                                            std::int64_t end_ms) const {
    return reporter_ ? reporter_->EyeSignsBetween(start_ms, end_ms)
                     : EyeSigns{};
  }

  FramePipeline::Analyzers MakeAnalyzers(std::string& error) {
    auto model = MediaPipeFaceModel::Create(model_, error);
    if (!model) {
      return {};
    }
    FramePipeline::Analyzers analyzers;
    analyzers.push_back(std::make_unique<FaceAnalyzer>(
        std::move(model),
        MakeFaceReaders(config_,
                        screen_source_ ? &screen_context_reader_ : nullptr,
                        calibration_.Controller(), &look_away_reader_)));
    return analyzers;
  }

  // Polls the preview's own request file for camera admission on a
  // thread of its own (#176), the same portable way RetryAfter above
  // schedules a retry: nothing else can tell the daemon the preview
  // wants the camera, since OnFrame's own freshness check
  // (preview_publisher_'s own `requested` callback above) runs only once
  // the camera is already open, which is exactly what is in question
  // here. A daemon with no --home resolved has no request file to poll,
  // the same emptiness PreviewRequestPath above already gives it.
  void StartPreviewDemandPoll() {
    if (preview_request_path_.empty()) {
      return;
    }
    // A member jthread, not a detached one: it captures `this`, so it must
    // stop and be joined before the Engine it points into is destroyed.
    preview_demand_poll_ = std::jthread([this](const std::stop_token& stop) {
      std::mutex sleep_mutex;
      std::condition_variable_any sleep;
      while (!stop.stop_requested()) {
        const bool fresh = IsFreshPreviewRequest(
            preview_request_path_, UnixMilliseconds(), FileModifiedMs);
        control_queue_(
            [this, fresh] { camera_demand_.SetPreviewDemand(fresh); });
        std::unique_lock lock(sleep_mutex);
        sleep.wait_for(lock, stop, kPreviewDemandPollInterval,
                       [] { return false; });
      }
    });
  }

  // CameraStatus!Publish. A daemon started with no --status path (such as
  // a test fixture's) simply runs without one.
  void PublishStatus(bool on) {
    if (status_path_.empty()) {
      return;
    }
    std::string error;
    if (!PublishCameraStatus(status_path_, on, error)) {
      std::fprintf(stderr, "facepalm-daemon: %s\n", error.c_str());
    }
  }

  // PreviewLease!PublishReadings (#134): called once a frame, alongside
  // preview_publisher_'s own OnFrame, so the text beside the preview
  // keeps up with a reading's own hold expiring even when nothing new
  // has happened. Goes through preview_lease_.Publish, the very same
  // gate the frame's own write already takes, so a camera close or
  // Stop landing first is never undone by this call landing after it.
  void PublishPreviewReadings() {
    if (preview_readings_path_.empty()) {
      return;
    }
    const std::int64_t now_ms = UnixMilliseconds();
    preview_lease_.Publish([this, now_ms] {
      return WriteFileAtomically(preview_readings_path_,
                                 preview_readings_.Text(now_ms));
    });
  }

  Post control_queue_;
  std::string model_;
  Config config_;
  std::string preview_request_path_;
  std::string preview_frame_path_;
  std::string preview_readings_path_;
  // src/camera_demand.h (#176): every reason the camera may be wanted,
  // combined into DaemonControl::Subscription's own `joined` boolean.
  // Touched only from control_queue_: approved_'s own callback below and
  // StartPreviewDemandPoll's own posted check.
  CameraDemand camera_demand_;
  PreviewLease preview_lease_;
  PreviewPublisher preview_publisher_;
  PreviewReadings preview_readings_;
  // Outlives any one camera session, unlike MakeFaceReaders' own readers
  // (#61 item 11); nullptr/nullopt with screen_context disabled.
  std::unique_ptr<ScreenSource> screen_source_;
  ScreenContextReader screen_context_reader_;
  // The profile reporter_ and look_away_reader_ read (#61 item 12, #137):
  // a fresh copy, shared ownership, not a borrow of main()'s own local,
  // so calibration_session_'s on_finished callback can replace it at
  // once (#136) rather than only on the next run -- with a wholly new
  // object each time (#167), never mutated in place, so a reader already
  // mid-snapshot of the one before it is never left reading a torn or
  // freed one. Declared before look_away_reader_ and reporter_ so it is
  // constructed first, and both can be given the very same initial
  // shared_ptr their own constructors take below.
  std::shared_ptr<const CalibrationProfile> calibration_profile_;
  // Outlives any one camera session, like screen_context_reader_ above
  // (#141, #61 item 11).
  LookAwayReader look_away_reader_;
  std::optional<ScreenContextReporter> reporter_;
  std::unique_ptr<FeelingStore> feelings_;  // The person's own reads (#35).
  std::string status_path_;
  ReactionFeed feed_;
  FeedClients clients_;
  ApprovedFeed approved_;
  // Calibration (#116): nullptr/nullptr when no store path was resolved
  // (the same shape screen_source_ above already has for its own config
  // flag); CalibrationSession takes its store by reference, so both are
  // owned here. Declared, in order, before calibration_ (built from
  // calibration_session_) and service_ (given calibration_.Controller()).
  std::unique_ptr<CalibrationFileStore> calibration_store_;
  std::unique_ptr<CalibrationSession> calibration_session_;
  DaemonCalibration calibration_;
  Service service_;
  DaemonControl control_;
#ifdef __linux__
  // Declared before capture_, whose constructor keeps a reference to it
  // (#111): a member's constructor runs in declaration order, never
  // initializer-list order, so hotplug_monitor_ must already exist. Its
  // own watch is not started until Start calls capture_.StartWatching,
  // once camera_ below exists too (#115).
  V4l2InotifyMonitor hotplug_monitor_;
  V4l2Session capture_;
#else
  // Its own device-change observers are not registered until Start calls
  // capture_.StartWatching, once camera_ below exists too (#121).
  AvFoundationSession capture_;
#endif
  SessionCamera camera_;
  CameraController controller_;
  FramePipeline pipeline_;
  // Declared last, so it is destroyed first: its destructor requests a
  // stop and joins the poll before any member the poll reads goes away.
  std::jthread preview_demand_poll_;
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

// Lists the cameras available now, for the cameras command (item 10 of
// #61, #13): a throwaway capture session, just for CaptureSession::List,
// which needs no running daemon, socket or approval, so Settings can
// offer a picker whether or not Facepalm is on. One line a camera: its
// id, name and whether it is built in, tab-separated, so the menu apps
// parse it with a plain split rather than a JSON reader this codebase
// otherwise has no reason to carry.
int ListCameras() {
#ifdef __linux__
  // Thrown away with capture itself: it stops watching, with no camera
  // ever having been opened, when this function returns (#111).
  V4l2InotifyMonitor hotplug;
  V4l2Session capture(
      /*device_id=*/{}, [](const Frame&) {}, [] {}, [](const std::string&) {},
      [](const std::string&) {}, hotplug);
#else
  AvFoundationSession capture(
      /*device_id=*/{}, [](const Frame&) {}, [] {}, [](const std::string&) {},
      [](const std::string&) {});
#endif
  for (const CameraDevice& device : capture.List()) {
    std::printf("%s\t%s\t%d\n", device.id.c_str(), device.name.c_str(),
                device.built_in ? 1 : 0);
  }
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
    std::fwrite(facepalm::kDaemonUsage.data(), 1, facepalm::kDaemonUsage.size(),
                stderr);
    return 2;
  }
  if (options->command == facepalm::DaemonOptions::Command::kCameras) {
    return facepalm::ListCameras();
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
  // A saved calibration profile (#61 item 12), if any: optional, so a
  // daemon run before one exists, or with one that cannot be read, simply
  // reports coarse gaze without it.
  std::string calibration_error;
  const std::optional<facepalm::UserPaths> daemon_paths =
      facepalm::PathsFor(home != nullptr ? home : "", facepalm::kThisPlatform);
  const std::optional<facepalm::CalibrationProfile> calibration_profile =
      daemon_paths ? facepalm::LoadCalibrationProfile(daemon_paths->calibration,
                                                      calibration_error)
                   : std::nullopt;
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
  // The chosen camera (item 10 of #61, #13) comes from Config's
  // camera.device_id, not an environment variable: FACEPALM_CAMERA_DEVICE
  // is retired.
  facepalm::Engine engine(std::move(listener), approvals, options->model,
                          config, std::move(feelings),
                          calibration_profile ? &*calibration_profile : nullptr,
                          control.Poster(), camera.Poster(), inference.Poster(),
                          options->status,
                          facepalm::PreviewRequestPath(daemon_paths),
                          facepalm::PreviewFramePath(daemon_paths),
                          facepalm::PreviewReadingsPath(daemon_paths),
                          facepalm::CalibrationStorePath(daemon_paths));
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
      std::move(feelings),
      calibration_profile ? &*calibration_profile : nullptr,
      facepalm::OnQueue(control),
      facepalm::OnQueue(
          dispatch_queue_create("facepalm.camera", DISPATCH_QUEUE_SERIAL)),
      facepalm::OnQueue(
          dispatch_queue_create("facepalm.inference", DISPATCH_QUEUE_SERIAL)),
      options->status, facepalm::PreviewRequestPath(daemon_paths),
      facepalm::PreviewFramePath(daemon_paths),
      facepalm::PreviewReadingsPath(daemon_paths),
      facepalm::CalibrationStorePath(daemon_paths));
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
