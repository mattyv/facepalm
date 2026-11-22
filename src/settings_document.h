#ifndef FACEPALM_SRC_SETTINGS_DOCUMENT_H_
#define FACEPALM_SRC_SETTINGS_DOCUMENT_H_

#include <optional>
#include <string>
#include <string_view>

#include "src/config.h"

namespace facepalm {
struct MenuSettings {
  int frames_per_second = Config::kDefaultFramesPerSecond;
  // The chosen camera's stable ID (item 10 of #61, #13), from the
  // `cameras` protocol line; blank for the default, the built-in camera.
  std::string camera_device_id;
  bool gestures_enabled = true;
  bool expressions_enabled = true;
  std::string gesture_confidence = "0.5";
  std::string expression_rise = "0.06";
  std::string expression_spreads = "3";
  std::string expression_hold_ms = "200";
  std::string expression_pose_degrees = "10";
  std::string expression_pose_range = "3";
  std::string expression_settle_ms = "600";
  // eyes_narrowed's own squint-guard (#140, #150: the Settings windows did
  // not show these either).
  std::string expression_eye_look_down_threshold = "0.2";
  std::string expression_eye_blink_threshold = "0.5";
  // Looking away (#35, #79): the Settings windows did not show these.
  bool looking_away_enabled = true;
  std::string looking_away_degrees = "15";
  std::string looking_away_hold_ms = "1000";
  // Screen context (#61 item 11, #79): window titles default off, since
  // they can hold private text and need a permission on macOS.
  bool screen_context_enabled = true;
  bool screen_context_window_titles_enabled = false;
  std::string screen_context_other_screen_degrees = "7.5";
};

// Validates the original, changes only the listed settings, and preserves
// every other line and any inline comment. Returns the error without editing
// anything when the original or requested settings are invalid.
std::optional<std::string> UpdateSettingsDocument(std::string_view original,
                                                  const MenuSettings& settings,
                                                  std::string& error);
}  // namespace facepalm
#endif  // FACEPALM_SRC_SETTINGS_DOCUMENT_H_
