#include "src/config.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

namespace facepalm {
namespace {

constexpr int kMostFramesPerSecond = 60;
constexpr float kMostExpressionsSpreads = 100;
constexpr int kMostExpressionsHoldMs = 5000;
constexpr float kMostExpressionsPoseDegrees = 180;
constexpr float kMostExpressionsPoseRange = 180;
constexpr int kMostExpressionsSettleMs = 5000;
constexpr float kMostLookingAwayDegrees = 180;
constexpr int kMostLookingAwayHoldMs = 10000;
constexpr int kMostLookingAwayAvertedMs = 30000;
constexpr float kMostScreenContextOtherScreenDegrees = 180;
constexpr float kMostEyeGazeGain = 200;
constexpr int kMostEyeGazeMs = 10000;
constexpr float kMostDegrees = 180;
constexpr float kMostEyeSignsBlinksPerMinute = 120;
constexpr int kMostEyeSignsMs = 10000;

// `text` without spaces or tabs at either end.
std::string_view Trim(std::string_view text) {
  constexpr std::string_view kBlank = " \t\r";
  const std::size_t first = text.find_first_not_of(kBlank);
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(kBlank) - first + 1);
}

// Reads all of `text` as a T, or nothing.
template <typename T>
std::optional<T> Number(std::string_view text) {
  T value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<bool> Boolean(std::string_view text) {
  if (text == "true") {
    return true;
  }
  if (text == "false") {
    return false;
  }
  return std::nullopt;
}

// One `name = value` line.
struct Setting {
  std::string_view name;
  std::string_view value;
};

// Sets camera.frames_per_second from `value`, or returns why it cannot.
std::optional<std::string> SetFramesPerSecond(std::string_view value,
                                              Config& config) {
  const std::optional<int> rate = Number<int>(value);
  if (!rate || *rate < 1 || *rate > kMostFramesPerSecond) {
    return "expected a whole number from 1 to 60";
  }
  config.frames_per_second = *rate;
  return std::nullopt;
}

// Sets camera.device_id from `value` (item 10 of #61, #13): any text, a
// stable ID some CaptureSession may one day report, which this file does
// not know the shape of; blank, the default, chooses the built-in camera.
// Settings (`UpdateSettingsDocument`) writes every setting it manages,
// including this one when the default is chosen, so blank has to parse.
std::optional<std::string> SetCameraDeviceId(std::string_view value,
                                             Config& config) {
  config.camera_device_id = std::string(value);
  return std::nullopt;
}

// Sets the on/off setting `setting.name` names from `setting.value`, or
// returns why it cannot. `setting.name` is one of head_pose.enabled,
// gestures.enabled, expressions.enabled, looking_away.enabled,
// screen_context.enabled or screen_context.window_titles_enabled, as `Set`
// has already checked.
std::optional<std::string> SetSwitch(const Setting& setting, Config& config) {
  const std::optional<bool> enabled = Boolean(setting.value);
  if (!enabled) {
    return "expected true or false";
  }
  if (setting.name == "head_pose.enabled") {
    config.head_pose_enabled = *enabled;
  } else if (setting.name == "gestures.enabled") {
    config.gestures_enabled = *enabled;
  } else if (setting.name == "looking_away.enabled") {
    config.looking_away_enabled = *enabled;
  } else if (setting.name == "screen_context.enabled") {
    config.screen_context_enabled = *enabled;
  } else if (setting.name == "screen_context.window_titles_enabled") {
    config.screen_context_window_titles_enabled = *enabled;
  } else {
    config.expressions_enabled = *enabled;
  }
  return std::nullopt;
}

// Sets gestures.confidence_threshold from `value`, or returns why it
// cannot.
std::optional<std::string> SetGestureConfidenceThreshold(std::string_view value,
                                                         Config& config) {
  const std::optional<float> threshold = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!threshold || std::isnan(*threshold) || *threshold < 0 ||
      *threshold > 1) {
    return "expected a number from 0 to 1";
  }
  config.gesture_confidence_threshold = *threshold;
  return std::nullopt;
}

// Sets expressions.smallest_rise from `value`, or returns why it cannot.
std::optional<std::string> SetExpressionsSmallestRise(std::string_view value,
                                                      Config& config) {
  const std::optional<float> rise = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!rise || std::isnan(*rise) || *rise < 0 || *rise > 1) {
    return "expected a number from 0 to 1";
  }
  config.expressions_smallest_rise = *rise;
  return std::nullopt;
}

