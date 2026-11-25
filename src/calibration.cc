#include "src/calibration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/eye_gaze.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {
constexpr double kFullTurnDegrees = 360;
constexpr std::size_t kPointsPerScreen = kCalibrationPoints.size();

// `pose.yaw_degrees` minus `other.yaw_degrees`, wrapped to -180..180, as
// LookAwayDetector compares yaw.
double YawDifference(const HeadRotation& pose, const HeadRotation& other) {
  return std::remainder(pose.yaw_degrees - other.yaw_degrees, kFullTurnDegrees);
}

// How far apart `gaze` and `other` read, by yaw and pitch (#137's spread
// check): the larger axis's own distance alone would undercount a corner
// that moved diagonally, so this is their combined, Euclidean distance.
double GazeDistance(const HeadRotation& gaze, const HeadRotation& other) {
  return std::hypot(YawDifference(gaze, other),
                    gaze.pitch_degrees - other.pitch_degrees);
}

// Adds `pose` into the running mean of `sample_count` poses already
// averaged into `sample`.
void Accumulate(HeadRotation& sample, int& sample_count,
                const HeadRotation& pose) {
  ++sample_count;
  const double weight = 1.0 / sample_count;
  sample.yaw_degrees += (pose.yaw_degrees - sample.yaw_degrees) * weight;
  sample.pitch_degrees += (pose.pitch_degrees - sample.pitch_degrees) * weight;
  sample.roll_degrees += (pose.roll_degrees - sample.roll_degrees) * weight;
}

// As Accumulate, for the running mean of a step's eye signal (#137):
// blink frames never reach this, so sample_count here is the count of
// non-blink frames alone.
void AccumulateEye(EyeSignal& sample, int& sample_count, const EyeSignal& eye) {
  ++sample_count;
  const double weight = 1.0 / sample_count;
  sample.horizontal += (eye.horizontal - sample.horizontal) * weight;
  sample.vertical += (eye.vertical - sample.vertical) * weight;
}

}  // namespace

std::string_view Name(CalibrationPoint point) {
  // In kCalibrationPoints' order.
  constexpr std::array<std::string_view, kPointsPerScreen> kNames = {
      "centre", "top_left", "top_right", "bottom_left", "bottom_right"};
  return kNames.at(static_cast<std::size_t>(point));
}

std::optional<CalibrationPoint> CalibrationPointNamed(std::string_view name) {
  for (const CalibrationPoint point : kCalibrationPoints) {
    if (Name(point) == name) {
      return point;
    }
  }
  return std::nullopt;
}

CalibrationSession::CalibrationSession(
    CalibrationStore& store, CalibrationSettings settings,
    std::function<void(const CalibrationProfile&)> on_finished)
    : store_(store),
      settings_(settings),
      on_finished_(std::move(on_finished)) {}

void CalibrationSession::ResetStep() {
  captured_ = false;
  sample_count_ = 0;
  sample_ = HeadRotation{};
  eye_sample_count_ = 0;
  eye_sample_ = EyeSignal{};
  gains_samples_.clear();
}

void CalibrationSession::ResetSession() {
  step_ = 0;
  ResetStep();
  resting_ = HeadRotation{};
  resting_eye_ = EyeSignal{};
  gains_ = EyeGains{};
}

CalibrationSession::Status CalibrationSession::CurrentStatus() const {
  Status status;
  status.running = true;
  status.index = step_;
  status.total = Total();
  status.resting_step = RestingStep();
  status.gains_step = GainsStep();
  if (!status.resting_step && !status.gains_step) {
    const int target = step_ - kFixedSteps;
    status.screen = target / static_cast<int>(kPointsPerScreen);
    status.point =
        kCalibrationPoints[static_cast<std::size_t>(target) % kPointsPerScreen];
  }
  return status;
}

