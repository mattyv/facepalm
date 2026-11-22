#include <algorithm>
#include <optional>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/feelings.h"

namespace facepalm {
namespace {

fuzztest::Domain<Cue> AnyCue() {
  return fuzztest::ElementOf(
      {Cue::kNone, Cue::kNod, Cue::kShake, Cue::kPuzzled});
}

fuzztest::Domain<std::vector<Cue>> AnyCues() {
  return fuzztest::VectorOf(AnyCue()).WithMaxSize(8);
}

// Whether `cues` holds two different non-kNone cues.
bool HasConflict(const std::vector<Cue>& cues) {
  std::optional<Cue> felt;
  for (const Cue cue : cues) {
    if (cue == Cue::kNone) {
      continue;
    }
    if (felt.has_value() && *felt != cue) {
      return true;
    }
    felt = cue;
  }
  return false;
}

// Property test (#120): Interpret is independent of the cues' order.
void OrderIndependent(const std::vector<Cue>& cues, bool complete) {
  std::vector<Cue> reversed(cues.rbegin(), cues.rend());
  EXPECT_EQ(Interpret(cues, complete), Interpret(reversed, complete));
}
FUZZ_TEST(FeelingsFuzz, OrderIndependent)
    .WithDomains(AnyCues(), fuzztest::Arbitrary<bool>());

// Property test (#120): cues of different feelings give unknown.
void ConflictGivesUnknown(const std::vector<Cue>& cues, bool complete) {
  if (HasConflict(cues)) {
    EXPECT_EQ(Interpret(cues, complete), Feeling::kUnknown);
  }
}
FUZZ_TEST(FeelingsFuzz, ConflictGivesUnknown)
    .WithDomains(AnyCues(), fuzztest::Arbitrary<bool>());

// Property test (#120): an incomplete window always gives unknown.
void IncompleteGivesUnknown(const std::vector<Cue>& cues) {
  EXPECT_EQ(Interpret(cues, /*complete=*/false), Feeling::kUnknown);
}
FUZZ_TEST(FeelingsFuzz, IncompleteGivesUnknown).WithDomains(AnyCues());

}  // namespace
}  // namespace facepalm
