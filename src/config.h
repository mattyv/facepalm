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
  // How the expression tracker gates out head movement (#46), tuned on the
  // same recording: still heads stayed under 6.5 degrees of deviation and
  // 2.0 degrees of range, while nods and shakes ran from 10 to 27 degrees.
  static constexpr float kDefaultExpressionsPoseDegrees = 10.0F;
  static constexpr float kDefaultExpressionsPoseRange = 3.0F;
  static constexpr int kDefaultExpressionsSettleMs = 600;
  // eyes_narrowed's own gate (#140): the eyeLookDown and eyeBlink scores,
  // either eye, above which a glance down or a blink holds the squint
  // back, so it is not read from a glance at the keyboard. Provisional:
  // reasoned from MediaPipe's 0-to-1 scale, not yet tuned on a recording,
  // pending #139's fixtures (test/fixtures/eyes/*.probe) or, lacking
  // those, synthetic data replayed through src/eyes_replay.h (#147).
  static constexpr float kDefaultExpressionsEyeLookDownThreshold = 0.3F;
  static constexpr float kDefaultExpressionsEyeBlinkThreshold = 0.5F;
  // LookAwayDetector's defaults (#35), tuned on one person's recordings:
  // their turns and looks down held 20 to 45 degrees from the resting pose
  // for seconds, while their nods stayed within about 12 degrees of it,
  // each swing lasting about half a second (#34).
  static constexpr float kDefaultLookingAwayDegrees = 15.0F;
  static constexpr int kDefaultLookingAwayHoldMs = 1000;
  // ScreenContextReporter's default (#61 item 11): half of
  // looking_away.degrees, the same halving LookAwayDetector's own
  // hysteresis uses for a middle ground.
  static constexpr float kDefaultScreenContextOtherScreenDegrees = 7.5F;

  int frames_per_second = kDefaultFramesPerSecond;
  // The chosen camera's stable ID (CameraDevice::id), or empty for the
  // default: the built-in camera, never a Continuity Camera or other
  // external device chosen silently (item 10 of #61, #13).
  std::string camera_device_id;
  bool head_pose_enabled = true;
  bool gestures_enabled = true;
  float gesture_confidence_threshold = kDefaultGestureConfidenceThreshold;
  bool expressions_enabled = true;
  float expressions_smallest_rise = kDefaultExpressionsSmallestRise;
  float expressions_spreads = kDefaultExpressionsSpreads;
  int expressions_hold_ms = kDefaultExpressionsHoldMs;
  float expressions_pose_degrees = kDefaultExpressionsPoseDegrees;
  float expressions_pose_range = kDefaultExpressionsPoseRange;
  int expressions_settle_ms = kDefaultExpressionsSettleMs;
  float expressions_eye_look_down_threshold =
      kDefaultExpressionsEyeLookDownThreshold;
  float expressions_eye_blink_threshold = kDefaultExpressionsEyeBlinkThreshold;
  bool looking_away_enabled = true;
  float looking_away_degrees = kDefaultLookingAwayDegrees;
  int looking_away_hold_ms = kDefaultLookingAwayHoldMs;
  // Active app, mouse position and coarse gaze (#61 item 11). Window
  // titles need the Screen Recording permission on macOS and can hold
  // private text, so they default off.
  bool screen_context_enabled = true;
  bool screen_context_window_titles_enabled = false;
  float screen_context_other_screen_degrees =
      kDefaultScreenContextOtherScreenDegrees;
};

// Reads a config file's text: one `name = value` a line, blank lines, and
// `#` comments. Names are camera.frames_per_second (1 to 60),
// camera.device_id (any text, the chosen camera's stable ID from the
// `cameras` protocol line, or blank for the default), head_pose.enabled,
// gestures.enabled, expressions.enabled and looking_away.enabled (true or
// false), gestures.confidence_threshold (0 to 1), expressions.smallest_rise
// (0 to 1), expressions.spreads (0 to 100), expressions.hold_ms (a whole
// number from 0 to 5000), expressions.pose_degrees (0 to 180),
// expressions.pose_range (0 to 180), expressions.settle_ms (a whole number
// from 0 to 5000), expressions.eye_look_down_threshold and
// expressions.eye_blink_threshold (0 to 1), looking_away.degrees (0 to
// 180), looking_away.hold_ms (a whole number from 0 to 10000),
// screen_context.enabled, screen_context.window_titles_enabled (true or
// false), and screen_context.other_screen_degrees (0 to 180). Returns the
// config, or nothing with `error` naming the line, for an unknown name, a
// bad value, a repeated name or a line with no `=`.
std::optional<Config> ParseConfig(std::string_view text, std::string& error);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CONFIG_H_
