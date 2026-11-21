#ifndef FACEPALM_SRC_FEELINGS_H_
#define FACEPALM_SRC_FEELINGS_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// What a reaction meant (#70, item 3 of #61): the feelings facepalm reads a
// reaction as when a client marks a beat, and the generic rule that reads
// them. This file is the only list of the feelings; every client, and the
// corrections of #35, name these.
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

// The feeling's name: agreement, disagreement, engagement, annoyance,
// confused or unknown.
std::string_view Name(Feeling feeling);

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

}  // namespace facepalm

#endif  // FACEPALM_SRC_FEELINGS_H_
