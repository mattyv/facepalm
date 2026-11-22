#include <string>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/feelings.h"
#include "src/tuning.h"

namespace facepalm {
namespace {

// Fuzz target 3 (#120): the feelings counts file is a person's own, kept
// under their config folder, but it is still read back off disk, so
// `Tuning::Parse` must never crash on whatever bytes are there.
void ParseDoesNotCrash(const std::string& text) { Tuning::Parse(text); }
FUZZ_TEST(TuningFuzz, ParseDoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>())
    .WithSeeds(fuzztest::ReadFilesFromDirectory(FACEPALM_FUZZ_CORPUS));

// A sign IsSign accepts: 1 to 32 of a-z and _.
fuzztest::Domain<std::string> ValidSign() {
  return fuzztest::StringOf(
             fuzztest::OneOf(fuzztest::InRange('a', 'z'), fuzztest::Just('_')))
      .WithMinSize(1)
      .WithMaxSize(32);
}

fuzztest::Domain<BeatKind> AnyBeatKind() {
  return fuzztest::ElementOf({BeatKind::kQuestion, BeatKind::kProposal});
}

fuzztest::Domain<Feeling> AnyFeeling() {
  return fuzztest::ElementOf({Feeling::kAgreement, Feeling::kDisagreement,
                              Feeling::kEngagement, Feeling::kAnnoyance,
                              Feeling::kConfused, Feeling::kUnknown});
}

// Property test (#120): the person's read never replaces the generic read
// on fewer than three agreeing picks (Tuning::kLeastPicks is 2, but the
// held-out check Tuning::Read runs needs one of those picks spare, so a
// feeling needs 3 before it can out-vote the generic read).
void FewerThanThreeNeverReplacesGeneric(const std::string& sign, BeatKind beat,
                                        Feeling feeling, Feeling generic,
                                        int count) {
  Tuning tuning;
  const Evidence evidence{.sign = sign, .beat = beat};
  for (int i = 0; i < count; ++i) {
    tuning.Count(evidence, feeling);
  }
  EXPECT_EQ(tuning.Read(evidence, generic), generic);
}
FUZZ_TEST(TuningFuzz, FewerThanThreeNeverReplacesGeneric)
    .WithDomains(ValidSign(), AnyBeatKind(), AnyFeeling(), AnyFeeling(),
                 fuzztest::InRange(0, 2));

// Property test (#120): a reset (fresh counts, as FeelingStore::Forget
// leaves them) restores the generic read for every evidence.
void ResetRestoresGenericRead(const std::string& sign, BeatKind beat,
                              Feeling generic) {
  const Tuning reset;
  EXPECT_EQ(reset.Read(Evidence{.sign = sign, .beat = beat}, generic), generic);
}
FUZZ_TEST(TuningFuzz, ResetRestoresGenericRead)
    .WithDomains(ValidSign(), AnyBeatKind(), AnyFeeling());

}  // namespace
}  // namespace facepalm
