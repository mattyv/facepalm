#include "src/screen_context.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "src/head_pose.h"

namespace facepalm {
namespace {

constexpr double kFullTurnDegrees = 360;

// Whether `other` sits entirely to one side of `main`, with no overlap in
// x: LookAwayDetector takes the same "no partial credit" approach to its
// own threshold.
bool EntirelyLeftOf(const ScreenRect& main, const ScreenRect& other) {
  return other.x + other.width <= main.x;
}

bool EntirelyRightOf(const ScreenRect& main, const ScreenRect& other) {
  return other.x >= main.x + main.width;
}

}  // namespace

std::string_view Name(Gaze gaze) {
  // In Gaze's order, as src/feelings.cc's own Name(Feeling) is written.
  constexpr std::array<std::string_view, 4> kNames = {
      "this_screen", "other_screen", "away", "unknown"};
  return kNames.at(static_cast<std::size_t>(gaze));
}

ScreenSide SideOf(std::span<const ScreenRect> screens) {
  const auto main = std::ranges::find_if(
      screens, [](const ScreenRect& screen) { return screen.main; });
  if (main == screens.end()) {
    return {};
  }
  ScreenSide side;
  for (const ScreenRect& screen : screens) {
    if (&screen == &*main) {
      continue;
    }
    side.left = side.left || EntirelyLeftOf(*main, screen);
    side.right = side.right || EntirelyRightOf(*main, screen);
  }
  return side;
}

Gaze CoarseGazeFrom(const HeadRotation& gaze,
                    const std::optional<HeadRotation>& resting, ScreenSide side,
                    const GazeSettings& settings) {
  if (!resting) {
    return Gaze::kUnknown;
  }
  // As LookAwayDetector's own `off`: yaw wrapped at the +/-180 seam, the
  // larger of it and the pitch difference.
  const double yaw_off =
      std::remainder(gaze.yaw_degrees - resting->yaw_degrees, kFullTurnDegrees);
  const double pitch_off = gaze.pitch_degrees - resting->pitch_degrees;
  const double off = std::max(std::abs(yaw_off), std::abs(pitch_off));
  if (off <= settings.other_screen_degrees) {
    return Gaze::kThisScreen;
  }
  if (off > settings.away_degrees) {
    return Gaze::kAway;
  }
  // Between the two: a turn toward a screen `side` knows about there is
  // read as that screen; positive yaw is a turn to the subject's left
  // (src/reaction.h), which is the left side of their own screens too.
  if ((yaw_off > 0 && side.left) || (yaw_off < 0 && side.right)) {
    return Gaze::kOtherScreen;
  }
  return Gaze::kUnknown;
}

std::string_view BeatGazeName(Gaze gaze) {
  // In Gaze's own order, as Name(Gaze) is.
  constexpr std::array<std::string_view, 4> kNames = {"on_content", "averted",
                                                      "away", "unknown"};
  return kNames.at(static_cast<std::size_t>(gaze));
}

std::optional<Gaze> BeatGazeNamed(std::string_view name) {
  // In Gaze's own order, as BeatGazeName's own kNames is.
  constexpr std::array<std::string_view, 4> kNames = {"on_content", "averted",
                                                      "away", "unknown"};
  const auto* found = std::ranges::find(kNames, name);
  if (found == kNames.end()) {
    return std::nullopt;
  }
  return static_cast<Gaze>(found - kNames.begin());
}

std::string_view Name(ContentHit hit) {
  constexpr std::array<std::string_view, 3> kNames = {"yes", "no", "unknown"};
  return kNames.at(static_cast<std::size_t>(hit));
}

ContentHit ContentHitFrom(const HeadRotation& gaze,
                          const std::optional<HeadRotation>& resting,
                          const ScreenRect& main, const ContentRect& content,
                          const GazeSettings& settings) {
  if (!resting) {
    return ContentHit::kUnknown;
  }
  const double yaw_off =
      std::remainder(gaze.yaw_degrees - resting->yaw_degrees, kFullTurnDegrees);
  const double pitch_off = gaze.pitch_degrees - resting->pitch_degrees;
  if (std::max(std::abs(yaw_off), std::abs(pitch_off)) >
      settings.other_screen_degrees) {
    return ContentHit::kUnknown;
  }
  // -1..1 relative to resting at the screen's own centre; positive yaw
  // (a turn to the subject's left) moves toward the screen's own left,
  // the lower end of its x, so it subtracts rather than adds.
  const double u = std::clamp(
      0.5 - (0.5 * (yaw_off / settings.other_screen_degrees)), 0.0, 1.0);
  const double v = std::clamp(
      0.5 + (0.5 * (pitch_off / settings.other_screen_degrees)), 0.0, 1.0);
  const double px = main.x + (u * main.width);
  const double py = main.y + (v * main.height);
  const bool inside = px >= content.x && px <= content.x + content.width &&
                      py >= content.y && py <= content.y + content.height;
  return inside ? ContentHit::kYes : ContentHit::kNo;
}

}  // namespace facepalm
