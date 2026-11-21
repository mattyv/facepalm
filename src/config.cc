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
constexpr float kMostScreenContextOtherScreenDegrees = 180;

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

// Sets the setting in `config`, or returns why it cannot.
std::optional<std::string> Set(const Setting& setting, Config& config) {
  const std::string_view name = setting.name;
  const std::string_view value = setting.value;
  if (name == "camera.frames_per_second") {
    return SetFramesPerSecond(value, config);
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
  if (name == "looking_away.degrees") {
    return SetLookingAwayDegrees(value, config);
  }
  if (name == "looking_away.hold_ms") {
    return SetLookingAwayHoldMs(value, config);
  }
  if (name == "screen_context.other_screen_degrees") {
    return SetScreenContextOtherScreenDegrees(value, config);
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