std::optional<CalibrationSession::Status> CalibrationSession::Start(
    int screens, std::string& error) {
  const std::scoped_lock lock(mutex_);
  if (running_) {
    error = "a calibration session is already running";
    return std::nullopt;
  }
  if (!camera_up_) {
    error = std::string(kCameraUnavailableError);
    return std::nullopt;
  }
  if (screens < 1 || screens > kMaxScreens) {
    error = "calibration takes 1 to 8 screens";
    return std::nullopt;
  }
  running_ = true;
  screens_ = screens;
  points_.assign(static_cast<std::size_t>(screens), ScreenCalibration{});
  ResetSession();
  return CurrentStatus();
}

void CalibrationSession::Update(const HeadRotation& pose,
                                const EyeSignal& eye) {
  const std::scoped_lock lock(mutex_);
  if (!running_) {
    return;
  }
  Accumulate(sample_, sample_count_, pose);
  captured_ = true;
  if (eye.blink) {
    return;
  }
  if (GainsStep()) {
    if (gains_samples_.size() < kMaxGainsSamples) {
      gains_samples_.push_back({.head = pose, .eye = eye});
    }
  } else {
    AccumulateEye(eye_sample_, eye_sample_count_, eye);
  }
}

std::optional<CalibrationSession::Status> CalibrationSession::Next(
    std::string& error) {
  const std::scoped_lock lock(mutex_);
  if (!running_) {
    error = "no calibration session is running";
    return std::nullopt;
  }
  if (!captured_) {
    // The session is still running, waiting on this same step: a refusal
    // (#177), not a failure, so CurrentStatus's own fields report it.
    Status status = CurrentStatus();
    status.refusal = "no pose captured yet for this step";
    return status;
  }
  return AdvanceOrRefuse();
}

CalibrationSession::Status CalibrationSession::AdvanceOrRefuse() {
  if (RestingStep()) {
    // #171: every frame of the resting step blinked, so eye_sample_
    // never accumulated anything -- refused the same way the gains step
    // below refuses too little head travel, rather than saving a
    // placeholder {0, 0} that would bias ComposeGaze's own
    // eye-relative-to-resting term for every later step.
    if (eye_sample_count_ == 0) {
      ResetStep();
      Status status = CurrentStatus();
      status.refusal = "keep your eyes open while you look at the dot";
      return status;
    }
    resting_ = sample_;
    resting_eye_ = eye_sample_;
  } else if (GainsStep()) {
    const std::optional<EstimatedGains> estimated =
        EstimateGains(gains_samples_, settings_.min_head_travel_degrees,
                      settings_.min_axis_degrees);
    if (!estimated) {
      ResetStep();
      Status status = CurrentStatus();
      status.refusal =
          "turn your head further while you keep looking at the dot";
      return status;
    }
    gains_.horizontal =
        estimated->horizontal.value_or(settings_.default_gains.horizontal);
    gains_.vertical =
        estimated->vertical.value_or(settings_.default_gains.vertical);
  } else {
    const int target = step_ - kFixedSteps;
    const auto screen =
        static_cast<std::size_t>(target / static_cast<int>(kPointsPerScreen));
    const auto point = static_cast<std::size_t>(target) % kPointsPerScreen;
    const std::optional<EyeSignal> eye =
        eye_sample_count_ > 0 ? std::make_optional(eye_sample_) : std::nullopt;
    const HeadRotation gaze = ComposeGaze(sample_, eye, resting_eye_, gains_,
                                          settings_.head_only_yaw_degrees);
    if (point != static_cast<std::size_t>(CalibrationPoint::kCentre) &&
        GazeDistance(gaze, points_[screen].points[static_cast<std::size_t>(
                               CalibrationPoint::kCentre)]) <
            settings_.min_spread_degrees) {
      ResetStep();
      Status status = CurrentStatus();
      status.refusal = "the corner reading was too close to the centre reading";
      return status;
    }
    points_[screen].points[point] = gaze;
  }
  if (step_ + 1 >= Total()) {
    const CalibrationProfile profile{.resting = resting_,
                                     .resting_eye = resting_eye_,
                                     .gains = gains_,
                                     .screens = points_};
    std::string save_error;
    if (!store_.Save(profile, save_error)) {
      // The capture stays, so the step can be finished again (the save is
      // the only thing that failed, as specs/Calibration.tla's Finish
      // requires a capture before it ever touches the profile): a
      // refusal (#177), not a failure, so the session stays running on
      // this same, last step for a retry.
      Status status = CurrentStatus();
      status.refusal = save_error;
      return status;
    }
    const int screens_calibrated = screens_;
    running_ = false;
    if (on_finished_) {
      on_finished_(profile);
    }
    Status status;
    status.running = false;
    status.done = true;
    status.screens = screens_calibrated;
    return status;
  }
  ++step_;
  ResetStep();
  return CurrentStatus();
}

