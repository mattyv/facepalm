#include "src/screen_context_reporter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>

#include "src/calibration.h"
#include "src/eye_gaze.h"
#include "src/head_pose.h"
#include "src/protocol.h"
#include "src/screen_context.h"
#include "src/screen_source.h"

namespace facepalm {
namespace {

// The nearest pixel: sub-pixel precision serves no reader of this field.
std::string PixelNumber(double value) {
  constexpr int kLongest = 32;
  std::array<char, kLongest> text{};
  std::snprintf(text.data(), text.size(), "%.0f", value);
  return text.data();
}

}  // namespace

// See the declaration (src/screen_context_reporter.h) for why
// head_only_yaw_degrees and max_window_ms are never swappable in
// practice.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
ScreenContextReporter::ScreenContextReporter(
    ScreenSource& source, GazeSettings settings, bool titles_enabled,
    const CalibrationProfile* profile, EyeGains default_gains,
    double head_only_yaw_degrees, std::int64_t max_window_ms)
    // NOLINTEND(bugprone-easily-swappable-parameters)
    : source_(source),
      settings_(settings),
      titles_enabled_(titles_enabled),
      default_gains_(default_gains),
      head_only_yaw_degrees_(head_only_yaw_degrees),
      max_window_ms_(max_window_ms),
      profile_(profile) {}

void ScreenContextReporter::SetProfile(const CalibrationProfile* profile) {
  const std::scoped_lock lock(mutex_);
  profile_ = profile;
}

// See the declaration (src/screen_context_reporter.h) for why pose/resting
// and eye/eye_resting are never swappable in practice.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
ScreenContextReporter::Zone ScreenContextReporter::ZoneOf(
    const std::optional<HeadRotation>& pose,
    const std::optional<HeadRotation>& resting,
    const std::optional<EyeSignal>& eye,
    const std::optional<EyeSignal>& eye_resting, ScreenSide side) const {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  // A calibrated person's own gains take over from the configured default
  // (#137): once a profile exists it always carries a full calibration
  // (CalibrationSession::Next only ever saves one once every step,
  // including the gains step, has passed), so there is no "partially
  // calibrated" gains value to fall back from.
  const EyeGains gains = profile_ != nullptr ? profile_->gains : default_gains_;
  const std::optional<HeadRotation> angle =
      pose ? std::make_optional(ComposeGaze(*pose, eye, eye_resting, gains,
                                            head_only_yaw_degrees_))
           : std::nullopt;
  const Gaze zone =
      angle ? CoarseGazeFrom(*angle, resting, side, settings_) : Gaze::kUnknown;
  return {.zone = zone, .angle = angle};
}

// See the declaration (src/screen_context_reporter.h) for why these four
// are never swappable in practice.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::string ScreenContextReporter::Json(
    const std::optional<HeadRotation>& pose,
    const std::optional<HeadRotation>& resting,
    const std::optional<EyeSignal>& eye,
    const std::optional<EyeSignal>& eye_resting) const {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  const std::optional<ActiveApp> app = source_.CurrentApp(titles_enabled_);
  const std::optional<MousePosition> mouse = source_.Mouse();
  const ScreenSide side = SideOf(source_.Screens());
  const std::scoped_lock lock(mutex_);
  const Zone read = ZoneOf(pose, resting, eye, eye_resting, side);
  // Additive and independent of `read` (#61 item 12): a profile's own
  // saved points, not CoarseGazeFrom's uncalibrated thresholds, say which
  // screen the composed gaze is nearest.
  const std::optional<std::size_t> calibrated_screen =
      (read.angle != std::nullopt && profile_ != nullptr)
          ? NearestCalibratedScreen(*profile_, *read.angle)
          : std::nullopt;

  std::string json = "{";
  if (app) {
    json += R"("active_app":)" + protocol::Quoted(app->name);
    if (titles_enabled_ && app->title) {
      json += R"(,"window_title":)" + protocol::Quoted(*app->title);
    }
    json += ',';
  }
  if (mouse) {
    json += R"("mouse":{"x":)" + PixelNumber(mouse->x) + R"(,"y":)" +
            PixelNumber(mouse->y) + "},";
  }
  json += R"("gaze":)" + protocol::Quoted(Name(read.zone));
  if (calibrated_screen) {
    json += R"(,"calibrated_screen":)" + std::to_string(*calibrated_screen);
  }
  return json + "}";
}

// See the declaration (src/screen_context_reporter.h) for why these four
// are never swappable in practice.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void ScreenContextReporter::Sample(const std::optional<HeadRotation>& pose,
                                   const std::optional<HeadRotation>& resting,
                                   const std::optional<EyeSignal>& eye,
                                   const std::optional<EyeSignal>& eye_resting,
                                   std::int64_t at_ms) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  const ScreenSide side = SideOf(source_.Screens());
  const std::scoped_lock lock(mutex_);
  const Zone read = ZoneOf(pose, resting, eye, eye_resting, side);
  samples_.push_back({.at_ms = at_ms,
                      .zone = read.zone,
                      .angle = read.angle,
                      .resting = resting});
  // The sample just pushed is never evicted by this loop (its own
  // at_ms - at_ms is 0, never over max_window_ms_), so samples_ is never
  // emptied by it either: no need to guard front() against that.
  while (at_ms - samples_.front().at_ms > max_window_ms_) {
    samples_.pop_front();
  }
}

GazeWindow ScreenContextReporter::Between(
    std::int64_t start_ms, std::int64_t end_ms,
    const std::optional<ContentRect>& content) const {
  const ScreenRect main = [this] {
    for (const ScreenRect& screen : source_.Screens()) {
      if (screen.main) {
        return screen;
      }
    }
    return ScreenRect{};
  }();
  const std::scoped_lock lock(mutex_);
  std::array<int, 4> zone_votes{};
  int yes_votes = 0;
  int no_votes = 0;
  for (const GazeSample& sample : samples_) {
    if (sample.at_ms < start_ms || sample.at_ms >= end_ms) {
      continue;
    }
    ++zone_votes.at(static_cast<std::size_t>(sample.zone));
    if (!content || !sample.angle) {
      continue;
    }
    const ContentHit hit = ContentHitFrom(*sample.angle, sample.resting, main,
                                          *content, settings_);
    if (hit == ContentHit::kYes) {
      ++yes_votes;
    } else if (hit == ContentHit::kNo) {
      ++no_votes;
    }
  }
  GazeWindow window;
  const auto* const most_voted = std::ranges::max_element(zone_votes);
  window.gaze = *most_voted > 0
                    ? static_cast<Gaze>(most_voted - zone_votes.begin())
                    : Gaze::kUnknown;
  if (content) {
    if (yes_votes == 0 && no_votes == 0) {
      window.content = ContentHit::kUnknown;
    } else {
      window.content =
          yes_votes >= no_votes ? ContentHit::kYes : ContentHit::kNo;
    }
  }
  return window;
}

}  // namespace facepalm