// Sets expressions.spreads from `value`, or returns why it cannot.
std::optional<std::string> SetExpressionsSpreads(std::string_view value,
                                                 Config& config) {
  const std::optional<float> spreads = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!spreads || std::isnan(*spreads) || *spreads < 0 ||
      *spreads > kMostExpressionsSpreads) {
    return "expected a number from 0 to 100";
  }
  config.expressions_spreads = *spreads;
  return std::nullopt;
}

// Sets expressions.hold_ms from `value`, or returns why it cannot.
std::optional<std::string> SetExpressionsHoldMs(std::string_view value,
                                                Config& config) {
  const std::optional<int> hold_ms = Number<int>(value);
  if (!hold_ms || *hold_ms < 0 || *hold_ms > kMostExpressionsHoldMs) {
    return "expected a whole number from 0 to 5000";
  }
  config.expressions_hold_ms = *hold_ms;
  return std::nullopt;
}

// Sets expressions.pose_degrees from `value`, or returns why it cannot.
std::optional<std::string> SetExpressionsPoseDegrees(std::string_view value,
                                                     Config& config) {
  const std::optional<float> degrees = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!degrees || std::isnan(*degrees) || *degrees < 0 ||
      *degrees > kMostExpressionsPoseDegrees) {
    return "expected a number from 0 to 180";
  }
  config.expressions_pose_degrees = *degrees;
  return std::nullopt;
}

// Sets expressions.pose_range from `value`, or returns why it cannot.
std::optional<std::string> SetExpressionsPoseRange(std::string_view value,
                                                   Config& config) {
  const std::optional<float> range = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!range || std::isnan(*range) || *range < 0 ||
      *range > kMostExpressionsPoseRange) {
    return "expected a number from 0 to 180";
  }
  config.expressions_pose_range = *range;
  return std::nullopt;
}

// Sets expressions.settle_ms from `value`, or returns why it cannot.
std::optional<std::string> SetExpressionsSettleMs(std::string_view value,
                                                  Config& config) {
  const std::optional<int> settle_ms = Number<int>(value);
  if (!settle_ms || *settle_ms < 0 || *settle_ms > kMostExpressionsSettleMs) {
    return "expected a whole number from 0 to 5000";
  }
  config.expressions_settle_ms = *settle_ms;
  return std::nullopt;
}

// Sets expressions.eye_look_down_threshold from `value`, or returns why it
// cannot.
std::optional<std::string> SetExpressionsEyeLookDownThreshold(
    std::string_view value, Config& config) {
  const std::optional<float> threshold = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!threshold || std::isnan(*threshold) || *threshold < 0 ||
      *threshold > 1) {
    return "expected a number from 0 to 1";
  }
  config.expressions_eye_look_down_threshold = *threshold;
  return std::nullopt;
}

// Sets expressions.eye_blink_threshold from `value`, or returns why it
// cannot.
std::optional<std::string> SetExpressionsEyeBlinkThreshold(
    std::string_view value, Config& config) {
  const std::optional<float> threshold = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!threshold || std::isnan(*threshold) || *threshold < 0 ||
      *threshold > 1) {
    return "expected a number from 0 to 1";
  }
  config.expressions_eye_blink_threshold = *threshold;
  return std::nullopt;
}

// Sets looking_away.degrees from `value`, or returns why it cannot.
std::optional<std::string> SetLookingAwayDegrees(std::string_view value,
                                                 Config& config) {
  const std::optional<float> degrees = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!degrees || std::isnan(*degrees) || *degrees < 0 ||
      *degrees > kMostLookingAwayDegrees) {
    return "expected a number from 0 to 180";
  }
  config.looking_away_degrees = *degrees;
  return std::nullopt;
}

