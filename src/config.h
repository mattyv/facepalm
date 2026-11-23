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
  // back, so it is not read from a glance at the keyboard. Tuned on
  // #139's fixtures (test/fixtures/eyes/*.probe, measured through
  // src/eyes_gaze_replay.h): was 0.3 for eyeLookDown, reasoned from
  // MediaPipe's 0-to-1 scale alone; glance_keyboard.probe's own deliberate
  // glances down read 0.20 to 0.24 on average (max 0.22 to 0.27), close
  // enough to that session's own "looking at the screen" baseline (0.23)
  // that 0.3 would have let a real glance through unguarded, so it comes
  // down to 0.2, which still clears the calmer baseline most of the other
  // eight fixtures hold (mostly under 0.19). eyeBlink's own 0.5 is
  // unchanged: blinks.probe and long_blinks.probe's actual blinks read
  // 0.45 to 0.74 at their peak, while every fixture's own calm baseline
  // stays under 0.31 (one transient blink inside thinking_aversion's
  // "look back" cue aside, which 0.5 correctly still catches).
  static constexpr float kDefaultExpressionsEyeLookDownThreshold = 0.2F;
  static constexpr float kDefaultExpressionsEyeBlinkThreshold = 0.5F;
  // LookAwayDetector's defaults (#35), tuned on one person's recordings:
  // their turns and looks down held 20 to 45 degrees from the resting pose
  // for seconds, while their nods stayed within about 12 degrees of it,
  // each swing lasting about half a second (#34).
  static constexpr float kDefaultLookingAwayDegrees = 15.0F;
  static constexpr int kDefaultLookingAwayHoldMs = 1000;
  // LookAwayDetector's other two settings (#141). kDefaultLookingAwayAvertedMs
  // was reasoned from the issue's own "back within about 5 to 8 seconds",
  // its midpoint rounded to a whole second; thinking_aversion.probe's own
  // two aversions (#139, measured through src/eyes_gaze_replay.h) each
  // last close to six seconds before returning, squarely inside that
  // midpoint, so a pause that long still reads as averted rather than
  // away, and the value is unchanged.
  // kDefaultLookingAwayOtherScreenMarginDegrees stays from
  // CalibrationSettings::kDefaultMinSpreadDegrees (#137's own spread check: a
  // margin smaller than the spread a corner must clear from its own screen's
  // centre would blur one screen's box into another's): #139's fixtures include
  // no calibrated screen to measure a box against, so this one is still
  // unproven on a recording.
  static constexpr int kDefaultLookingAwayAvertedMs = 6500;
  static constexpr float kDefaultLookingAwayOtherScreenMarginDegrees = 3.0F;
  // ScreenContextReporter's default (#61 item 11): half of
  // looking_away.degrees, the same halving LookAwayDetector's own
  // hysteresis uses for a middle ground.
  static constexpr float kDefaultScreenContextOtherScreenDegrees = 7.5F;
  // src/eye_gaze.h's defaults (#137), checked against #139's fixtures
  // (test/fixtures/eyes/*.probe, measured through src/eyes_gaze_replay.h):
  // horizontal_gain and vertical_gain were 30, given from memory, like
  // the eyes line's own field order (#139), not tuned on a recording.
  // head_turn_eyes_centered.probe's own two turns (eyes held on the
  // camera while the head turns to 29.6 and 32.8 degrees of yaw) need
  // ComposeGaze's eye term to cancel almost all of that head yaw back
  // out, which only a horizontal gain of about 43 to 48 does; 30 left a
  // third of the turn still showing in the composed gaze. Raised to 45.
  // vertical_gain has no matching head-pitch-with-eyes-centred recording
  // among #139's nine fixtures to measure the same way, so it keeps the
  // horizontal gain's own value rather than guess an unmeasured one.
  static constexpr float kDefaultEyeGazeHorizontalGain = 45.0F;
  static constexpr float kDefaultEyeGazeVerticalGain = 45.0F;
  // blink_threshold, median_ms, resting_ms, gap_ms and
  // head_only_yaw_degrees are unchanged: the same fixtures confirm them.
  // blink_threshold (0.5) separates blinks.probe's and long_blinks.probe's
  // own blinks (0.45 to 0.74 at their peak) from every fixture's calm
  // baseline (under 0.31), the same split expressions.eye_blink_threshold
  // reads above. median_ms (250 ms) already damps the frame noise a held
  // gaze shows (a standard deviation of roughly 0.01 to 0.09) without
  // blurring a held corner look, which lasts the cue's own six seconds.
  // resting_ms (3,000 ms) and gap_ms (2,000 ms) match ExpressionTracker's
  // own design for the same reason it does (below); every "look at the
  // camera" cue runs a full six seconds, well past 3,000 ms.
  // head_only_yaw_degrees (35) stays clear of the two turns above (29.6
  // and 32.8 degrees), where the eye correction above is still wanted.
  static constexpr float kDefaultEyeGazeBlinkThreshold = 0.5F;
  static constexpr int kDefaultEyeGazeMedianMs = 250;
  // Matches ExpressionTracker's own first-three-seconds and after-a-gap
  // design (#46), which this follows for the same reason: give the
  // resting level time to settle before anything is read relative to it.
  static constexpr int kDefaultEyeGazeRestingMs = 3000;
  static constexpr int kDefaultEyeGazeGapMs = 2000;
  static constexpr float kDefaultEyeGazeHeadOnlyYawDegrees = 35.0F;
  // CalibrationSettings's defaults (#137). Unchanged: #139's nine
  // fixtures do not include a calibration gains step or a calibrated
  // screen's corners, so there is nothing recorded to measure a travel,
  // an axis or a spread against. head_turn_eyes_centered.probe's own
  // yaw, end to end, covers more than 60 degrees, comfortably clearing
  // min_head_travel_degrees and min_axis_degrees either way, which is as
  // far as these fixtures can check them; a proper tuning needs its own
  // recording of the gains step and a calibrated screen (see this
  // issue's PR).
  static constexpr float kDefaultCalibrationMinHeadTravelDegrees = 10.0F;
  static constexpr float kDefaultCalibrationMinAxisDegrees = 4.0F;
  static constexpr float kDefaultCalibrationMinSpreadDegrees = 3.0F;
  // src/eye_signs.h's own EyeSignSettings defaults (#145): blink rate, a
  // long blink and widened eyes, coarse signs for the personal read alone
  // (src/tuning.h), never specs/FeelingBeat.tla's own Rule. Measured on
  // #139's fixtures (test/fixtures/eyes/*.probe) through
  // src/eyes_gaze_replay.h, which this issue extends to print one
  // `eye_sign <timestamp_ms> <blink> <eye_wide>` line per paired frame (0
  // or 1 for blink).
  //
  // kDefaultEyeSignsElevatedBlinksPerMinute (14): blinks.probe's own eight
  // deliberate blinks span its whole 25.257 s recording, a rate of about
  // 19 a minute; every other fixture's own natural blinking stays at or
  // under thinking_aversion.probe's four single-frame blinks over the
  // same span, about 9.5 a minute (the other seven fixtures show none at
  // all). 14 sits at that split's own midpoint, clear of both.
  //
  // kDefaultEyeSignsLongBlinkMs (500): blinks.probe's own blinks, the
  // longest of its eight, hold the blink score (eye_gaze.blink_threshold,
  // 0.5) for at most 200 ms; long_blinks.probe's own two deliberate long
  // blinks hold it for 4,264 ms and 5,264 ms. 500 ms clears the ordinary
  // blinks by 2.5x and stays 8.5x under the shortest long one.
  //
  // kDefaultEyeSignsWidenedEyesThreshold (0.025),
  // kDefaultEyeSignsWidenedEyesHoldMs (1000) and
  // kDefaultEyeSignsWidenedEyesMaxSideLook (0.2): measured against #165's
  // own tenth fixture, test/fixtures/eyes/widened_eyes.probe, which carries
  // a genuine "widen your eyes" cue (the original nine do not). eye_wide
  // (the mean of eyeWideLeft/Right) over that fixture's own four 6 s cue
  // windows: ahead (resting) mean 0.007, max 0.013; widen mean 0.015, max
  // 0.031; relax mean 0.007; widen2 mean 0.022, max 0.030 -- a weak signal
  // for this user, peaking at 0.031, but clearly above the ahead/relax
  // baseline. 0.025 sits between that baseline and both widen cues' own
  // means.
  //
  // eye_wide alone cannot tell this from a sideways glance, which lifts it
  // too: every other fixture's own eye_wide peaks higher still -- up to
  // 0.051 (reading_other_screen.probe) and 0.036 (gaze_other_screen.probe)
  // -- and holds there for long runs (91 and 99 frames at or past 0.02,
  // against widened_eyes.probe's own 39). What tells them apart is how far
  // the eyes are turned horizontally (EyeSignSample::side_look, the larger
  // of mean eyeLookIn and mean eyeLookOut): at eye_wide >= 0.02, it reads
  // 0.08 for widened_eyes.probe and 0.09 for thinking_aversion.probe,
  // against 0.35 to 0.36 for the two other-screen fixtures.
  // widened_eyes_max_side_look (0.2) sits well clear of both groups.
  //
  // widened_eyes_hold_ms (1000, up from the old reasoned 200) matches
  // widened_eyes.probe's own longest run at or past the threshold (39
  // frames, about 2.6 s at 15 fps), comfortably past 1000 ms while staying
  // clear of thinking_aversion.probe's own longest run at the same bar
  // (4 frames): a brief flicker, not a held widen.
  //
  // This sign stays weak for this user specifically (its own true positive
  // peaks at 0.031, a small lift over a 0.007 baseline); it only ever
  // feeds the personal read's own tuning (src/tuning.h), never
  // specs/FeelingBeat.tla's own Rule, so an occasionally silent sign costs
  // nothing but a feature that rarely fires.
  //
  // kDefaultEyeSignsMinWindowMs (3000): BlinkRate reads unknown under
  // this, the same "not enough to judge yet" guard eye_gaze.resting_ms
  // takes, rather than let a short window's own single blink read as an
  // implausibly high rate.
  static constexpr float kDefaultEyeSignsElevatedBlinksPerMinute = 14.0F;
  static constexpr int kDefaultEyeSignsLongBlinkMs = 500;
  static constexpr float kDefaultEyeSignsWidenedEyesThreshold = 0.025F;
  static constexpr int kDefaultEyeSignsWidenedEyesHoldMs = 1000;
  static constexpr float kDefaultEyeSignsWidenedEyesMaxSideLook = 0.2F;
  static constexpr int kDefaultEyeSignsMinWindowMs = 3000;

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
  int looking_away_averted_ms = kDefaultLookingAwayAvertedMs;
  float looking_away_other_screen_margin_degrees =
      kDefaultLookingAwayOtherScreenMarginDegrees;
  // Active app, mouse position and coarse gaze (#61 item 11). Window
  // titles need the Screen Recording permission on macOS and can hold
  // private text, so they default off.
  bool screen_context_enabled = true;
  bool screen_context_window_titles_enabled = false;
  float screen_context_other_screen_degrees =
      kDefaultScreenContextOtherScreenDegrees;
  // src/eye_gaze.h's own settings (#137): the eye-in-head signal composed
  // with head pose into coarse gaze, and the calibration session's gains
  // step and spread check.
  float eye_gaze_horizontal_gain = kDefaultEyeGazeHorizontalGain;
  float eye_gaze_vertical_gain = kDefaultEyeGazeVerticalGain;
  float eye_gaze_blink_threshold = kDefaultEyeGazeBlinkThreshold;
  int eye_gaze_median_ms = kDefaultEyeGazeMedianMs;
  int eye_gaze_resting_ms = kDefaultEyeGazeRestingMs;
  int eye_gaze_gap_ms = kDefaultEyeGazeGapMs;
  float eye_gaze_head_only_yaw_degrees = kDefaultEyeGazeHeadOnlyYawDegrees;
  float calibration_min_head_travel_degrees =
      kDefaultCalibrationMinHeadTravelDegrees;
  float calibration_min_axis_degrees = kDefaultCalibrationMinAxisDegrees;
  float calibration_min_spread_degrees = kDefaultCalibrationMinSpreadDegrees;
  // src/eye_signs.h's own EyeSignSettings (#145).
  float eye_signs_elevated_blinks_per_minute =
      kDefaultEyeSignsElevatedBlinksPerMinute;
  int eye_signs_long_blink_ms = kDefaultEyeSignsLongBlinkMs;
  float eye_signs_widened_eyes_threshold = kDefaultEyeSignsWidenedEyesThreshold;
  int eye_signs_widened_eyes_hold_ms = kDefaultEyeSignsWidenedEyesHoldMs;
  float eye_signs_widened_eyes_max_side_look =
      kDefaultEyeSignsWidenedEyesMaxSideLook;
  int eye_signs_min_window_ms = kDefaultEyeSignsMinWindowMs;
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
// looking_away.averted_ms (a whole number from 0 to 30000, #141) and
// looking_away.other_screen_margin_degrees (0 to 180, #141),
// screen_context.enabled, screen_context.window_titles_enabled (true or
// false), screen_context.other_screen_degrees (0 to 180),
// eye_gaze.horizontal_gain and eye_gaze.vertical_gain (0 to 200),
// eye_gaze.blink_threshold (0 to 1), eye_gaze.median_ms, eye_gaze.resting_ms
// and eye_gaze.gap_ms (a whole number from 0 to 10000),
// eye_gaze.head_only_yaw_degrees (0 to 180),
// calibration.min_head_travel_degrees, calibration.min_axis_degrees and
// calibration.min_spread_degrees (0 to 180), and
// eye_signs.elevated_blinks_per_minute (0 to 120),
// eye_signs.long_blink_ms and eye_signs.widened_eyes_hold_ms (a whole
// number from 0 to 10000, #145), eye_signs.min_window_ms (a whole number
// from 1 to 10000, #145, #171: 0 would divide EyeSignsOf's own blink rate
// by a zero-length window), and eye_signs.widened_eyes_threshold and
// eye_signs.widened_eyes_max_side_look (0 to 1, the latter #145/#165).
// Returns the config, or
// nothing with `error` naming the line, for an unknown name, a bad value, a
// repeated name or a line with no `=`.
std::optional<Config> ParseConfig(std::string_view text, std::string& error);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CONFIG_H_
