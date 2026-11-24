#include "src/daemon_engine_support.h"

#include <memory>
#include <optional>
#include <string>

#include "src/calibration.h"
#include "src/config.h"
#include "src/user_paths.h"

namespace facepalm {

std::string PathOrEmpty(const std::optional<UserPaths>& daemon_paths,
                        std::string UserPaths::* member) {
  return daemon_paths ? (*daemon_paths).*member : std::string{};
}

CalibrationSettings CalibrationSettingsFromConfig(const Config& config) {
  return CalibrationSettings{
      .default_gains = {.horizontal = config.eye_gaze_horizontal_gain,
                        .vertical = config.eye_gaze_vertical_gain},
      .min_head_travel_degrees = config.calibration_min_head_travel_degrees,
      .min_axis_degrees = config.calibration_min_axis_degrees,
      .min_spread_degrees = config.calibration_min_spread_degrees,
      .head_only_yaw_degrees = config.eye_gaze_head_only_yaw_degrees};
}

std::shared_ptr<const CalibrationProfile> SharedCalibrationProfile(
    const CalibrationProfile* profile) {
  if (profile == nullptr) {
    return nullptr;
  }
  return std::make_shared<const CalibrationProfile>(*profile);
}

}  // namespace facepalm
