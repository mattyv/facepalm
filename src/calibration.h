#ifndef FACEPALM_SRC_CALIBRATION_H_
#define FACEPALM_SRC_CALIBRATION_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/eye_gaze.h"
#include "src/head_pose.h"

namespace facepalm {

// CalibrationController::Start's error, and CalibrationFlow::Start's
// (src/calibration_flow.cc) one retryable reason, when the camera the
// session's poses would come from is not open yet (#128): the one error
// text both sides of that cold-start retry agree on, so a shared constant
// replaces matching a literal twice.
inline constexpr std::string_view kCameraUnavailableError =
    "the camera is not available";

// One target a calibration session walks the person through, after the
// resting pose and the gains step: a screen's centre, then its four
// corners (#36, #35).
enum class CalibrationPoint : std::uint8_t {
  kCentre,
  kTopLeft,
  kTopRight,
  kBottomLeft,
  kBottomRight,
};
// Every CalibrationPoint, in the order a session prompts them.
inline constexpr std::array<CalibrationPoint, 5> kCalibrationPoints = {
    CalibrationPoint::kCentre, CalibrationPoint::kTopLeft,
    CalibrationPoint::kTopRight, CalibrationPoint::kBottomLeft,
    CalibrationPoint::kBottomRight};

// "centre", "top_left", "top_right", "bottom_left" or "bottom_right".
std::string_view Name(CalibrationPoint point);
// The point `name` names, or nullopt.
std::optional<CalibrationPoint> CalibrationPointNamed(std::string_view name);

// One screen's calibrated gaze angle (src/eye_gaze.h's ComposeGaze, head
// pose composed with eye-in-head signal, #137) at its centre and four
// corners, indexed by CalibrationPoint's position in kCalibrationPoints.
struct ScreenCalibration {
  std::array<HeadRotation, kCalibrationPoints.size()> points{};
};

// What a calibration session records (#35, #36, #137): the resting head
// pose and eye signal, the gains the session's own fixate-and-turn step
// estimated (a default-gain axis when that axis could not be measured),
// and each screen's points, numbers only, never images.
struct CalibrationProfile {
  HeadRotation resting;
  EyeSignal resting_eye;
  EyeGains gains;
  std::vector<ScreenCalibration> screens;
};

// Saves CalibrationProfile, replaceable so a session can be tested without
// touching a file.
class CalibrationStore {
 public:
  virtual ~CalibrationStore() = default;
  // Saves `profile` atomically, replacing any profile already there. False,
  // with `error` saying why, when it could not be saved; the file this
  // store owns is then unchanged.
  virtual bool Save(const CalibrationProfile& profile, std::string& error) = 0;
};

// What the service asks of a calibration session (#36, #35, item 12 of
// #61, #137); CalibrationSession implements it, and specs/Calibration.tla
// describes it.
class CalibrationController {
 public:
  // The step a running session now prompts, or that it has ended.
  struct Status {
    bool running = false;
    bool done = false;          // True only in the reply that ends a session.
    int index = 0;              // 0-based, while running.
    int total = 0;              // Steps this session has, while running.
    bool resting_step = false;  // Whether the running step is the resting pose.
    bool gains_step = false;    // Whether the running step is #137's gains
                                // step (fixate a dot, turn the head slowly).
    int screen = 0;             // 0-based, when neither resting nor gains.
    CalibrationPoint point = CalibrationPoint::kCentre;  // When a screen step.
    int screens = 0;  // Screens just calibrated; set only when done.
  };

  virtual ~CalibrationController() = default;

  // Starts a session of `screens` screens (1 to
  // CalibrationSession::kMaxScreens). nullopt, with `error`, when one is
  // already running or `screens` is out of range.
  virtual std::optional<Status> Start(int screens, std::string& error) = 0;
  // Finishes the step now prompted and moves to the next, saving the
  // profile and ending the session once the last is finished. nullopt,
  // with `error`, when no session is running, the step has not yet
  // captured a sample, or the sample is too weak to use (#137: the gains
  // step with too little head travel, or a screen's point too close to
  // its own centre) -- either way the step stays where it is, waiting on
  // a fresh capture.
  virtual std::optional<Status> Next(std::string& error) = 0;
  // Discards the session in progress; the saved profile is untouched.
  // False, with `error`, when no session is running.
  virtual bool Cancel(std::string& error) = 0;
  // The gaze pose and eye signal read this frame; ignored unless a
  // session is running. A caller (such as a test) that only cares about
  // head pose passes a default-constructed EyeSignal{}: Google style
  // forbids a default argument here (it is a virtual method).
  virtual void Update(const HeadRotation& pose, const EyeSignal& eye) = 0;
  // The camera the session's samples come from has gone down: cancels a
  // running session, since there is nothing left to capture, and refuses
  // Start until OnCameraUp. Whoever owns the camera's lifecycle (the
  // daemon's composition root, not a per-frame reader) calls this and
  // OnCameraUp.
  virtual void OnCameraLost() = 0;
  virtual void OnCameraUp() = 0;
};

// Tunable thresholds and defaults for CalibrationSession (#137), Config's
// own (src/config.h); every member has a default so a test that does not
// care about one can leave it out. A free-standing struct, not a nested
// one: a nested Settings's own default member initializers cannot be
// evaluated for a defaulted constructor parameter of the enclosing,
// still-incomplete class.
struct CalibrationSettings {
  // Matches Config's own kDefaultEyeGazeHorizontalGain/VerticalGain
  // (src/config.h); named here too since this struct's own defaults
  // stand alone, for a caller (such as a test) that builds one without
  // going through Config at all.
  static constexpr double kDefaultGain = 30;
  static constexpr double kDefaultMinHeadTravelDegrees = 10;
  static constexpr double kDefaultMinAxisDegrees = 4;
  static constexpr double kDefaultMinSpreadDegrees = 3;
  static constexpr double kDefaultHeadOnlyYawDegrees = 35;

