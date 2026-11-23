#ifndef FACEPALM_SRC_FEELINGS_H_
#define FACEPALM_SRC_FEELINGS_H_

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "src/eye_signs.h"
#include "src/screen_context.h"

// What a reaction meant (#70, item 3 of #61): the feelings facepalm reads a
// reaction as when a client marks a beat, and the generic rule that reads
// them, and what a person's correction of a read is counted under (#35).
// This file is the only list of the feelings; every client, and the
// corrections, name these.
namespace facepalm {

// How the person felt about what was just asked. kEngagement (shown as
// "Interested") and kAnnoyance are labels a correction can name; no generic
// rule gives them yet. kUnknown ("Not sure") is the read when the rule cannot
// tell.
enum class Feeling : std::uint8_t {
  kAgreement,
  kDisagreement,
  kEngagement,
  kAnnoyance,
  kConfused,
  kUnknown,
};

// How many feelings there are.
inline constexpr int kFeelings = 6;

// Every feeling, in order.
inline constexpr std::array<Feeling, kFeelings> kEveryFeeling = {
    Feeling::kAgreement, Feeling::kDisagreement, Feeling::kEngagement,
    Feeling::kAnnoyance, Feeling::kConfused,     Feeling::kUnknown};

// The feeling's name: agreement, disagreement, engagement, annoyance,
// confused or unknown.
std::string_view Name(Feeling feeling);
// The feeling named `name`, or nullopt.
std::optional<Feeling> FeelingNamed(std::string_view name);

// What the client just did, which opens the beat: asked a question or made
// a proposal.
enum class BeatKind : std::uint8_t {
  kQuestion,
  kProposal,
};

// question or proposal.
std::string_view Name(BeatKind kind);
// The kind named `name`, or nullopt.
std::optional<BeatKind> BeatKindNamed(std::string_view name);

// The bearing one reaction has on a feeling: a nod, a shake, a puzzled face,
// or none.
enum class Cue : std::uint8_t {
  kNone,
  kNod,
  kShake,
  // Brows down or eyes narrowed. ExpressionTracker reports either only once
  // it has been held for its hold time, so a blink or a flicker is not one.
  kPuzzled,
};

// The cue a reaction gives, from its JSON text as the readers write it:
// {"gesture":"nod"}, {"gesture":"shake"}, {"expression":"brows_down"} and
// {"expression":"eyes_narrowed"} give one; anything else gives kNone.
Cue CueOf(std::string_view reaction);

// specs/FeelingBeat.tla's Rule: the feeling the cues made during a beat
// give. One kind of cue gives its feeling, a nod agreement, a shake
// disagreement, a puzzled face confused; no cue, cues of different feelings,
// or a window that is not `complete` (some of its readings were dropped)
// give kUnknown.
Feeling Interpret(std::span<const Cue> cues, bool complete);

// A reaction's own name, from its JSON text as the readers write it: "nod"
// for {"gesture":"nod"}, "brows_down" for {"expression":"brows_down"}. Empty
// for anything else. A name is 1 to 32 of a-z and _.
std::string_view SignOf(std::string_view reaction);

// Whether `word` can be a sign: 1 to 32 of a-z and _.
bool IsSign(std::string_view word);

// The sign of a beat in which no reaction was made.
inline constexpr std::string_view kNoSign = "none";

// What a beat showed, which a person's corrections are counted under (#35,
// gaze added by #143; blink rate, a long blink and widened eyes added by
// #145): the one kind of reaction made during it, or kNoSign, what the
// client marked the beat after, where they were looking during it, in the
// beat's own wording (src/screen_context.h's Gaze and BeatGazeName), and
// the three coarse eye signs a window of samples gives
// (src/eye_signs.h's BlinkRate, LongBlink and WidenedEyes) -- every one of
// these a feature the person's own read (src/tuning.h) learns from, never
// Rule's own input (#142's ContextNeverChangesFeeling, untouched by #145
// and #165 too): EvidenceOf below never sets any of the four, leaving
// them at their own unknown value; src/reaction_feed.cc's Beat fills all
// four in from the same window its reply's gaze (#142) and its own
// EyeSignsWindowFn (#165) report.
struct Evidence {
  std::string sign;
  BeatKind beat;
  Gaze gaze = Gaze::kUnknown;
  BlinkRate blink_rate = BlinkRate::kUnknown;
  LongBlink long_blink = LongBlink::kUnknown;
  WidenedEyes widened_eyes = WidenedEyes::kUnknown;
  friend auto operator<=>(const Evidence&, const Evidence&) = default;
};

// The evidence of a beat whose window held readings with `signs`, its
// gaze, blink rate, long blink and widened eyes all left at their own
// unknown value for the caller to fill in (#143, #145). nullopt when they
// were of more than one kind, one had no sign, or the window was not
// complete: a correction could not then say which reaction it was about.
std::optional<Evidence> EvidenceOf(std::span<const std::string_view> signs,
                                   BeatKind beat, bool complete);

}  // namespace facepalm

#endif  // FACEPALM_SRC_FEELINGS_H_
