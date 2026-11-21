#include "src/feelings.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace facepalm {

std::string_view Name(Feeling feeling) {
  // In Feeling's order.
  constexpr std::array<std::string_view, kFeelings> kNames = {
      "agreement", "disagreement", "engagement",
      "annoyance", "confused",     "unknown"};
  return kNames.at(static_cast<std::size_t>(feeling));
}

std::string_view Name(BeatKind kind) {
  return kind == BeatKind::kQuestion ? "question" : "proposal";
}

std::optional<BeatKind> BeatKindNamed(std::string_view name) {
  if (name == "question") {
    return BeatKind::kQuestion;
  }
  if (name == "proposal") {
    return BeatKind::kProposal;
  }
  return std::nullopt;
}

Cue CueOf(std::string_view reaction) {
  if (reaction == R"({"gesture":"nod"})") {
    return Cue::kNod;
  }
  if (reaction == R"({"gesture":"shake"})") {
    return Cue::kShake;
  }
  if (reaction == R"({"expression":"brows_down"})" ||
      reaction == R"({"expression":"eyes_narrowed"})") {
    return Cue::kPuzzled;
  }
  return Cue::kNone;
}

// specs/FeelingBeat.tla's Rule, and EndBeat's unknown for a window that is
// not complete (GuardDroppedUnknown); cues of different feelings are unknown
// (GuardConflictUnknown).
Feeling Interpret(std::span<const Cue> cues, bool complete) {
  std::optional<Cue> felt;
  for (const Cue cue : cues) {
    if (cue == Cue::kNone) {
      continue;
    }
    if (felt.has_value() && *felt != cue) {
      return Feeling::kUnknown;
    }
    felt = cue;
  }
  if (!complete || !felt.has_value()) {
    return Feeling::kUnknown;
  }
  switch (*felt) {
    case Cue::kNod:
      return Feeling::kAgreement;
    case Cue::kShake:
      return Feeling::kDisagreement;
    default:
      return Feeling::kConfused;
  }
}

}  // namespace facepalm
