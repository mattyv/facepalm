#ifndef FACEPALM_SRC_EYE_SIGNS_H_
#define FACEPALM_SRC_EYE_SIGNS_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "src/face_model.h"

namespace facepalm {

// How often blinking happened over a beat's own window (#145): coarse,
// like src/screen_context.h's Gaze, and read the same stateless way, over
// samples a caller collects elsewhere. kUnknown is too short a window to
// judge a rate from (EyeSignSettings::min_window_ms), never "no face":
// src/screen_context_reporter.h's own EyeSignsBetween (#165) decides
// that itself, by never buffering a sample for a frame with none, the
// way src/screen_context.h's GazeWindow does for gaze.
enum class BlinkRate : std::uint8_t { kNormal, kElevated, kUnknown };

// normal, elevated or unknown.
std::string_view Name(BlinkRate rate);
// The rate named `name`, or nullopt.
std::optional<BlinkRate> BlinkRateNamed(std::string_view name);

// Whether a blink in the window was held past EyeSignSettings::long_blink_ms
// (#145): measurably different from an ordinary blink, which #139's own
// fixtures hold under a quarter of a second at the longest. kUnknown with
// no samples at all.
enum class LongBlink : std::uint8_t { kNo, kYes, kUnknown };

// no, yes or unknown.
std::string_view Name(LongBlink blink);
// The value named `name`, or nullopt.
std::optional<LongBlink> LongBlinkNamed(std::string_view name);

// Whether the eyes read as widened, held past
// EyeSignSettings::widened_eyes_hold_ms (#145). #139's nine fixtures carry
// no cue that deliberately widens the eyes (every session's own eyeWide
// stays under 0.051 at its peak, test/fixtures/eyes/README.md's own
// recordings), so EyeSignSettings::widened_eyes_threshold is reasoned from
// that ceiling, not measured against a true positive (this file's own
// header comment says so again where the constant is defined); a
// dedicated fixture would let it be tightened later. kUnknown with no
// samples at all.
enum class WidenedEyes : std::uint8_t { kNo, kYes, kUnknown };

// no, yes or unknown.
std::string_view Name(WidenedEyes widened);
// The value named `name`, or nullopt.
std::optional<WidenedEyes> WidenedEyesNamed(std::string_view name);

// One frame's own blink flag and eye-widening score, the inputs EyeSignsOf
// below reads a window of (#145): pure computation, so a caller --
// src/screen_context_reporter.h's own live buffer (#165), or a fixture
// replay -- collects these however it samples the camera, the way
// src/screen_context.h's CoarseGazeFrom takes one frame's own
// HeadRotation rather than sampling it itself.
struct EyeSignSample {
  std::int64_t timestamp_ms = 0;
  // src/eye_gaze.h's own EyeSignalFrom, read at EyeSignSettings'
  // blink_threshold: true when this frame reads as a blink.
  bool blink = false;
  // The mean of eyeWideLeft and eyeWideRight (src/eyes_probe.h's own
  // blendshape indices 21 and 22), 0 to 1.
  double eye_wide = 0;
};

// Builds one EyeSignSample from `face` at `timestamp_ms`, reading `face`
// the way src/eyes_gaze_replay.cc's own FaceFromEyes-built Face already
// is: eyeWideLeft and eyeWideRight at indices 21 and 22
// (src/eyes_probe.h), and EyeSignalFrom's own blink flag at
// `blink_threshold` (src/eye_gaze.h, the same threshold
// EyeGazeTracker::Settings takes). Shared by
// src/screen_context_reader.h's own live OnFace (#165) and this file's
// own fixture-replay tests, so neither reads the blendshapes a second,
// possibly different, way.
EyeSignSample SampleFrom(const Face& face, double blink_threshold,
                         std::int64_t timestamp_ms);

// EyeSignsOf's own thresholds (#145); Config's defaults (src/config.h,
// kDefaultEyeSignsElevatedBlinksPerMinute and the rest) carry the
// measurement each is reasoned from, the way src/screen_context.h's own
// GazeSettings leaves its defaults to Config too.
struct EyeSignSettings {
  double elevated_blinks_per_minute;
  std::int64_t long_blink_ms;
  double widened_eyes_threshold;
  std::int64_t widened_eyes_hold_ms;
  // BlinkRate reads kUnknown under this, the same "not enough to judge
  // yet" guard src/eye_gaze.h's own resting_ms takes, rather than let a
  // short window's own single blink read as an implausibly high rate.
  std::int64_t min_window_ms;
};

// The three coarse signs a beat's own window of eye samples gives (#145):
// never Rule's own input (specs/FeelingBeat.tla's Interpret stays
// untouched), a feature src/tuning.h's personal read alone may weigh,
// the same way src/screen_context.h's Gaze already is (#143).
struct EyeSigns {
  BlinkRate blink_rate = BlinkRate::kUnknown;
  LongBlink long_blink = LongBlink::kUnknown;
  WidenedEyes widened_eyes = WidenedEyes::kUnknown;
};

// Reads `samples`, oldest first, into one EyeSigns (#145): BlinkRate from
// how many separate runs of `blink` samples the window holds, as a rate
// per minute over the span from its first sample to its last, unknown
// under `settings.min_window_ms`; LongBlink from the longest such run's
// own span, at or past `settings.long_blink_ms`; WidenedEyes from the
// longest run of samples at or past `settings.widened_eyes_threshold`'s
// own `eye_wide`, at or past `settings.widened_eyes_hold_ms`. Every sign
// is kUnknown with no samples at all. `samples` need not come from one
// steady frame rate: each run's own span is read from its first and last
// sample's own timestamps, not counted in frames.
EyeSigns EyeSignsOf(std::span<const EyeSignSample> samples,
                    const EyeSignSettings& settings);

}  // namespace facepalm

#endif  // FACEPALM_SRC_EYE_SIGNS_H_
