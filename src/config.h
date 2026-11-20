#ifndef FACEPALM_SRC_CONFIG_H_
#define FACEPALM_SRC_CONFIG_H_

#include <optional>
#include <string>
#include <string_view>

namespace facepalm {

// facepalm's settings, read from ~/.config/facepalm/facepalm.conf (#34).
// Each has a default, so the file holds only what the user changes.
struct Config {
  static constexpr int kDefaultFramesPerSecond = 15;
  // The head-gesture model's own softmax has no notion of "sure"; this
  // holds back a call under half of a 6-way vote (#34).
  static constexpr float kDefaultGestureConfidenceThreshold = 0.5F;
  // ExpressionTracker's own defaults (#46), tuned on one person's 80-second
  // recording; the frown (brows_down) was the weakest of the five.
  static constexpr float kDefaultExpressionsSmallestRise = 0.06F;
  static constexpr float kDefaultExpressionsSpreads = 3.0F;
  static constexpr int kDefaultExpressionsHoldMs = 200;

  int frames_per_second = kDefaultFramesPerSecond;
  bool head_pose_enabled = true;
  bool gestures_enabled = true;
  float gesture_confidence_threshold = kDefaultGestureConfidenceThreshold;
  bool expressions_enabled = true;
  float expressions_smallest_rise = kDefaultExpressionsSmallestRise;
  float expressions_spreads = kDefaultExpressionsSpreads;
  int expressions_hold_ms = kDefaultExpressionsHoldMs;
};

// Reads a config file's text: one `name = value` a line, blank lines, and
// `#` comments. Names are camera.frames_per_second (1 to 60),
// head_pose.enabled, gestures.enabled and expressions.enabled (true or
// false), gestures.confidence_threshold (0 to 1), expressions.smallest_rise
// (0 to 1), expressions.spreads (0 to 100), and expressions.hold_ms (a
// whole number from 0 to 5000). Returns the config, or nothing with `error`
// naming the line, for an unknown name, a bad value, a repeated name or a
// line with no `=`.
std::optional<Config> ParseConfig(std::string_view text, std::string& error);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CONFIG_H_