// Sets looking_away.hold_ms from `value`, or returns why it cannot.
std::optional<std::string> SetLookingAwayHoldMs(std::string_view value,
                                                Config& config) {
  const std::optional<int> hold_ms = Number<int>(value);
  if (!hold_ms || *hold_ms < 0 || *hold_ms > kMostLookingAwayHoldMs) {
    return "expected a whole number from 0 to 10000";
  }
  config.looking_away_hold_ms = *hold_ms;
  return std::nullopt;
}

// Sets looking_away.averted_ms (#141) from `value`, or returns why it
// cannot.
std::optional<std::string> SetLookingAwayAvertedMs(std::string_view value,
                                                   Config& config) {
  const std::optional<int> averted_ms = Number<int>(value);
  if (!averted_ms || *averted_ms < 0 ||
      *averted_ms > kMostLookingAwayAvertedMs) {
    return "expected a whole number from 0 to 30000";
  }
  config.looking_away_averted_ms = *averted_ms;
  return std::nullopt;
}

// Sets screen_context.other_screen_degrees from `value`, or returns why it
// cannot.
std::optional<std::string> SetScreenContextOtherScreenDegrees(
    std::string_view value, Config& config) {
  const std::optional<float> degrees = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!degrees || std::isnan(*degrees) || *degrees < 0 ||
      *degrees > kMostScreenContextOtherScreenDegrees) {
    return "expected a number from 0 to 180";
  }
  config.screen_context_other_screen_degrees = *degrees;
  return std::nullopt;
}

// Sets the on/off eye_gaze.horizontal_gain or eye_gaze.vertical_gain
// (#137) from `value`, or returns why it cannot. `setting.name` is one of
// those two, as `Set` has already checked.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<std::string> SetEyeGazeGain(const Setting& setting,
                                          Config& config) {
  const std::optional<float> gain = Number<float>(setting.value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!gain || std::isnan(*gain) || *gain < 0 || *gain > kMostEyeGazeGain) {
    return "expected a number from 0 to 200";
  }
  if (setting.name == "eye_gaze.horizontal_gain") {
    config.eye_gaze_horizontal_gain = *gain;
  } else {
    config.eye_gaze_vertical_gain = *gain;
  }
  return std::nullopt;
}

// Sets eye_gaze.blink_threshold from `value`, or returns why it cannot.
std::optional<std::string> SetEyeGazeBlinkThreshold(std::string_view value,
                                                    Config& config) {
  const std::optional<float> threshold = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!threshold || std::isnan(*threshold) || *threshold < 0 ||
      *threshold > 1) {
    return "expected a number from 0 to 1";
  }
  config.eye_gaze_blink_threshold = *threshold;
  return std::nullopt;
}

// Sets eye_gaze.median_ms, eye_gaze.resting_ms or eye_gaze.gap_ms (#137)
// from `value`, or returns why it cannot. `setting.name` is one of those
// three, as `Set` has already checked.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<std::string> SetEyeGazeMs(const Setting& setting,
                                        Config& config) {
  const std::optional<int> milliseconds = Number<int>(setting.value);
  if (!milliseconds || *milliseconds < 0 || *milliseconds > kMostEyeGazeMs) {
    return "expected a whole number from 0 to 10000";
  }
  if (setting.name == "eye_gaze.median_ms") {
    config.eye_gaze_median_ms = *milliseconds;
  } else if (setting.name == "eye_gaze.resting_ms") {
    config.eye_gaze_resting_ms = *milliseconds;
  } else {
    config.eye_gaze_gap_ms = *milliseconds;
  }
  return std::nullopt;
}