bool CalibrationSession::Cancel(std::string& error) {
  const std::scoped_lock lock(mutex_);
  if (!running_) {
    error = "no calibration session is running";
    return false;
  }
  running_ = false;
  ResetSession();
  return true;
}

void CalibrationSession::OnCameraLost() {
  const std::scoped_lock lock(mutex_);
  camera_up_ = false;
  if (running_) {
    // As Cancel, since there is nothing left to capture; the saved profile
    // is untouched either way.
    running_ = false;
    ResetSession();
  }
}

void CalibrationSession::OnCameraUp() {
  const std::scoped_lock lock(mutex_);
  camera_up_ = true;
}

std::optional<std::size_t> NearestCalibratedScreen(
    const CalibrationProfile& profile, const HeadRotation& gaze) {
  if (profile.screens.empty()) {
    return std::nullopt;
  }
  std::optional<std::size_t> nearest;
  double nearest_distance = 0;
  for (std::size_t index = 0; index < profile.screens.size(); ++index) {
    const HeadRotation& centre =
        profile.screens[index]
            .points[static_cast<std::size_t>(CalibrationPoint::kCentre)];
    const double distance = GazeDistance(gaze, centre);
    if (!nearest || distance < nearest_distance) {
      nearest = index;
      nearest_distance = distance;
    }
  }
  return nearest;
}

std::optional<std::size_t> CalibratedScreenBoxContaining(
    const CalibrationProfile& profile, const HeadRotation& gaze,
    double margin_degrees) {
  for (std::size_t index = 0; index < profile.screens.size(); ++index) {
    const ScreenCalibration& screen = profile.screens[index];
    double min_yaw = 0;
    double max_yaw = 0;
    double min_pitch = 0;
    double max_pitch = 0;
    bool first = true;
    for (const CalibrationPoint point : kCalibrationPoints) {
      if (point == CalibrationPoint::kCentre) {
        continue;  // The box is the four corners' own span.
      }
      const HeadRotation& corner =
          screen.points[static_cast<std::size_t>(point)];
      if (first) {
        min_yaw = corner.yaw_degrees;
        max_yaw = corner.yaw_degrees;
        min_pitch = corner.pitch_degrees;
        max_pitch = corner.pitch_degrees;
        first = false;
      } else {
        min_yaw = std::min(min_yaw, corner.yaw_degrees);
        max_yaw = std::max(max_yaw, corner.yaw_degrees);
        min_pitch = std::min(min_pitch, corner.pitch_degrees);
        max_pitch = std::max(max_pitch, corner.pitch_degrees);
      }
    }
    if (gaze.yaw_degrees >= min_yaw - margin_degrees &&
        gaze.yaw_degrees <= max_yaw + margin_degrees &&
        gaze.pitch_degrees >= min_pitch - margin_degrees &&
        gaze.pitch_degrees <= max_pitch + margin_degrees) {
      return index;
    }
  }
  return std::nullopt;
}

}  // namespace facepalm
