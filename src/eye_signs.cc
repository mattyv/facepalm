#include "src/eye_signs.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "src/eye_gaze.h"
#include "src/face_model.h"

namespace facepalm {
namespace {

// src/eyes_probe.h's own blendshape indices for eyeWideLeft and
// eyeWideRight.
constexpr int kEyeWideLeft = 21;
constexpr int kEyeWideRight = 22;
// src/eyes_probe.h's own blendshape indices for eyeLookInLeft/Right and
// eyeLookOutLeft/Right (#145, #165): EyeSignSample::side_look's own
// inputs.
constexpr int kEyeLookInLeft = 13;
constexpr int kEyeLookInRight = 14;
constexpr int kEyeLookOutLeft = 15;
constexpr int kEyeLookOutRight = 16;

constexpr double kMillisecondsPerMinute = 60000.0;

// The longest run of consecutive `samples` each matching `is_active`, as
// the span from its first to its last sample's own timestamp. Zero with
// none. Shared by BlinkRate/LongBlink's own blink runs and WidenedEyes'
// own widened runs below, so neither reimplements the same run-finding
// loop its own way.
template <typename Predicate>
std::int64_t LongestRunMs(std::span<const EyeSignSample> samples,
                          Predicate is_active) {
  std::int64_t longest = 0;
  std::optional<std::int64_t> run_start;
  for (const EyeSignSample& sample : samples) {
    if (is_active(sample)) {
      if (!run_start) {
        run_start = sample.timestamp_ms;
      }
      longest = std::max(longest, sample.timestamp_ms - *run_start);
    } else {
      run_start.reset();
    }
  }
  return longest;
}

// How many separate runs of `blink` samples `samples` holds.
int BlinkRuns(std::span<const EyeSignSample> samples) {
  int runs = 0;
  bool in_run = false;
  for (const EyeSignSample& sample : samples) {
    if (sample.blink && !in_run) {
      ++runs;
    }
    in_run = sample.blink;
  }
  return runs;
}

}  // namespace

std::string_view Name(BlinkRate rate) {
  constexpr std::array<std::string_view, 3> kNames = {"normal", "elevated",
                                                      "unknown"};
  return kNames.at(static_cast<std::size_t>(rate));
}

std::optional<BlinkRate> BlinkRateNamed(std::string_view name) {
  constexpr std::array<std::string_view, 3> kNames = {"normal", "elevated",
                                                      "unknown"};
  const auto* found = std::ranges::find(kNames, name);
  if (found == kNames.end()) {
    return std::nullopt;
  }
  return static_cast<BlinkRate>(found - kNames.begin());
}

std::string_view Name(LongBlink blink) {
  constexpr std::array<std::string_view, 3> kNames = {"no", "yes", "unknown"};
  return kNames.at(static_cast<std::size_t>(blink));
}

std::optional<LongBlink> LongBlinkNamed(std::string_view name) {
  constexpr std::array<std::string_view, 3> kNames = {"no", "yes", "unknown"};
  const auto* found = std::ranges::find(kNames, name);
  if (found == kNames.end()) {
    return std::nullopt;
  }
  return static_cast<LongBlink>(found - kNames.begin());
}

std::string_view Name(WidenedEyes widened) {
  constexpr std::array<std::string_view, 3> kNames = {"no", "yes", "unknown"};
  return kNames.at(static_cast<std::size_t>(widened));
}

std::optional<WidenedEyes> WidenedEyesNamed(std::string_view name) {
  constexpr std::array<std::string_view, 3> kNames = {"no", "yes", "unknown"};
  const auto* found = std::ranges::find(kNames, name);
  if (found == kNames.end()) {
    return std::nullopt;
  }
  return static_cast<WidenedEyes>(found - kNames.begin());
}

EyeSignSample SampleFrom(const Face& face, double blink_threshold,
                         std::int64_t timestamp_ms) {
  const double look_in =
      (static_cast<double>(face.blendshapes[kEyeLookInLeft]) +
       static_cast<double>(face.blendshapes[kEyeLookInRight])) /
      2;
  const double look_out =
      (static_cast<double>(face.blendshapes[kEyeLookOutLeft]) +
       static_cast<double>(face.blendshapes[kEyeLookOutRight])) /
      2;
  return {.timestamp_ms = timestamp_ms,
          .blink = EyeSignalFrom(face, blink_threshold).blink,
          .eye_wide = (static_cast<double>(face.blendshapes[kEyeWideLeft]) +
                       static_cast<double>(face.blendshapes[kEyeWideRight])) /
                      2,
          .side_look = std::max(look_in, look_out)};
}

EyeSigns EyeSignsOf(std::span<const EyeSignSample> samples,
                    const EyeSignSettings& settings) {
  if (samples.empty()) {
    return {};
  }
  const std::int64_t window_ms =
      samples.back().timestamp_ms - samples.front().timestamp_ms;
  const int blinks = BlinkRuns(samples);
  const std::int64_t longest_blink_ms = LongestRunMs(
      samples, [](const EyeSignSample& sample) { return sample.blink; });
  const std::int64_t widened_ms =
      LongestRunMs(samples, [&settings](const EyeSignSample& sample) {
        return sample.eye_wide >= settings.widened_eyes_threshold &&
               sample.side_look < settings.widened_eyes_max_side_look;
      });
  EyeSigns signs;
  // #171: window_ms > 0 guards the divide just below, whatever
  // settings.min_window_ms itself allows (src/config.cc's own Set
  // requires at least 1, but EyeSignSettings is a plain struct a test can
  // still build with 0) -- a single sample, or several sharing one
  // timestamp, is never enough to judge a rate from either way.
  if (window_ms > 0 && window_ms >= settings.min_window_ms) {
    const double per_minute = static_cast<double>(blinks) *
                              kMillisecondsPerMinute /
                              static_cast<double>(window_ms);
    signs.blink_rate = per_minute >= settings.elevated_blinks_per_minute
                           ? BlinkRate::kElevated
                           : BlinkRate::kNormal;
  }
  signs.long_blink = longest_blink_ms >= settings.long_blink_ms
                         ? LongBlink::kYes
                         : LongBlink::kNo;
  signs.widened_eyes = widened_ms >= settings.widened_eyes_hold_ms
                           ? WidenedEyes::kYes
                           : WidenedEyes::kNo;
  return signs;
}

}  // namespace facepalm
