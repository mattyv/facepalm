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

// on_content, averted, away or unknown: the same four zones Name(Gaze)
// names, worded for a beat's own reply (#142) -- a beat is about the one
// thing the client just asked or proposed, so "on the content" reads
// better there than "this screen" does.
std::string_view BeatGazeName(Gaze gaze);

// The zone BeatGazeName names, or nullopt for anything else. src/tuning.h's
// Evidence stores a beat's own gaze this way (#143), the same wording its
// reply already carries.
std::optional<Gaze> BeatGazeNamed(std::string_view name);

// A content rectangle a client's beat may carry (#144): the area on
// screen its question or proposal occupies, in the same coordinate space
// as ScreenRect (#36 item 11), consistent across one call, not across
// platforms.
struct ContentRect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  friend auto operator<=>(const ContentRect&, const ContentRect&) = default;
};

// Whether gaze was mostly inside a content rectangle (#144): yes, no or
// unknown -- coarse only, never angles or window titles (#83).
enum class ContentHit : std::uint8_t { kYes, kNo, kUnknown };

// yes, no or unknown.
std::string_view Name(ContentHit hit);

// Coarse only (#144, #83): never the angle itself, only whether a point
// this far inside CoarseGazeFrom's own this_screen cone lands in
// `content`. kUnknown wherever CoarseGazeFrom would read kUnknown or
// kAway (no resting pose yet, or too far from it to place at all); past
// `settings.other_screen_degrees`, nothing is "mostly inside" any
// rectangle on `main`. Within the cone, the offset from resting, as a
// fraction of `settings.other_screen_degrees` per axis, is read as the
// same fraction across `main`'s own rect -- centred (resting looks at
// `main`'s own centre), and the coarsest reading this model can give
// without a calibration profile's own corners (#137 has one; nothing
// here reads it, the same simplification CoarseGazeFrom's own uncalibrated
// reading already makes) -- and compared to `content`. Positive yaw is a
// turn to the subject's left (src/reaction.h), which is `main`'s own left
// (the lower end of its x); positive pitch is looking down, `main`'s own
// lower end of y.
//
ContentHit ContentHitFrom(const HeadRotation& gaze,
                          const std::optional<HeadRotation>& resting,
                          const ScreenRect& main, const ContentRect& content,
                          const GazeSettings& settings);

// What a beat's window showed of where the person looked (#142, #144):
// the majority coarse gaze, and, given a content rectangle, whether gaze
// was mostly inside it. No content without one asked for.
struct GazeWindow {
  Gaze gaze = Gaze::kUnknown;
  std::optional<ContentHit> content = std::nullopt;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_CONTEXT_H_
