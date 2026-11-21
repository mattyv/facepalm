#include "src/tuning.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include "src/feelings.h"

// The tuning rule (#35): a person's picks change a read only once a
// held-out check shows they beat the generic rule for that evidence.

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::Optional;

// A frown at a question.
Evidence FrownAtAQuestion() {
  return {.sign = "brows_down", .beat = BeatKind::kQuestion};
}
// A frown at a proposal.
Evidence FrownAtAProposal() {
  return {.sign = "brows_down", .beat = BeatKind::kProposal};
}
// A smile at a question.
Evidence SmileAtAQuestion() {
  return {.sign = "smile", .beat = BeatKind::kQuestion};
}

// `tuning` with `feeling` picked `times` times for `evidence`.
Tuning Picked(Tuning tuning, const Evidence& evidence, Feeling feeling,
              int times) {
  for (int i = 0; i < times; ++i) {
    tuning.Count(evidence, feeling);
  }
  return tuning;
}

TEST(TuningReadTest, WithNoPicksTheReadIsTheGenericOne) {
  EXPECT_THAT(Tuning().Read(FrownAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kConfused));
}

TEST(TuningReadTest, TwoPicksAreNotEnough) {
  const Tuning tuning =
      Picked(Tuning(), FrownAtAQuestion(), Feeling::kAnnoyance, 2);

  EXPECT_THAT(tuning.Read(FrownAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kConfused));
}

