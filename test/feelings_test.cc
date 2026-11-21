#include "src/feelings.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <vector>

// The generic rule's tests stand for the specs/FeelingBeat.tla properties
// they are named for; the window itself is reaction_feed_test's.

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::Optional;

TEST(FeelingNameTest, EachOfTheSixFeelingsHasItsName) {
  EXPECT_THAT(Name(Feeling::kAgreement), Eq("agreement"));
  EXPECT_THAT(Name(Feeling::kDisagreement), Eq("disagreement"));
  EXPECT_THAT(Name(Feeling::kEngagement), Eq("engagement"));
  EXPECT_THAT(Name(Feeling::kAnnoyance), Eq("annoyance"));
  EXPECT_THAT(Name(Feeling::kConfused), Eq("confused"));
  EXPECT_THAT(Name(Feeling::kUnknown), Eq("unknown"));
}

TEST(BeatKindTest, AQuestionAndAProposalAreNamed) {
  EXPECT_THAT(Name(BeatKind::kQuestion), Eq("question"));
  EXPECT_THAT(Name(BeatKind::kProposal), Eq("proposal"));
  EXPECT_THAT(BeatKindNamed("question"), Optional(BeatKind::kQuestion));
  EXPECT_THAT(BeatKindNamed("proposal"), Optional(BeatKind::kProposal));
  EXPECT_THAT(BeatKindNamed("answer"), Eq(std::nullopt));
}

TEST(CueOfTest, ANodAndAShakeAreTheirOwnCues) {
  EXPECT_THAT(CueOf(R"({"gesture":"nod"})"), Eq(Cue::kNod));
  EXPECT_THAT(CueOf(R"({"gesture":"shake"})"), Eq(Cue::kShake));
}

TEST(CueOfTest, BrowsDownOrEyesNarrowedArePuzzled) {
  EXPECT_THAT(CueOf(R"({"expression":"brows_down"})"), Eq(Cue::kPuzzled));
  EXPECT_THAT(CueOf(R"({"expression":"eyes_narrowed"})"), Eq(Cue::kPuzzled));
}

TEST(CueOfTest, AnythingElseBearsOnNoFeeling) {
  EXPECT_THAT(CueOf(R"({"gesture":"turn"})"), Eq(Cue::kNone));
  EXPECT_THAT(CueOf(R"({"expression":"smile"})"), Eq(Cue::kNone));
  EXPECT_THAT(CueOf(R"({"yaw":1.0,"pitch":0.0,"roll":0.0})"), Eq(Cue::kNone));
  EXPECT_THAT(CueOf("{}"), Eq(Cue::kNone));
}

TEST(ReadsOnlyTheBeatTest, OneKindOfCueGivesItsFeeling) {
  EXPECT_THAT(Interpret(std::vector{Cue::kNod}, /*complete=*/true),
              Eq(Feeling::kAgreement));
  EXPECT_THAT(Interpret(std::vector{Cue::kShake}, /*complete=*/true),
              Eq(Feeling::kDisagreement));
  EXPECT_THAT(Interpret(std::vector{Cue::kPuzzled}, /*complete=*/true),
              Eq(Feeling::kConfused));
}

TEST(ReadsOnlyTheBeatTest, RepeatsAndCuesOfNoFeelingDoNotChangeIt) {
  EXPECT_THAT(Interpret(std::vector{Cue::kNod, Cue::kNone, Cue::kNod},
                        /*complete=*/true),
              Eq(Feeling::kAgreement));
}

TEST(UnknownWhenUnclearTest, NoCueIsUnknown) {
  EXPECT_THAT(Interpret(std::vector<Cue>{}, /*complete=*/true),
              Eq(Feeling::kUnknown));
  EXPECT_THAT(Interpret(std::vector{Cue::kNone}, /*complete=*/true),
              Eq(Feeling::kUnknown));
}

TEST(UnknownWhenUnclearTest, CuesOfDifferentFeelingsAreUnknown) {
  EXPECT_THAT(Interpret(std::vector{Cue::kNod, Cue::kShake}, /*complete=*/true),
              Eq(Feeling::kUnknown));
  EXPECT_THAT(
      Interpret(std::vector{Cue::kPuzzled, Cue::kNod}, /*complete=*/true),
      Eq(Feeling::kUnknown));
}

TEST(UnknownWhenIncompleteTest, AWindowMissingAReadingIsUnknown) {
  EXPECT_THAT(Interpret(std::vector{Cue::kNod}, /*complete=*/false),
              Eq(Feeling::kUnknown));
}

}  // namespace
}  // namespace facepalm
