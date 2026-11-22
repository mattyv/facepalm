#ifndef FACEPALM_SRC_EYE_GAZE_H_
#define FACEPALM_SRC_EYE_GAZE_H_

#include <cstdint>
#include <deque>
#include <optional>
#include <span>

#include "src/face_model.h"
#include "src/head_pose.h"

namespace facepalm {

// The eye-in-head signal read from one frame's blendshapes (#137): raw, not
// yet relative to a resting level. horizontal follows head yaw's own sign
// (positive is a turn of the eyes to the subject's left); vertical follows
// head pitch's own sign (positive is looking down). blink is whether this
// frame reads as a blink (src/eyes_probe.h's own eyeBlink indices), for the
// caller to skip rather than fold into either signal.
struct EyeSignal {
  double horizontal = 0;
  double vertical = 0;
  bool blink = false;
};

// Reads `face`'s eye-look blendshapes into one EyeSignal (#137's design):
// horizontal is the mean of (eyeLookOutLeft - eyeLookInLeft) and
// (eyeLookInRight - eyeLookOutRight) -- turning the eyes to the subject's
// left raises the left eye's "out" score and the right eye's "in" score,
// following src/eyes_probe.h's own blendshape indices (11 through 18);
// vertical is the mean "down" score minus the mean "up" score. blink is
// set when the mean of eyeBlinkLeft (9) and eyeBlinkRight (10) passes
// `blink_threshold` (#137: "skip blinks (eyeBlink > ~0.5)").
EyeSignal EyeSignalFrom(const Face& face, double blink_threshold);

// How many degrees of gaze one unit of eye signal is worth, per axis:
// Config's default, or a person's own calibrated value (#137's
// fixate-and-turn gains step, src/calibration.h's CalibrationProfile).
struct EyeGains {
  double horizontal = 0;
  double vertical = 0;
};

// Composes a coarse gaze angle from head pose and eye-in-head signal
// (#137): gaze = head + gain * (eye - eye_resting), per axis, relative to
// the eyes' own resting level -- the same relative-to-resting approach
// ExpressionTracker (src/expressions.h) takes for its blendshapes. Beyond
// `head_only_yaw_degrees` of head yaw the eyes stop contributing (a glance
// that far is read as a head-only turn, #137's own design), and with no
// eye signal yet (EyeGazeTracker still learning the resting level, or no
// face at all), gaze is head pose alone either way. roll is always the
// head's own: no eye signal reads it.
HeadRotation ComposeGaze(const HeadRotation& head,
                         const std::optional<EyeSignal>& eye,
                         const std::optional<EyeSignal>& eye_resting,
                         const EyeGains& gains, double head_only_yaw_degrees);

// Smooths eye-in-head signal and learns its resting level, for
// ScreenContextReader (#137), the way ExpressionTracker (src/expressions.h)
// does for blendshapes: a blink (EyeSignalFrom's own blink_threshold) is
// skipped outright; kept samples are folded into a median over the last
// `median_ms` to damp frame noise, which doubles as a dwell on whatever
// class a caller reads off the result, since that cannot flip until most
// of the window agrees; and the first `resting_ms` of samples (and again
// after a gap of `gap_ms` with no sample) learn the resting level instead
// of reporting one.
class EyeGazeTracker {
 public:
  struct Settings {
    double blink_threshold;   // EyeSignalFrom's own.
    std::int64_t median_ms;   // The median filter's window.
    std::int64_t resting_ms;  // How long a resting level takes to learn.
    std::int64_t gap_ms;      // How long a gap before learning afresh.
  };

  explicit EyeGazeTracker(Settings settings);

  // Takes the next frame's face and timestamp (must increase from call to
  // call while `face` keeps having a value); nullopt resets it, as a
  // missing face does for ScreenContextReader's own pose (#137's blink
  // skip and the resting gate below are this class's own; a missing face
  // is simply nothing to read).
  void Update(const std::optional<Face>& face, std::int64_t timestamp_ms);

  // The median-filtered signal, once past the learning window; nullopt
  // while still learning, on a gap with no sample yet since, or with
  // every recent sample a blink.
  [[nodiscard]] std::optional<EyeSignal> Signal() const;
  // The resting level Update has learned; nullopt until `resting_ms` of
  // samples have been seen.
  [[nodiscard]] std::optional<EyeSignal> Resting() const;

 private:
  // Folds `signal` into the resting mean, or starts a fresh one after a
  // gap (#137, as ExpressionTracker's own StartOver does for the resting
  // face).
  void LearnResting(const EyeSignal& signal, std::int64_t timestamp_ms);

  Settings settings_;
  // Samples kept for the median filter, each not a blink, within
  // median_ms of the latest; oldest first.
  std::deque<std::pair<std::int64_t, EyeSignal>> recent_;
  std::optional<EyeSignal> resting_;
  int resting_samples_ = 0;
  std::int64_t resting_started_ms_ = 0;
  std::int64_t last_ms_ = 0;
  bool have_last_ = false;
};

// One sample #137's gains step collects: the step's own fixate-and-turn
// head pose and eye signal at one instant, the way CalibrationSession
// (src/calibration.h) feeds its other steps' Update, but kept as a series
// rather than averaged into one.
struct GainsSample {
  HeadRotation head;  // Only yaw_degrees and pitch_degrees are read.
  EyeSignal eye;       // Only horizontal and vertical are read.
};

// Gains #137's fixate-and-turn step estimates, per axis; either may be
// nullopt (that axis barely moved, so its own slope cannot be measured --
// the caller keeps its default gain for it) even when the step as a whole
// was not refused.
struct EstimatedGains {
  std::optional<double> horizontal;
  std::optional<double> vertical;
};

// The gains step's own estimate (#137: "fixate a dot, turn the head
// slowly; slope of head vs eye gives the gain"): per axis, the least-
// squares slope of head over eye (head = slope * eye + intercept, while
// the person's eyes compensate to keep looking at the dot as their head
// turns), negated for ComposeGaze's own sign (gaze = head + gain * eye
// must stay level as head varies, so gain = -slope). `blink` samples
// (EyeSignalFrom's own flag) are never passed in; CalibrationSession
// leaves them out of the buffer it builds.
//
// nullopt (the whole step refused, #137's "refuse too little head
// travel") when neither axis's own head range (max minus min, over
// `samples`) reaches `min_travel_degrees`. Otherwise, each axis whose own
// head range is at least `min_axis_degrees` gets its slope estimated (and
// nullopt instead when that axis's own eye variance is too small to
// divide by); an axis under `min_axis_degrees` of its own range is left
// nullopt too, rather than dividing by a range that barely moved.
//
// `min_travel_degrees` and `min_axis_degrees` are never swappable in
// practice: every call site passes both from CalibrationSettings' own
// named fields, never a bare literal in either position.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::optional<EstimatedGains> EstimateGains(std::span<const GainsSample> samples,
                                            double min_travel_degrees,
                                            double min_axis_degrees);
// NOLINTEND(bugprone-easily-swappable-parameters)

}  // namespace facepalm

#endif  // FACEPALM_SRC_EYE_GAZE_H_
