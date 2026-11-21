#ifndef FACEPALM_SRC_CALIBRATION_H_
#define FACEPALM_SRC_CALIBRATION_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/head_pose.h"

namespace facepalm {

// One target a calibration session walks the person through, after the
// resting pose: a screen's centre, then its four corners (#36, #35).
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

// One screen's calibrated head pose at its centre and four corners, indexed
// by CalibrationPoint's position in kCalibrationPoints.
struct ScreenCalibration {
  std::array<HeadRotation, kCalibrationPoints.size()> points{};
};

// What a calibration session records (#35, #36): the resting pose and each
// screen's points, numbers only, never images.
struct CalibrationProfile {
  HeadRotation resting;
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
// #61); CalibrationSession implements it, and specs/Calibration.tla
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
    int screen = 0;             // 0-based, when not the resting step.
    CalibrationPoint point = CalibrationPoint::kCentre;  // When not resting.
  };

  virtual ~CalibrationController() = default;

  // Starts a session of `screens` screens (1 to
  // CalibrationSession::kMaxScreens). nullopt, with `error`, when one is
  // already running or `screens` is out of range.
  virtual std::optional<Status> Start(int screens, std::string& error) = 0;
  // Finishes the step now prompted and moves to the next, saving the
  // profile and ending the session once the last is finished. nullopt, with
  // `error`, when no session is running or the step has not yet captured a
  // pose.
  virtual std::optional<Status> Next(std::string& error) = 0;
  // Discards the session in progress; the saved profile is untouched.
  // False, with `error`, when no session is running.
  virtual bool Cancel(std::string& error) = 0;
  // The pose read this frame; ignored unless a session is running.
  virtual void Update(const HeadRotation& pose) = 0;
  // The camera the session's poses come from has gone down: cancels a
  // running session, since there is nothing left to capture, and refuses
  // Start until OnCameraUp. Whoever owns the camera's lifecycle (the
  // daemon's composition root, not a per-frame reader) calls this and
  // OnCameraUp.
  virtual void OnCameraLost() = 0;
  virtual void OnCameraUp() = 0;
};

// A short guided calibration session (#36, #35, item 12 of #61), following
// specs/Calibration.tla: Start is its Start, Update its Capture, Next its
// Advance or Finish, Cancel its Cancel, and OnCameraLost and OnCameraUp its
// CameraLost and CameraBack.
class CalibrationSession : public CalibrationController {
 public:
  static constexpr int kMaxScreens = 8;

  explicit CalibrationSession(CalibrationStore& store);

  std::optional<Status> Start(int screens, std::string& error) override;
  std::optional<Status> Next(std::string& error) override;
  bool Cancel(std::string& error) override;
  void Update(const HeadRotation& pose) override;
  void OnCameraLost() override;
  void OnCameraUp() override;

 private:
  [[nodiscard]] int Total() const {
    return 1 + (screens_ * static_cast<int>(kCalibrationPoints.size()));
  }
  [[nodiscard]] Status CurrentStatus() const;

  CalibrationStore& store_;
  bool running_ = false;
  bool camera_up_ = true;
  int screens_ = 0;
  int step_ = 0;  // 0..Total()-1, while running_.
  bool captured_ = false;
  int sample_count_ = 0;
  HeadRotation sample_;  // The running step's average so far.
  HeadRotation resting_;
  std::vector<ScreenCalibration> points_;
};

// The index of the screen in `profile` whose centre point is nearest to
// `pose`, by yaw and pitch; nullopt when `profile` has no screens. Coarse
// gaze (#36) uses this when a profile is present, instead of its
// uncalibrated, screen-only guess.
std::optional<std::size_t> NearestCalibratedScreen(
    const CalibrationProfile& profile, const HeadRotation& pose);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATION_H_
