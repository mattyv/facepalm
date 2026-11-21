#include "src/feelings.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace facepalm {
namespace {

// In Feeling's order.
constexpr std::array<std::string_view, kFeelings> kNames = {
    "agreement", "disagreement", "engagement",
    "annoyance", "confused",     "unknown"};

constexpr std::size_t kLongestSign = 32;

}  // namespace

bool IsSign(std::string_view word) {
  return !word.empty() && word.size() <= kLongestSign &&
         std::ranges::all_of(
             word, [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; });
}

std::string_view Name(Feeling feeling) {
  return kNames.at(static_cast<std::size_t>(feeling));
}

std::optional<Feeling> FeelingNamed(std::string_view name) {
  const auto* found = std::ranges::find(kNames, name);
  if (found == kNames.end()) {
    return std::nullopt;
  }
  return static_cast<Feeling>(found - kNames.begin());
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

std::string_view SignOf(std::string_view reaction) {
  constexpr std::string_view kOpen = R"({")";
  constexpr std::string_view kMiddle = R"(":")";
  constexpr std::string_view kClose = R"("})";
  if (!reaction.starts_with(kOpen) || !reaction.ends_with(kClose)) {
    return {};
  }
  const std::size_t middle = reaction.find(kMiddle, kOpen.size());
  if (middle == std::string_view::npos ||
      reaction.size() < middle + kMiddle.size() + kClose.size()) {
    return {};
  }
  const std::string_view key =
      reaction.substr(kOpen.size(), middle - kOpen.size());
  const std::string_view value = reaction.substr(
      middle + kMiddle.size(),
      reaction.size() - middle - kMiddle.size() - kClose.size());
  return IsSign(key) && IsSign(value) ? value : std::string_view{};
}

std::optional<Evidence> EvidenceOf(std::span<const std::string_view> signs,
                                   BeatKind beat, bool complete) {
  if (!complete || std::ranges::any_of(signs, [&](std::string_view sign) {
        return sign.empty() || sign != signs.front();
      })) {
    return std::nullopt;
  }
  return Evidence{.sign = std::string(signs.empty() ? kNoSign : signs.front()),
                  .beat = beat};
}

}  // namespace facepalm
