#include "src/feelings.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The generic rule's tests stand for the specs/FeelingBeat.tla properties
// they are named for; the window itself is reaction_feed_test's.

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Optional;

TEST(FeelingNameTest, EachOfTheSixFeelingsHasItsName) {
  EXPECT_THAT(Name(Feeling::kAgreement), Eq("agreement"));
  EXPECT_THAT(Name(Feeling::kDisagreement), Eq("disagreement"));
  EXPECT_THAT(Name(Feeling::kEngagement), Eq("engagement"));
  EXPECT_THAT(Name(Feeling::kAnnoyance), Eq("annoyance"));
  EXPECT_THAT(Name(Feeling::kConfused), Eq("confused"));
  EXPECT_THAT(Name(Feeling::kUnknown), Eq("unknown"));
}

TEST(FeelingNameTest, EachNameGivesItsFeeling) {
  for (const Feeling feeling : kEveryFeeling) {
    EXPECT_THAT(FeelingNamed(Name(feeling)), Optional(feeling));
  }
}

TEST(FeelingNameTest, EveryFeelingIsListedInOrder) {
  for (std::size_t i = 0; i < kEveryFeeling.size(); ++i) {
    EXPECT_THAT(static_cast<std::size_t>(kEveryFeeling.at(i)), Eq(i));
  }
}

TEST(FeelingNameTest, NoOtherNameIsAFeeling) {
  EXPECT_THAT(FeelingNamed("Agreement"), Eq(std::nullopt));
  EXPECT_THAT(FeelingNamed("happy"), Eq(std::nullopt));
  EXPECT_THAT(FeelingNamed(""), Eq(std::nullopt));
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

TEST(SignOfTest, AReactionIsSignedByItsOwnName) {
  EXPECT_THAT(SignOf(R"({"gesture":"nod"})"), Eq("nod"));
  EXPECT_THAT(SignOf(R"({"expression":"brows_down"})"), Eq("brows_down"));
  EXPECT_THAT(SignOf(R"({"gesture":"looking_away"})"), Eq("looking_away"));
}

TEST(SignOfTest, AnythingElseHasNoSign) {
  for (const std::string_view reaction :
       {"{}", R"({"yaw":1.0})", R"({"gesture":"Nod"})", R"({"gesture":""})",
        R"({"":"nod"})", R"({"gesture":"nod"} )", R"( {"gesture":"nod"})",
        R"({"a":"})", R"({":"})", R"({"gesture:"nod"})",
        R"({"gesture":"a_name_longer_than_thirty_two_chars"})"}) {
    EXPECT_THAT(SignOf(reaction), IsEmpty()) << reaction;
  }
}

TEST(IsSignTest, ASignIsOneToThirtyTwoOfLowercaseLettersAndUnderscores) {
  EXPECT_TRUE(IsSign("a"));
  EXPECT_TRUE(IsSign("looking_away"));
  EXPECT_TRUE(IsSign(std::string(32, 'x')));
  EXPECT_FALSE(IsSign(""));
  EXPECT_FALSE(IsSign(std::string(33, 'x')));
  EXPECT_FALSE(IsSign("Nod"));
  EXPECT_FALSE(IsSign("nod1"));
  EXPECT_FALSE(IsSign("nod~"));
}

TEST(EvidenceOfTest, OneKindOfReactionIsTheEvidence) {
  EXPECT_THAT(
      EvidenceOf(std::vector<std::string_view>{"brows_down"},
                 BeatKind::kQuestion, /*complete=*/true),
      Optional(Evidence{.sign = "brows_down", .beat = BeatKind::kQuestion}));
  EXPECT_THAT(EvidenceOf(std::vector<std::string_view>{"nod", "nod"},
                         BeatKind::kProposal, /*complete=*/true),
              Optional(Evidence{.sign = "nod", .beat = BeatKind::kProposal}));
}

TEST(EvidenceOfTest, AWindowWithNoReactionIsEvidenceToo) {
  EXPECT_THAT(EvidenceOf(std::vector<std::string_view>{}, BeatKind::kQuestion,
                         /*complete=*/true),
              Optional(Evidence{.sign = std::string(kNoSign),
                                .beat = BeatKind::kQuestion}));
}

TEST(EvidenceOfTest, MixedUnsignedOrIncompleteWindowsAreNot) {
  EXPECT_THAT(EvidenceOf(std::vector<std::string_view>{"nod", "smile"},
                         BeatKind::kQuestion, /*complete=*/true),
              Eq(std::nullopt));
  EXPECT_THAT(EvidenceOf(std::vector<std::string_view>{"nod", ""},
                         BeatKind::kQuestion, /*complete=*/true),
              Eq(std::nullopt));
  EXPECT_THAT(EvidenceOf(std::vector<std::string_view>{"nod"},
                         BeatKind::kQuestion, /*complete=*/false),
              Eq(std::nullopt));
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
