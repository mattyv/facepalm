#ifndef FACEPALM_SRC_SETTINGS_DOCUMENT_H_
#define FACEPALM_SRC_SETTINGS_DOCUMENT_H_

#include <optional>
#include <string>
#include <string_view>

#include "src/config.h"

namespace facepalm {
struct MenuSettings {
  int frames_per_second = Config::kDefaultFramesPerSecond;
  bool gestures_enabled = true;
  bool expressions_enabled = true;
  std::string gesture_confidence = "0.5";
  std::string expression_rise = "0.06";
  std::string expression_spreads = "3";
  std::string expression_hold_ms = "200";
  std::string expression_pose_degrees = "10";
  std::string expression_pose_range = "3";
  std::string expression_settle_ms = "600";
};

// Validates the original, changes only the listed settings, and preserves
// every other line and any inline comment. Returns the error without editing
// anything when the original or requested settings are invalid.
std::optional<std::string> UpdateSettingsDocument(std::string_view original,
                                                  const MenuSettings& settings,
                                                  std::string& error);
}  // namespace facepalm
#endif  // FACEPALM_SRC_SETTINGS_DOCUMENT_H_
