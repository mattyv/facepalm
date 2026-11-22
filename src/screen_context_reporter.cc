#include "src/screen_context_reporter.h"

#include <array>
#include <cstddef>
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

ScreenContextReporter::ScreenContextReporter(ScreenSource& source,
                                             GazeSettings settings,
                                             bool titles_enabled,
                                             const CalibrationProfile* profile,
                                             EyeGains default_gains,
                                             double head_only_yaw_degrees)
    : source_(source),
      settings_(settings),
      titles_enabled_(titles_enabled),
      default_gains_(default_gains),
      head_only_yaw_degrees_(head_only_yaw_degrees),
      profile_(profile) {}

void ScreenContextReporter::SetProfile(const CalibrationProfile* profile) {
  const std::scoped_lock lock(mutex_);
  profile_ = profile;
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
  // A calibrated person's own gains take over from the configured default
  // (#137): once a profile exists it always carries a full calibration
  // (CalibrationSession::Next only ever saves one once every step,
  // including the gains step, has passed), so there is no "partially
  // calibrated" gains value to fall back from.
  const EyeGains gains = profile_ != nullptr ? profile_->gains : default_gains_;
  const std::optional<HeadRotation> gaze =
      pose ? std::make_optional(ComposeGaze(*pose, eye, eye_resting, gains,
                                            head_only_yaw_degrees_))
           : std::nullopt;
  const Gaze read =
      gaze ? CoarseGazeFrom(*gaze, resting, side, settings_) : Gaze::kUnknown;
  // Additive and independent of `read` (#61 item 12): a profile's own
  // saved points, not CoarseGazeFrom's uncalibrated thresholds, say which
  // screen the composed gaze is nearest.
  const std::optional<std::size_t> calibrated_screen =
      (gaze != std::nullopt && profile_ != nullptr)
          ? NearestCalibratedScreen(*profile_, *gaze)
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
  json += R"("gaze":)" + protocol::Quoted(Name(read));
  if (calibrated_screen) {
    json += R"(,"calibrated_screen":)" + std::to_string(*calibrated_screen);
  }
  return json + "}";
}

}  // namespace facepalm
