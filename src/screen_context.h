#ifndef FACEPALM_SRC_SCREEN_CONTEXT_H_
#define FACEPALM_SRC_SCREEN_CONTEXT_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "src/head_pose.h"

namespace facepalm {

// One screen's position and size, in whatever coordinate space the platform
// source reports (#36 item 11): consistent across one call, not across
// platforms. `main` is the screen the camera is assumed to face, since
// nothing here is calibrated to the camera's real position.
struct ScreenRect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  bool main = false;
};

// Where another screen sits relative to the main one, read from
// ScreenRect::main and the rest of a Screens() call: to its left, its
// right, both, or neither (one screen, or none known).
struct ScreenSide {
  bool left = false;
  bool right = false;
};

// How the person's head relates to their screens, read without calibration
// from head pose alone: how far it has turned from the resting pose, and,
// for the middle ground between "still at rest" and "away", which side
// another screen is known to sit on.
enum class Gaze : std::uint8_t {
  kThisScreen,
  kOtherScreen,
  kAway,
  kUnknown,
};

// this_screen, other_screen, away or unknown.
std::string_view Name(Gaze gaze);

// Config's screen_context settings for CoarseGazeFrom.
struct GazeSettings {
  // At or under this many degrees from the resting pose, still this_screen.
  double other_screen_degrees;
  // Over this many degrees from the resting pose, away (reuses
  // looking_away.degrees: the same "is the head still near its resting
  // pose" question, read as a snapshot rather than LookAwayDetector's held
  // event).
  double away_degrees;
};

// The side(s) of `screens` another screen sits on, relative to the one
// marked `main`; {false, false} with no `main` screen, or only one screen,
// in `screens`.
ScreenSide SideOf(std::span<const ScreenRect> screens);

// specs/README.md carries no entry for this: it is a stateless computation
// with no lifecycle of its own (CLAUDE.md's spec-first rule is for a
// lifecycle, such as sampling started and stopped with the camera), so it
// is tested directly rather than modeled.
//
// Reads `gaze` against `resting`: `gaze` is head pose composed with eye-in-
// head signal (src/eye_gaze.h's ComposeGaze), not head pose alone (#137);
// `resting` stays the resting head pose either way, since ComposeGaze
// already reads eye signal relative to its own resting level, so the
// difference below still isolates how far the person is looking from
// where they rest. No resting pose yet is kUnknown. Within
// `settings.other_screen_degrees` of it is kThisScreen. Past
// `settings.away_degrees` is kAway. Between the two, the turn is read as
// toward a screen `side` says is there (kOtherScreen) or, with none known
// that way, as kUnknown: a coarse middle ground this model cannot place
// without calibration (#61 item 12). Only yaw decides the side (left is a
// turn to the subject's left, following src/reaction.h's sign); pitch alone
// cannot point at a second screen in this model, so it only ever pushes the
// reading toward kAway. Monitors stacked above or below each other are not
// modeled.
Gaze CoarseGazeFrom(const HeadRotation& gaze,
                    const std::optional<HeadRotation>& resting, ScreenSide side,
                    const GazeSettings& settings);

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_CONTEXT_H_