// Sets eye_gaze.head_only_yaw_degrees, calibration.min_head_travel_degrees,
// calibration.min_axis_degrees, calibration.min_spread_degrees (#137) or
// looking_away.other_screen_margin_degrees (#141) from `value`, or returns
// why it cannot. `setting.name` is one of those five, as `Set` has
// already checked.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<std::string> SetDegreesSetting(const Setting& setting,
                                             Config& config) {
  const std::optional<float> degrees = Number<float>(setting.value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!degrees || std::isnan(*degrees) || *degrees < 0 ||
      *degrees > kMostDegrees) {
    return "expected a number from 0 to 180";
  }
  if (setting.name == "eye_gaze.head_only_yaw_degrees") {
    config.eye_gaze_head_only_yaw_degrees = *degrees;
  } else if (setting.name == "calibration.min_head_travel_degrees") {
    config.calibration_min_head_travel_degrees = *degrees;
  } else if (setting.name == "calibration.min_axis_degrees") {
    config.calibration_min_axis_degrees = *degrees;
  } else if (setting.name == "calibration.min_spread_degrees") {
    config.calibration_min_spread_degrees = *degrees;
  } else {
    config.looking_away_other_screen_margin_degrees = *degrees;
  }
  return std::nullopt;
}

// Sets eye_signs.elevated_blinks_per_minute (#145) from `value`, or
// returns why it cannot.
std::optional<std::string> SetEyeSignsElevatedBlinksPerMinute(
    std::string_view value, Config& config) {
  const std::optional<float> rate = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!rate || std::isnan(*rate) || *rate < 0 ||
      *rate > kMostEyeSignsBlinksPerMinute) {
    return "expected a number from 0 to 120";
  }
  config.eye_signs_elevated_blinks_per_minute = *rate;
  return std::nullopt;
}

// Sets eye_signs.long_blink_ms, eye_signs.widened_eyes_hold_ms or
// eye_signs.min_window_ms (#145) from `value`, or returns why it cannot.
// `setting.name` is one of those three, as `Set` has already checked.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<std::string> SetEyeSignsMs(const Setting& setting,
                                         Config& config) {
  const std::optional<int> milliseconds = Number<int>(setting.value);
  if (!milliseconds || *milliseconds < 0 || *milliseconds > kMostEyeSignsMs) {
    return "expected a whole number from 0 to 10000";
  }
  if (setting.name == "eye_signs.long_blink_ms") {
    config.eye_signs_long_blink_ms = *milliseconds;
  } else if (setting.name == "eye_signs.widened_eyes_hold_ms") {
    config.eye_signs_widened_eyes_hold_ms = *milliseconds;
  } else {
    config.eye_signs_min_window_ms = *milliseconds;
  }
  return std::nullopt;
}

// Sets eye_signs.widened_eyes_threshold (#145) from `value`, or returns
// why it cannot.
std::optional<std::string> SetEyeSignsWidenedEyesThreshold(
    std::string_view value, Config& config) {
  const std::optional<float> threshold = Number<float>(value);
  // isnan too: from_chars reads "nan", and NaN passes the comparisons.
  if (!threshold || std::isnan(*threshold) || *threshold < 0 ||
      *threshold > 1) {
    return "expected a number from 0 to 1";
  }
  config.eye_signs_widened_eyes_threshold = *threshold;
  return std::nullopt;
}

// Sets one of the three eye_signs.* settings (#145) from `setting`, or
// returns why it cannot. Kept apart from Set so that function's own
// cognitive complexity stays under the project's limit.
std::optional<std::string> SetEyeSigns(const Setting& setting, Config& config) {
  const std::string_view name = setting.name;
  if (name == "eye_signs.elevated_blinks_per_minute") {
    return SetEyeSignsElevatedBlinksPerMinute(setting.value, config);
  }
  if (name == "eye_signs.long_blink_ms" ||
      name == "eye_signs.widened_eyes_hold_ms" ||
      name == "eye_signs.min_window_ms") {
    return SetEyeSignsMs(setting, config);
  }
  if (name == "eye_signs.widened_eyes_threshold") {
    return SetEyeSignsWidenedEyesThreshold(setting.value, config);
  }
  return "unknown setting " + std::string(name);
}