  EyeGains default_gains{.horizontal = kDefaultGain, .vertical = kDefaultGain};
  // #137's "refuse too little head travel": the gains step's own
  // combined (the larger of yaw's and pitch's own range) head travel
  // must reach this to be accepted at all.
  double min_head_travel_degrees = kDefaultMinHeadTravelDegrees;
  // Within an accepted gains step, an axis under this much of its own
  // head range keeps the default gain instead of an estimated one
  // (src/eye_gaze.h's EstimateGains).
  double min_axis_degrees = kDefaultMinAxisDegrees;
  // #137's spread check: a screen's corner must read at least this many
  // degrees of gaze from that screen's own centre to be accepted.
  double min_spread_degrees = kDefaultMinSpreadDegrees;
  // src/eye_gaze.h's ComposeGaze: beyond this much head yaw, gaze is head
  // pose alone.
  double head_only_yaw_degrees = kDefaultHeadOnlyYawDegrees;
};

// A short guided calibration session (#36, #35, item 12 of #61, #137),
// following specs/Calibration.tla: Start is its Start, Update its
// CaptureGood/CaptureWeak, Next its Advance, Finish or Refuse, Cancel its
// Cancel, and OnCameraLost and OnCameraUp its CameraLost and CameraBack.
// Every one of those runs under one mutex (#128): Start, Next and Cancel
// reach this from a connection's own thread (src/service.cc), Update from
// the inference queue, and OnCameraLost and OnCameraUp from the daemon's
// control queue (src/daemon_calibration.h), with no other ordering
// between any two of them, so each below is made the single indivisible
// step specs/Calibration.tla's own actions already assume it is.
class CalibrationSession : public CalibrationController {
 public:
  static constexpr int kMaxScreens = 8;

  // `on_finished`, when given, is called with the freshly saved profile
  // once a session finishes (specs/Calibration.tla's Finish, with
  // GuardApplyImmediately on): the daemon's composition root
  // (tools/daemon/main.cc) uses it to swap the profile
  // ScreenContextReporter reads at once, rather than only on the next
  // run (#136). Called synchronously, with mutex_ held, so it must not
  // call back into this session.
  explicit CalibrationSession(
      CalibrationStore& store, CalibrationSettings settings = {},
      std::function<void(const CalibrationProfile&)> on_finished = {});

  std::optional<Status> Start(int screens, std::string& error) override;
  std::optional<Status> Next(std::string& error) override;
  bool Cancel(std::string& error) override;
  void Update(const HeadRotation& pose, const EyeSignal& eye) override;
  void OnCameraLost() override;
  void OnCameraUp() override;

 private:
  // Resets every piece of per-session state that a fresh Start, or a
  // Cancel/CameraLost that ends one, leaves behind.
  void ResetSession();
  // Resets the running step's own capture, the way a fresh Start's first
  // step, Advance's next one, or a Refuse's retry does.
  void ResetStep();
  [[nodiscard]] int Total() const {
    return kFixedSteps +
           (screens_ * static_cast<int>(kCalibrationPoints.size()));
  }
  [[nodiscard]] Status CurrentStatus() const;
  // Next's own body once a sample is captured: nullopt, with `error`, for
  // a weak step (resetting it for a retry) or a save failure; the
  // Status to return otherwise. Lower cognitive complexity than folding
  // this into Next itself.
  std::optional<Status> AdvanceOrRefuse(std::string& error);
  // Whether the step now captured is too weak to use (#137), and the
  // gain or point it would otherwise have stored.
  [[nodiscard]] bool RestingStep() const { return step_ == kRestingStep; }
  [[nodiscard]] bool GainsStep() const { return step_ == kGainsStep; }

  static constexpr int kRestingStep = 0;
  static constexpr int kGainsStep = 1;
  static constexpr int kFixedSteps = 2;
  // Bounds the gains step's own sample buffer (#137): comfortably more
  // than a several-second turn at any frame rate this daemon runs,
  // without growing without limit while a person lingers on the step.
  static constexpr std::size_t kMaxGainsSamples = 1024;

  std::mutex mutex_;
  CalibrationStore& store_;
  CalibrationSettings settings_;
  std::function<void(const CalibrationProfile&)> on_finished_;
  bool running_ = false;
  bool camera_up_ = true;
  int screens_ = 0;
  int step_ = 0;  // 0..Total()-1, while running_.
  bool captured_ = false;
  int sample_count_ = 0;
  HeadRotation sample_;  // The running step's head average so far.
  int eye_sample_count_ = 0;
  EyeSignal eye_sample_;  // The running step's eye average so far (blinks
                          // excluded).
  std::vector<GainsSample> gains_samples_;  // The gains step's own buffer.
  HeadRotation resting_;
  EyeSignal resting_eye_;
  EyeGains gains_;
  std::vector<ScreenCalibration> points_;
};

// The index of the screen in `profile` whose centre gaze is nearest to
// `gaze` (src/eye_gaze.h's ComposeGaze, not head pose alone, #137), by
// yaw and pitch; nullopt when `profile` has no screens. Coarse gaze (#36)
// uses this when a profile is present, instead of its uncalibrated,
// screen-only guess.
std::optional<std::size_t> NearestCalibratedScreen(
    const CalibrationProfile& profile, const HeadRotation& gaze);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATION_H_
