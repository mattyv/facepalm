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
  static constexpr float kDefaultMotionThreshold = 0.5F;   // Nodding Pigeon's.
  static constexpr float kDefaultGestureThreshold = 0.9F;  // Nodding Pigeon's.

  int frames_per_second = kDefaultFramesPerSecond;
  bool head_pose_enabled = true;
  bool gestures_enabled = true;
  float motion_threshold = kDefaultMotionThreshold;
  float gesture_threshold = kDefaultGestureThreshold;
};

// Reads a config file's text: one `name = value` a line, blank lines, and
// `#` comments. Names are camera.frames_per_second (1 to 60),
// head_pose.enabled and gestures.enabled (true or false), and
// gestures.motion_threshold and gestures.gesture_threshold (0 to 1).
// Returns the config, or nothing with `error` naming the line, for an
// unknown name, a bad value, a repeated name or a line with no `=`.
std::optional<Config> ParseConfig(std::string_view text, std::string& error);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CONFIG_H_
