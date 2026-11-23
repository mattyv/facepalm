#include "src/tuning.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include "src/feelings.h"
#include "src/screen_context.h"

// The tuning rule (#35): a person's picks change a read only once a
// held-out check shows they beat the generic rule for that evidence. #143
// adds gaze as a third field of that evidence: below Tuning::kLeastPicks
// picks for one gaze, the rule falls back to every pick of the same sign
// and beat regardless of gaze.

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
// A frown at a question, looking at the content.
Evidence FrownOnContentAtAQuestion() {
  return {.sign = "brows_down",
          .beat = BeatKind::kQuestion,
          .gaze = Gaze::kThisScreen};
}
// The same frown, looking away from the content.
Evidence FrownAvertedAtAQuestion() {
  return {.sign = "brows_down",
          .beat = BeatKind::kQuestion,
          .gaze = Gaze::kOtherScreen};
}
// A smile at a question, looking at the content.
Evidence SmileOnContentAtAQuestion() {
  return {
      .sign = "smile", .beat = BeatKind::kQuestion, .gaze = Gaze::kThisScreen};
}
// The same smile, away from every screen.
Evidence SmileAwayAtAQuestion() {
  return {.sign = "smile", .beat = BeatKind::kQuestion, .gaze = Gaze::kAway};
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

// #143: gaze is a third field of the evidence. Below kLeastPicks picks for
// one gaze, Read falls back to every pick of the same sign and beat,
// whatever gaze it was made under.
TEST(TuningGazeTest, BelowTheMinimumForOneGazeFallsBackToEveryGazeOfIt) {
  // Three annoyance picks looking at the content: enough on their own.
  // Reading the same frown looking away, which has none of its own,
  // still finds them through the fallback.
  const Tuning tuning =
      Picked(Tuning(), FrownOnContentAtAQuestion(), Feeling::kAnnoyance, 3);

  EXPECT_THAT(tuning.Read(FrownAvertedAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
}

TEST(TuningGazeTest, AtOrAboveTheMinimumForOneGazeReadsItAlone) {
  // Three annoyance picks on the content, three confused picks averted:
  // each gaze has enough of its own, so each is read on its own, not
  // pooled with the other.
  const Tuning tuning = Picked(
      Picked(Tuning(), FrownOnContentAtAQuestion(), Feeling::kAnnoyance, 3),
      FrownAvertedAtAQuestion(), Feeling::kConfused, 3);

  EXPECT_THAT(tuning.Read(FrownOnContentAtAQuestion(), Feeling::kUnknown),
              Eq(Feeling::kAnnoyance));
  EXPECT_THAT(tuning.Read(FrownAvertedAtAQuestion(), Feeling::kUnknown),
              Eq(Feeling::kConfused));
}

TEST(TuningGazeTest, PicksAcrossGazesCanStillNameAFeelingNoGenericRuleGives) {
  // One engagement pick on the content -- below kLeastPicks on its own --
  // and two away: pooled, the three beat the generic read, so engagement
  // -- a feeling no generic rule gives -- still comes from the person's
  // own labels before on_content alone ever sees enough of them (#143's
  // own point).
  const Tuning tuning = Picked(
      Picked(Tuning(), SmileOnContentAtAQuestion(), Feeling::kEngagement, 1),
      SmileAwayAtAQuestion(), Feeling::kEngagement, 2);

  EXPECT_THAT(tuning.Read(SmileOnContentAtAQuestion(), Feeling::kUnknown),
              Eq(Feeling::kEngagement));
}

TEST(TuningGazeTest, PicksTuneOnlyTheirOwnSignAndBeatAcrossGazes) {
  // The fallback pools gaze, never sign or beat: a frown's picks never
  // answer for a smile, or a question's for a proposal.
  const Tuning tuning =
      Picked(Tuning(), FrownOnContentAtAQuestion(), Feeling::kAnnoyance, 3);

  EXPECT_THAT(tuning.Read(SmileOnContentAtAQuestion(), Feeling::kUnknown),
              Eq(Feeling::kUnknown));
  EXPECT_THAT(tuning.Read(Evidence{.sign = "brows_down",
                                   .beat = BeatKind::kProposal,
                                   .gaze = Gaze::kOtherScreen},
                          Feeling::kConfused),
              Eq(Feeling::kConfused));
}

TEST(TuningTextTest, WritesOneLinePerPickedFeeling) {
  const Tuning tuning = Picked(
      Picked(Picked(Tuning(), FrownAtAQuestion(), Feeling::kAnnoyance, 3),
             SmileAtAQuestion(), Feeling::kEngagement, 1),
      FrownAtAProposal(), Feeling::kConfused, 2);

  EXPECT_THAT(tuning.Text(), Eq("facepalm feelings 2\n"
                                "brows_down question unknown annoyance 3\n"
                                "brows_down proposal unknown confused 2\n"
                                "smile question unknown engagement 1\n"));
}

TEST(TuningTextTest, WritesTheGazeEachPickWasMadeUnder) {
  const Tuning tuning = Picked(
      Picked(Tuning(), FrownOnContentAtAQuestion(), Feeling::kAnnoyance, 3),
      FrownAvertedAtAQuestion(), Feeling::kConfused, 2);

  EXPECT_THAT(tuning.Text(), Eq("facepalm feelings 2\n"
                                "brows_down question on_content annoyance 3\n"
                                "brows_down question averted confused 2\n"));
}

TEST(TuningTextTest, WritesNoLinesWithNoPicks) {
  EXPECT_THAT(Tuning().Text(), Eq("facepalm feelings 2\n"));
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

TEST(TuningTextTest, ReadsBackTheGazeItWrote) {
  const Tuning tuning =
      Picked(Tuning(), FrownOnContentAtAQuestion(), Feeling::kAnnoyance, 3);

  const std::optional<Tuning> parsed = Tuning::Parse(tuning.Text());

  ASSERT_TRUE(parsed.has_value());
  const Tuning read = parsed.value_or(Tuning());
  EXPECT_THAT(read.Text(), Eq(tuning.Text()));
  EXPECT_THAT(read.Read(FrownOnContentAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
  // The same picks, read by a different gaze, fall back (#143) rather
  // than vanish.
  EXPECT_THAT(read.Read(FrownAvertedAtAQuestion(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
}

TEST(TuningTextTest, ReadsAFileWithNoPicks) {
  EXPECT_THAT(
      Tuning::Parse("facepalm feelings 2\n"),
      Optional(testing::Property(&Tuning::Text, Eq("facepalm feelings 2\n"))));
}

TEST(TuningTextTest, RefusesAnythingElse) {
  for (const std::string_view text : {
           "",
           "facepalm feelings 1\n",
           "facepalm feelings 3\n",
           "facepalm feelings 2",
           "facepalm feelings 2\nsmile question on_content engagement 1",
           "facepalm feelings 2\n\n",
           "facepalm feelings 2\nSmile question on_content engagement 1\n",
           "facepalm feelings 2\nsmile answer on_content engagement 1\n",
           "facepalm feelings 2\nsmile question sideways engagement 1\n",
           "facepalm feelings 2\nsmile question on_content happy 1\n",
           "facepalm feelings 2\nsmile question on_content engagement 0\n",
           "facepalm feelings 2\nsmile question on_content engagement x\n",
           "facepalm feelings 2\nsmile question on_content engagement 1x\n",
           "facepalm feelings 2\nsmile question away engagement 1000001\n",
           "facepalm feelings 2\nsmile question on_content engagement\n",
           "facepalm feelings 2\nsmile question on_content engagement 1 1\n",
           "facepalm feelings 2\nsmile question engagement 1\n",
       }) {
    EXPECT_THAT(Tuning::Parse(text), Eq(std::nullopt)) << text;
  }
  const std::string twice = std::string("facepalm feelings 2\n") +
                            "smile question on_content engagement 1\n" +
                            "smile question on_content engagement 2\n";
  EXPECT_THAT(Tuning::Parse(twice), Eq(std::nullopt));
}

TEST(TuningCountTest, AFeelingStopsCountingAtTheMostPicks) {
  const std::string full =
      "facepalm feelings 2\nsmile question unknown engagement " +
      std::to_string(Tuning::kMostPicks) + "\n";
  const std::optional<Tuning> parsed = Tuning::Parse(full);
  ASSERT_TRUE(parsed.has_value());
  Tuning tuning = parsed.value_or(Tuning());

  tuning.Count(SmileAtAQuestion(), Feeling::kEngagement);

  EXPECT_THAT(tuning.Text(), Eq(full));
}

}  // namespace
}  // namespace facepalm
