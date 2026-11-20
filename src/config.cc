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

// Sets the setting in `config`, or returns why it cannot.
std::optional<std::string> Set(const Setting& setting, Config& config) {
  const std::string_view name = setting.name;
  const std::string_view value = setting.value;
  if (name == "camera.frames_per_second") {
    const std::optional<int> rate = Number<int>(value);
    if (!rate || *rate < 1 || *rate > kMostFramesPerSecond) {
      return "expected a whole number from 1 to 60";
    }
    config.frames_per_second = *rate;
    return std::nullopt;
  }
  if (name == "head_pose.enabled" || name == "gestures.enabled") {
    const std::optional<bool> enabled = Boolean(value);
    if (!enabled) {
      return "expected true or false";
    }
    (name == "head_pose.enabled" ? config.head_pose_enabled
                                 : config.gestures_enabled) = *enabled;
    return std::nullopt;
  }
  if (name == "gestures.confidence_threshold") {
    const std::optional<float> threshold = Number<float>(value);
    // isnan too: from_chars reads "nan", and NaN passes the comparisons.
    if (!threshold || std::isnan(*threshold) || *threshold < 0 ||
        *threshold > 1) {
      return "expected a number from 0 to 1";
    }
    config.gesture_confidence_threshold = *threshold;
    return std::nullopt;
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
