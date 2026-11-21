#include "src/screen_context_reporter.h"

#include <array>
#include <cstdio>
#include <optional>
#include <string>

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
                                             bool titles_enabled)
    : source_(source), settings_(settings), titles_enabled_(titles_enabled) {}

std::string ScreenContextReporter::Json(
    const std::optional<HeadRotation>& pose,
    const std::optional<HeadRotation>& resting) const {
  const std::optional<ActiveApp> app = source_.CurrentApp(titles_enabled_);
  const std::optional<MousePosition> mouse = source_.Mouse();
  const ScreenSide side = SideOf(source_.Screens());
  const Gaze gaze =
      pose ? CoarseGazeFrom(*pose, resting, side, settings_) : Gaze::kUnknown;

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
  json += R"("gaze":)" + protocol::Quoted(Name(gaze)) + "}";
  return json;
}

}  // namespace facepalm
