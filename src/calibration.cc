#include "src/calibration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

CalibrationSession::CalibrationSession(CalibrationStore& store)
    : store_(store) {}

CalibrationSession::Status CalibrationSession::CurrentStatus() const {
  Status status;
  status.running = true;
  status.index = step_;
  status.total = Total();
  status.resting_step = step_ == 0;
  if (!status.resting_step) {
    const int target = step_ - 1;
    status.screen = target / static_cast<int>(kPointsPerScreen);
    status.point =
        kCalibrationPoints[static_cast<std::size_t>(target) % kPointsPerScreen];
  }
  return status;
}

std::optional<CalibrationSession::Status> CalibrationSession::Start(
    int screens, std::string& error) {
  if (running_) {
    error = "a calibration session is already running";
    return std::nullopt;
  }
  if (!camera_up_) {
    error = "the camera is not available";
    return std::nullopt;
  }
  if (screens < 1 || screens > kMaxScreens) {
    error = "calibration takes 1 to 8 screens";
    return std::nullopt;
  }
  running_ = true;
  screens_ = screens;
  step_ = 0;
  captured_ = false;
  sample_count_ = 0;
  sample_ = HeadRotation{};
  resting_ = HeadRotation{};
  points_.assign(static_cast<std::size_t>(screens), ScreenCalibration{});
  return CurrentStatus();
}

void CalibrationSession::Update(const HeadRotation& pose) {
  if (!running_) {
    return;
  }
  Accumulate(sample_, sample_count_, pose);
  captured_ = true;
}

std::optional<CalibrationSession::Status> CalibrationSession::Next(
    std::string& error) {
  if (!running_) {
    error = "no calibration session is running";
    return std::nullopt;
  }
  if (!captured_) {
    error = "no pose captured yet for this step";
    return std::nullopt;
  }
  if (step_ == 0) {
    resting_ = sample_;
  } else {
    const int target = step_ - 1;
    const auto screen =
        static_cast<std::size_t>(target / static_cast<int>(kPointsPerScreen));
    const auto point = static_cast<std::size_t>(target) % kPointsPerScreen;
    points_[screen].points[point] = sample_;
  }
  if (step_ + 1 >= Total()) {
    const CalibrationProfile profile{.resting = resting_, .screens = points_};
    std::string save_error;
    if (!store_.Save(profile, save_error)) {
      // The capture stays, so the step can be finished again (the save is
      // the only thing that failed, as specs/Calibration.tla's Finish
      // requires a capture before it ever touches the profile).
      error = save_error;
      return std::nullopt;
    }
    running_ = false;
    Status status;
    status.running = false;
    status.done = true;
    return status;
  }
  ++step_;
  captured_ = false;
  sample_count_ = 0;
  sample_ = HeadRotation{};
  return CurrentStatus();
}

bool CalibrationSession::Cancel(std::string& error) {
  if (!running_) {
    error = "no calibration session is running";
    return false;
  }
  running_ = false;
  step_ = 0;
  captured_ = false;
  sample_count_ = 0;
  sample_ = HeadRotation{};
  return true;
}

void CalibrationSession::OnCameraLost() {
  camera_up_ = false;
  if (running_) {
    // As Cancel, since there is nothing left to capture; the saved profile
    // is untouched either way.
    running_ = false;
    step_ = 0;
    captured_ = false;
    sample_count_ = 0;
    sample_ = HeadRotation{};
  }
}

void CalibrationSession::OnCameraUp() { camera_up_ = true; }

std::optional<std::size_t> NearestCalibratedScreen(
    const CalibrationProfile& profile, const HeadRotation& pose) {
  if (profile.screens.empty()) {
    return std::nullopt;
  }
  std::optional<std::size_t> nearest;
  double nearest_distance = 0;
  for (std::size_t index = 0; index < profile.screens.size(); ++index) {
    const HeadRotation& centre =
        profile.screens[index]
            .points[static_cast<std::size_t>(CalibrationPoint::kCentre)];
    const double yaw_off = YawDifference(pose, centre);
    const double pitch_off = pose.pitch_degrees - centre.pitch_degrees;
    const double distance = std::hypot(yaw_off, pitch_off);
    if (!nearest || distance < nearest_distance) {
      nearest = index;
      nearest_distance = distance;
    }
  }
  return nearest;
}

}  // namespace facepalm