TEST(TuningReadTest, ThreePicksOfOneFeelingBeatTheGenericRead) {
  const Tuning tuning =
      Picked(Tuning(), FrownAtAQuestion(), Feeling::kAnnoyance, 3);

  EXPECT_THAT(tuning.Read(FrownAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
}

TEST(TuningReadTest, PicksTuneOnlyTheirOwnEvidence) {
  const Tuning tuning =
      Picked(Tuning(), FrownAtAQuestion(), Feeling::kAnnoyance, 3);

  EXPECT_THAT(tuning.Read(FrownAtAProposal(), Feeling::kConfused),
              Eq(Feeling::kConfused));
  EXPECT_THAT(tuning.Read(SmileAtAQuestion(), Feeling::kUnknown),
              Eq(Feeling::kUnknown));
}

TEST(TuningReadTest, PicksCanNameAFeelingNoGenericRuleGives) {
  const Tuning tuning =
      Picked(Tuning(), SmileAtAQuestion(), Feeling::kEngagement, 3);

  EXPECT_THAT(tuning.Read(SmileAtAQuestion(), Feeling::kUnknown),
              Eq(Feeling::kEngagement));
}

TEST(TuningReadTest, PicksThatAgreeWithTheGenericReadKeepIt) {
  const Tuning tuning =
      Picked(Tuning(), FrownAtAQuestion(), Feeling::kConfused, 5);

  EXPECT_THAT(tuning.Read(FrownAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kConfused));
}

TEST(TuningReadTest, ASplitThatDoesNotBeatTheGenericReadKeepsIt) {
  // Held out one at a time, each annoyance leaves two of each, no
  // majority, so the generic read stands and misses it; each confused
  // leaves annoyance the majority, which misses it. The person's picks
  // are right 0 times, the generic read 2.
  const Tuning tuning =
      Picked(Picked(Tuning(), FrownAtAQuestion(), Feeling::kAnnoyance, 3),
             FrownAtAQuestion(), Feeling::kConfused, 2);

  EXPECT_THAT(tuning.Read(FrownAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kConfused));
}

TEST(TuningReadTest, AClearMajorityBeatsTheGenericRead) {
  // Each annoyance held out leaves three of four: right 4 times, against
  // the generic read's 1.
  const Tuning tuning =
      Picked(Picked(Tuning(), FrownAtAQuestion(), Feeling::kAnnoyance, 4),
             FrownAtAQuestion(), Feeling::kConfused, 1);

  EXPECT_THAT(tuning.Read(FrownAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
}

TEST(TuningReadTest, AnEvenSplitBeatsNothing) {
  // Each held out leaves the other feeling the majority, which misses it.
  const Tuning tuning =
      Picked(Picked(Tuning(), SmileAtAQuestion(), Feeling::kEngagement, 2),
             SmileAtAQuestion(), Feeling::kAgreement, 2);

  EXPECT_THAT(tuning.Read(SmileAtAQuestion(), Feeling::kUnknown),
              Eq(Feeling::kUnknown));
}

TEST(TuningTextTest, WritesOneLinePerPickedFeeling) {
  const Tuning tuning = Picked(
      Picked(Picked(Tuning(), FrownAtAQuestion(), Feeling::kAnnoyance, 3),
             SmileAtAQuestion(), Feeling::kEngagement, 1),
      FrownAtAProposal(), Feeling::kConfused, 2);

  EXPECT_THAT(tuning.Text(), Eq("facepalm feelings 1\n"
                                "brows_down question annoyance 3\n"
                                "brows_down proposal confused 2\n"
                                "smile question engagement 1\n"));
}

TEST(TuningTextTest, WritesNoLinesWithNoPicks) {
  EXPECT_THAT(Tuning().Text(), Eq("facepalm feelings 1\n"));
}

TEST(TuningTextTest, ReadsBackWhatItWrote) {
  const Tuning tuning = Picked(
      Picked(Tuning(), FrownAtAQuestion(), Feeling::kAnnoyance, 3),
      Evidence{.sign = std::string(kNoSign), .beat = BeatKind::kQuestion},
      Feeling::kUnknown, 1);

  const std::optional<Tuning> parsed = Tuning::Parse(tuning.Text());

  ASSERT_TRUE(parsed.has_value());
  const Tuning read = parsed.value_or(Tuning());
  EXPECT_THAT(read.Text(), Eq(tuning.Text()));
  EXPECT_THAT(read.Read(FrownAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
}

TEST(TuningTextTest, ReadsAFileWithNoPicks) {
  EXPECT_THAT(
      Tuning::Parse("facepalm feelings 1\n"),
      Optional(testing::Property(&Tuning::Text, Eq("facepalm feelings 1\n"))));
}

TEST(TuningTextTest, RefusesAnythingElse) {
  for (const std::string_view text : {
           "",
           "facepalm feelings 2\n",
           "facepalm feelings 1",
           "facepalm feelings 1\nsmile question engagement 1",
           "facepalm feelings 1\n\n",
           "facepalm feelings 1\nSmile question engagement 1\n",
           "facepalm feelings 1\nsmile answer engagement 1\n",
           "facepalm feelings 1\nsmile question happy 1\n",
           "facepalm feelings 1\nsmile question engagement 0\n",
           "facepalm feelings 1\nsmile question engagement x\n",
           "facepalm feelings 1\nsmile question engagement 1x\n",
           "facepalm feelings 1\nsmile question engagement 1000001\n",
           "facepalm feelings 1\nsmile question engagement\n",
           "facepalm feelings 1\nsmile question engagement 1 1\n",
       }) {
    EXPECT_THAT(Tuning::Parse(text), Eq(std::nullopt)) << text;
  }
  const std::string twice = std::string("facepalm feelings 1\n") +
                            "smile question engagement 1\n" +
                            "smile question engagement 2\n";
  EXPECT_THAT(Tuning::Parse(twice), Eq(std::nullopt));
}

TEST(TuningCountTest, AFeelingStopsCountingAtTheMostPicks) {
  const std::string full = "facepalm feelings 1\nsmile question engagement " +
                           std::to_string(Tuning::kMostPicks) + "\n";
  const std::optional<Tuning> parsed = Tuning::Parse(full);
  ASSERT_TRUE(parsed.has_value());
  Tuning tuning = parsed.value_or(Tuning());

  tuning.Count(SmileAtAQuestion(), Feeling::kEngagement);

  EXPECT_THAT(tuning.Text(), Eq(full));
}

}  // namespace
}  // namespace facepalm