// Sets the setting in `config`, or returns why it cannot.
std::optional<std::string> Set(const Setting& setting, Config& config) {
  const std::string_view name = setting.name;
  const std::string_view value = setting.value;
  if (name == "camera.frames_per_second") {
    return SetFramesPerSecond(value, config);
  }
  if (name == "camera.device_id") {
    return SetCameraDeviceId(value, config);
  }
  if (name == "head_pose.enabled" || name == "gestures.enabled" ||
      name == "expressions.enabled" || name == "looking_away.enabled" ||
      name == "screen_context.enabled" ||
      name == "screen_context.window_titles_enabled") {
    return SetSwitch(setting, config);
  }
  if (name == "gestures.confidence_threshold") {
    return SetGestureConfidenceThreshold(value, config);
  }
  if (name == "expressions.smallest_rise") {
    return SetExpressionsSmallestRise(value, config);
  }
  if (name == "expressions.spreads") {
    return SetExpressionsSpreads(value, config);
  }
  if (name == "expressions.hold_ms") {
    return SetExpressionsHoldMs(value, config);
  }
  if (name == "expressions.pose_degrees") {
    return SetExpressionsPoseDegrees(value, config);
  }
  if (name == "expressions.pose_range") {
    return SetExpressionsPoseRange(value, config);
  }
  if (name == "expressions.settle_ms") {
    return SetExpressionsSettleMs(value, config);
  }
  if (name == "expressions.eye_look_down_threshold") {
    return SetExpressionsEyeLookDownThreshold(value, config);
  }
  if (name == "expressions.eye_blink_threshold") {
    return SetExpressionsEyeBlinkThreshold(value, config);
  }
  if (name == "looking_away.degrees") {
    return SetLookingAwayDegrees(value, config);
  }
  if (name == "looking_away.hold_ms") {
    return SetLookingAwayHoldMs(value, config);
  }
  if (name == "looking_away.averted_ms") {
    return SetLookingAwayAvertedMs(value, config);
  }
  if (name == "screen_context.other_screen_degrees") {
    return SetScreenContextOtherScreenDegrees(value, config);
  }
  if (name == "eye_gaze.horizontal_gain" || name == "eye_gaze.vertical_gain") {
    return SetEyeGazeGain(setting, config);
  }
  if (name == "eye_gaze.blink_threshold") {
    return SetEyeGazeBlinkThreshold(value, config);
  }
  if (name == "eye_gaze.median_ms" || name == "eye_gaze.resting_ms" ||
      name == "eye_gaze.gap_ms") {
    return SetEyeGazeMs(setting, config);
  }
  if (name == "eye_gaze.head_only_yaw_degrees" ||
      name == "calibration.min_head_travel_degrees" ||
      name == "calibration.min_axis_degrees" ||
      name == "calibration.min_spread_degrees" ||
      name == "looking_away.other_screen_margin_degrees") {
    return SetDegreesSetting(setting, config);
  }
  if (name.starts_with("eye_signs.")) {
    return SetEyeSigns(setting, config);
  }
  return "unknown setting " + std::string(name);
}

}  // namespace

std::optional<Config> ParseConfig(std::string_view text, std::string& error) {
  Config config;
  std::set<std::string, std::less<>> seen;
  int number = 0;
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view()
                                         : text.substr(end + 1);
    ++number;
    line = Trim(line.substr(0, line.find('#')));
    if (line.empty()) {
      continue;
    }
    const std::size_t equals = line.find('=');
    std::optional<std::string> problem;
    if (equals == std::string_view::npos) {
      problem = "expected name = value";
    } else {
      const std::string_view name = Trim(line.substr(0, equals));
      if (!seen.emplace(name).second) {
        problem = "repeats " + std::string(name);
      } else {
        problem =
            Set({.name = name, .value = Trim(line.substr(equals + 1))}, config);
      }
    }
    if (problem) {
      error = "line " + std::to_string(number) + ": " + *problem;
      return std::nullopt;
    }
  }
  return config;
}

}  // namespace facepalm
