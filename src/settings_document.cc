#include "src/settings_document.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "src/config.h"

namespace facepalm {
namespace {
constexpr int kMaximumFramesPerSecond = 60;
// ParseConfig has already established a nonempty setting name.
std::string_view Trim(std::string_view text) {
  constexpr std::string_view kSpace = " \t\r";
  const auto first = text.find_first_not_of(kSpace);
  return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}
}  // namespace

// The single pass preserves source lines and their comments.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::optional<std::string> UpdateSettingsDocument(std::string_view original,
                                                  const MenuSettings& settings,
                                                  std::string& error) {
  if (original.find('\0') != std::string_view::npos) {
    error = "The settings file contains a NUL character.";
    return std::nullopt;
  }
  if (!ParseConfig(original, error)) {
    return std::nullopt;
  }
  if (settings.frames_per_second < 1 ||
      settings.frames_per_second > kMaximumFramesPerSecond) {
    error = "Frames per second must be between 1 and 60.";
    return std::nullopt;
  }
  constexpr std::array<std::string_view, 17> kNames = {
      "camera.frames_per_second",
      "camera.device_id",
      "gestures.enabled",
      "expressions.enabled",
      "gestures.confidence_threshold",
      "expressions.smallest_rise",
      "expressions.spreads",
      "expressions.hold_ms",
      "expressions.pose_degrees",
      "expressions.pose_range",
      "expressions.settle_ms",
      "looking_away.enabled",
      "looking_away.degrees",
      "looking_away.hold_ms",
      "screen_context.enabled",
      "screen_context.window_titles_enabled",
      "screen_context.other_screen_degrees"};
  const std::array<std::string, kNames.size()> values = {
      std::to_string(settings.frames_per_second),
      settings.camera_device_id,
      settings.gestures_enabled ? "true" : "false",
      settings.expressions_enabled ? "true" : "false",
      settings.gesture_confidence,
      settings.expression_rise,
      settings.expression_spreads,
      settings.expression_hold_ms,
      settings.expression_pose_degrees,
      settings.expression_pose_range,
      settings.expression_settle_ms,
      settings.looking_away_enabled ? "true" : "false",
      settings.looking_away_degrees,
      settings.looking_away_hold_ms,
      settings.screen_context_enabled ? "true" : "false",
      settings.screen_context_window_titles_enabled ? "true" : "false",
      settings.screen_context_other_screen_degrees};
  for (const auto& value : values) {
    if (value.find_first_of("\r\n#\0", 0, 4) != std::string::npos) {
      error = "A setting contains an invalid character.";
      return std::nullopt;
    }
  }
  std::array<bool, kNames.size()> found{};
  std::string result;
  while (!original.empty()) {
    const auto end = original.find('\n');
    const auto line = original.substr(0, end);
    original = end == std::string_view::npos ? std::string_view{}
                                             : original.substr(end + 1);
    const auto comment = line.find('#');
    const auto code = line.substr(0, comment);
    const auto equals = code.find('=');
    bool replaced = false;
    if (equals != std::string_view::npos) {
      const auto name = Trim(code.substr(0, equals));
      for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (name == kNames[i]) {
          result.append(line.substr(0, equals + 1));
          result.append(" ").append(values[i]);
          if (comment != std::string_view::npos) {
            result.append(" ").append(line.substr(comment));
          }
          found[i] = true;
          replaced = true;
          break;
        }
      }
    }
    if (!replaced) {
      result.append(line);
    }
    result.push_back('\n');
  }
  for (std::size_t i = 0; i < kNames.size(); ++i) {
    if (!found[i]) {
      result.append(kNames[i]).append(" = ").append(values[i]).append("\n");
    }
  }
  if (!ParseConfig(result, error)) {
    return std::nullopt;
  }
  return result;
}
}  // namespace facepalm
