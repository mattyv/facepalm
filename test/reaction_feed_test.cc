#include "src/reaction_feed.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/eye_signs.h"
#include "src/feelings.h"
#include "src/screen_context.h"
#include "src/tuner.h"

// Each test stands for the specs/ReactionFeed.tla property it is named for,
// or, from the beats on, specs/FeelingBeat.tla's or
// specs/FeelingFeedback.tla's.

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Return;
using ::testing::ReturnArg;

constexpr auto kLong = std::chrono::seconds(10);
constexpr auto kShort = std::chrono::milliseconds(10);
// Well inside kLong: a wait answered by an event, not by its timeout.
constexpr auto kPrompt = std::chrono::seconds(5);

// The wait's result, failing unless it came well before its timeout.
std::optional<ReactionFeed::Delivery> Answered(
    std::future<std::optional<ReactionFeed::Delivery>>& waited) {
  EXPECT_THAT(waited.wait_for(kPrompt), Eq(std::future_status::ready));
  return waited.get();
}

// The delivery, or an empty one and a failure when there is none.
ReactionFeed::Delivery Got(
    const std::optional<ReactionFeed::Delivery>& delivery) {
  if (!delivery) {
    ADD_FAILURE() << "no delivery";
    return {};
  }
  return *delivery;
}

// The summaries of the readings `delivery` holds.
std::vector<std::string> Summaries(const ReactionFeed::Delivery& delivery) {
  std::vector<std::string> summaries;
  summaries.reserve(delivery.readings.size());
  for (const auto& reading : delivery.readings) {
    summaries.push_back(reading.summary);
  }
  return summaries;
}

class MockTuner : public Tuner {
 public:
  MOCK_METHOD(Feeling, Read, (const Evidence&, Feeling), (override));
  MOCK_METHOD(bool, Learn, (const Evidence&, Feeling), (override));
  MOCK_METHOD(bool, Forget, (), (override));
};

// The id of the interpretation `delivery` holds, or 0 and a failure.
std::int64_t InterpretationOf(
    const std::optional<ReactionFeed::Delivery>& delivery) {
  if (!delivery || !delivery->interpretation) {
    ADD_FAILURE() << "no interpretation";
    return 0;
  }
  return delivery->interpretation->id;
}

class ReactionFeedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // With no picks, the person's read is the generic one.
    ON_CALL(tuner_, Read).WillByDefault(ReturnArg<1>());
    ON_CALL(tuner_, Learn).WillByDefault(Return(true));
    ON_CALL(tuner_, Forget).WillByDefault(Return(true));
  }
  NiceMock<MockTuner> tuner_;
  std::chrono::steady_clock::time_point now_;
  std::int64_t now_ms_ = 0;
  // Unset (the default for every test but the gaze ones below): every
  // beat then answers GazeWindow{}, Gaze::kUnknown with no content, the
  // same as a daemon with screen context disabled (#142, #144).
  ReactionFeed::GazeWindowFn gaze_window_;
  // Unset (the default for every test but the eye-signs ones below):
  // every beat then answers EyeSigns{}, every field unknown, the same as
  // a daemon with screen context disabled (#145/#165, the same reporter
  // buffers gaze and eye signs alike).
  ReactionFeed::EyeSignsWindowFn eye_signs_window_;
  ReactionFeed feed_{
      /*capacity=*/2,
      tuner_,
      [this] { return now_; },
      [this] { return now_ms_; },
      [this](std::int64_t start, std::int64_t end,
             const std::optional<ContentRect>& content) {
        return gaze_window_ ? gaze_window_(start, end, content) : GazeWindow{};
      },
      [this](std::int64_t start, std::int64_t end) {
        return eye_signs_window_ ? eye_signs_window_(start, end) : EyeSigns{};
      }};
};

using NoRepeatTest = ReactionFeedTest;

TEST_F(NoRepeatTest, AReadShowsOnlyWhatCameSinceTheLast) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("nod", "{}");
  ASSERT_THAT(
      feed_.Read("agent"),
      Optional(Field(&ReactionFeed::Delivery::readings, testing::SizeIs(1))));
  feed_.React("frown", "{}");

  const auto delivery = feed_.Read("agent");

  ASSERT_TRUE(delivery);
  EXPECT_THAT(Summaries(Got(delivery)), ElementsAre("frown"));
  EXPECT_THAT(Summaries(Got(feed_.Read("agent"))), IsEmpty());
}

TEST_F(NoRepeatTest, EachClientHasItsOwnPlace) {
  ASSERT_TRUE(feed_.Connect("agent"));
  ASSERT_TRUE(feed_.Connect("ai-pair"));
  feed_.React("nod", "{}");
  feed_.Read("agent");

  EXPECT_THAT(Summaries(Got(feed_.Read("ai-pair"))), ElementsAre("nod"));
}

TEST_F(NoRepeatTest, AWaitAnsweredByAReadingMovesPastIt) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto waited = std::async(std::launch::async,
                           [this] { return feed_.Wait("agent", kLong); });
  while (!feed_.Waiting("agent")) {
    std::this_thread::yield();
  }
  feed_.React("nod", "{}");
  ASSERT_THAT(Summaries(Got(Answered(waited))), ElementsAre("nod"));

  EXPECT_THAT(Summaries(Got(feed_.Read("agent"))), IsEmpty());
}

TEST_F(NoRepeatTest, AReconnectingClientStartsWithNoBacklog) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.Disconnect("agent");
  feed_.React("nod", "{}");
  ASSERT_TRUE(feed_.Connect("agent"));

  EXPECT_THAT(Summaries(Got(feed_.Read("agent"))), IsEmpty());
}

TEST_F(ReactionFeedTest, ForgetEndsAWaitAndRetiresItsInterpretations) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t given = InterpretationOf(
      feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt));
  auto waited = std::async(std::launch::async,
                           [this] { return feed_.Wait("agent", kLong); });
  while (!feed_.Waiting("agent")) {
    std::this_thread::yield();
  }
  feed_.DisconnectAndForget("agent");
  EXPECT_FALSE(Answered(waited));
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_FALSE(feed_.Feel("agent", given, Feeling::kAgreement));
}

TEST_F(ReactionFeedTest, ForgetAfterDisconnectWaitsForTheOldWaiter) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto waited = std::async(std::launch::async,
                           [this] { return feed_.Wait("agent", kLong); });
  while (!feed_.Waiting("agent")) {
    std::this_thread::yield();
  }
  feed_.Disconnect("agent");
  feed_.DisconnectAndForget("agent");
  EXPECT_FALSE(Answered(waited));
  ASSERT_TRUE(feed_.Connect("agent"));
}

TEST_F(ReactionFeedTest, ForgetRetiresAnIdleOrUnknownClient) {
  feed_.DisconnectAndForget("unknown");
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t given = InterpretationOf(
      feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt));
  feed_.DisconnectAndForget("agent");
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_FALSE(feed_.Feel("agent", given, Feeling::kAgreement));
}

using NoSilentGapTest = ReactionFeedTest;

TEST_F(NoSilentGapTest, DroppedReadingsAreCountedAsMissed) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("nod", "{}");
  feed_.React("frown", "{}");
  feed_.React("smile", "{}");

  const auto delivery = feed_.Read("agent");

  ASSERT_TRUE(delivery);
  EXPECT_THAT(Summaries(Got(delivery)), ElementsAre("frown", "smile"));
  EXPECT_THAT(Got(delivery).missed, Eq(1));
}

TEST_F(NoSilentGapTest, AReadingCarriesItsIdAndDetail) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("nod", R"({"gesture":"nod"})");

  const auto delivery = feed_.Read("agent");

  ASSERT_TRUE(delivery);
  ASSERT_THAT(Got(delivery).readings, testing::SizeIs(1));
  EXPECT_THAT(Got(delivery).readings[0].id, Eq(id));
  EXPECT_THAT(Got(delivery).readings[0].detail, Eq(R"({"gesture":"nod"})"));
}

// Screen context (#61 item 11): a fourth, optional argument a caller that
// does not pass one leaves this reading without, as every other test here
// does.
TEST_F(NoSilentGapTest, AReadingCarriesItsContextWhenGiven) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("nod", R"({"gesture":"nod"})", /*published_at_ms=*/std::nullopt,
              R"({"gaze":"this_screen"})");

  const auto delivery = feed_.Read("agent");

  ASSERT_TRUE(delivery);
  ASSERT_THAT(Got(delivery).readings, testing::SizeIs(1));
  EXPECT_THAT(Got(delivery).readings[0].context,
              Optional(Eq(R"({"gaze":"this_screen"})")));
}

TEST_F(NoSilentGapTest, AReadingWithNoContextGivenCarriesNone) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("nod", R"({"gesture":"nod"})");

  const auto delivery = feed_.Read("agent");

  ASSERT_TRUE(delivery);
  ASSERT_THAT(Got(delivery).readings, testing::SizeIs(1));
  EXPECT_THAT(Got(delivery).readings[0].context, Eq(std::nullopt));
}

TEST_F(NoSilentGapTest, AClientNotConnectedGetsNothing) {
  ASSERT_TRUE(feed_.Connect("left"));
  feed_.Disconnect("left");
  feed_.React("nod", "{}");

  EXPECT_THAT(feed_.Read("stranger"), Eq(std::nullopt));
  EXPECT_THAT(feed_.Wait("stranger", kShort), Eq(std::nullopt));
  EXPECT_THAT(feed_.Read("left"), Eq(std::nullopt));
  EXPECT_FALSE(feed_.Waiting("stranger"));
}

using NoWaitPastAReadingTest = ReactionFeedTest;

TEST_F(NoWaitPastAReadingTest, ANewReadingEndsTheWait) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto waited = std::async(std::launch::async,
                           [this] { return feed_.Wait("agent", kLong); });
  while (!feed_.Waiting("agent")) {
    std::this_thread::yield();
  }

  feed_.React("nod", "{}");

  EXPECT_THAT(Summaries(Got(Answered(waited))), ElementsAre("nod"));
  EXPECT_FALSE(feed_.Waiting("agent"));
}

TEST_F(NoWaitPastAReadingTest, AReadingAlreadyThereReturnsAtOnce) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("nod", "{}");

  EXPECT_THAT(Summaries(Got(feed_.Wait("agent", kLong))), ElementsAre("nod"));
}

TEST_F(NoWaitPastAReadingTest, AMissedReadingAlsoReturnsAtOnce) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("nod", "{}");
  feed_.React("frown", "{}");
  feed_.React("smile", "{}");
  feed_.Read("agent");
  feed_.React("a", "{}");
  feed_.React("b", "{}");
  feed_.React("c", "{}");

  const auto delivery = feed_.Wait("agent", kLong);

  ASSERT_TRUE(delivery);
  EXPECT_THAT(Got(delivery).missed, Eq(1));
}

using CanShowTest = ReactionFeedTest;

TEST_F(CanShowTest, AConnectedClientIsShownANewReading) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("nod", "{}");

  EXPECT_THAT(Summaries(Got(feed_.Read("agent"))), ElementsAre("nod"));
}

TEST_F(CanShowTest, ASecondWaitFromTheSameClientIsRefused) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto waited = std::async(std::launch::async,
                           [this] { return feed_.Wait("agent", kLong); });
  while (!feed_.Waiting("agent")) {
    std::this_thread::yield();
  }

  EXPECT_THAT(feed_.Wait("agent", kShort), Eq(std::nullopt));
  EXPECT_THAT(feed_.Read("agent"), Eq(std::nullopt));
  feed_.React("nod", "{}");
  EXPECT_THAT(Summaries(Got(Answered(waited))), ElementsAre("nod"));
}

using WaitEndsTest = ReactionFeedTest;

TEST_F(WaitEndsTest, AWaitWithNoReadingTimesOutEmpty) {
  ASSERT_TRUE(feed_.Connect("agent"));

  const auto delivery = feed_.Wait("agent", kShort);

  ASSERT_TRUE(delivery);
  EXPECT_THAT(Got(delivery).readings, IsEmpty());
  EXPECT_FALSE(feed_.Waiting("agent"));
}

TEST_F(WaitEndsTest, DisconnectingEndsTheWait) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto waited = std::async(std::launch::async,
                           [this] { return feed_.Wait("agent", kLong); });
  while (!feed_.Waiting("agent")) {
    std::this_thread::yield();
  }

  feed_.Disconnect("agent");

  EXPECT_THAT(Answered(waited), Eq(std::nullopt));
}

// ReactionFeed!Disconnect ends the wait with nothing, even when the name
// connects again before the waiting thread wakes: the ended wait must not
// answer for the new connection, nor move its cursor.
TEST_F(WaitEndsTest, DisconnectingEndsTheWaitEvenWhenTheNameReconnects) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto waited = std::async(std::launch::async,
                           [this] { return feed_.Wait("agent", kLong); });
  while (!feed_.Waiting("agent")) {
    std::this_thread::yield();
  }

  feed_.Disconnect("agent");
  ASSERT_TRUE(feed_.Connect("agent"));

  EXPECT_THAT(Answered(waited), Eq(std::nullopt));
  EXPECT_FALSE(feed_.Waiting("agent"));
}

// specs/Service.tla's UniqueNames: a name already connected is refused,
// and the client keeps its place.
TEST_F(WaitEndsTest, ConnectingANameAlreadyConnectedIsRefused) {
  EXPECT_TRUE(feed_.Connect("agent"));
  feed_.React("nod", "{}");

  EXPECT_FALSE(feed_.Connect("agent"));
  EXPECT_THAT(Summaries(Got(feed_.Read("agent"))), ElementsAre("nod"));
}

// The beat tests below stand for the specs/FeelingBeat.tla properties they
// are named for.

constexpr auto kNodText = R"({"gesture":"nod"})";
constexpr auto kShakeText = R"({"gesture":"shake"})";
constexpr auto kFrownText = R"({"expression":"brows_down"})";
// Long enough that a reading made once the beat is seen open lands in it.
constexpr auto kBeat = std::chrono::milliseconds(500);

class BeatTest : public ReactionFeedTest {
 protected:
  // Starts a beat for "agent", returning once it is open, or with a failure
  // once the beat has ended without being seen open.
  std::future<std::optional<ReactionFeed::Delivery>> Mark(
      BeatKind kind = BeatKind::kQuestion) {
    auto beat = std::async(std::launch::async, [this, kind] {
      return feed_.Beat("agent", kind, kBeat, std::nullopt);
    });
    while (!feed_.Waiting("agent")) {
      if (beat.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        ADD_FAILURE() << "the beat ended before it was seen open";
        break;
      }
      std::this_thread::yield();
    }
    return beat;
  }

  // The beat's feeling, or a failure and kUnknown when it has none.
  static Feeling FeelingOf(const ReactionFeed::Delivery& delivery) {
    if (!delivery.interpretation) {
      ADD_FAILURE() << "no interpretation";
      return Feeling::kUnknown;
    }
    return delivery.interpretation->feeling;
  }

  // The beat's own gaze (#142), or a failure and kUnknown when it has no
  // interpretation.
  static Gaze GazeOf(const ReactionFeed::Delivery& delivery) {
    if (!delivery.interpretation) {
      ADD_FAILURE() << "no interpretation";
      return Gaze::kUnknown;
    }
    return delivery.interpretation->gaze;
  }

  // The beat's own content hit (#144), or a failure and nullopt when it
  // has no interpretation.
  static std::optional<ContentHit> ContentOf(
      const ReactionFeed::Delivery& delivery) {
    if (!delivery.interpretation) {
      ADD_FAILURE() << "no interpretation";
      return std::nullopt;
    }
    return delivery.interpretation->content;
  }
};

using CanAgreeTest = BeatTest;

TEST_F(CanAgreeTest, ANodDuringTheBeatIsAgreement) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark();
  feed_.React("Gesture: nod", kNodText);

  const auto delivery = Got(Answered(beat));

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kAgreement));
  EXPECT_THAT(Summaries(delivery), ElementsAre("Gesture: nod"));
  EXPECT_FALSE(feed_.Waiting("agent"));
}

using ReadsOnlyTheBeatTest = BeatTest;

TEST_F(ReadsOnlyTheBeatTest, AShakeDuringTheBeatIsDisagreement) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark(BeatKind::kProposal);
  feed_.React("Gesture: shake", kShakeText);

  const auto delivery = Got(Answered(beat));

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kDisagreement));
  EXPECT_THAT(delivery.interpretation,
              Optional(Field(&ReactionFeed::Interpretation::beat,
                             Eq(BeatKind::kProposal))));
}

TEST_F(ReadsOnlyTheBeatTest, AFrownDuringTheBeatIsConfused) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark();
  feed_.React("Expression: brows_down", kFrownText);

  EXPECT_THAT(FeelingOf(Got(Answered(beat))), Eq(Feeling::kConfused));
}

// The nod came before the beat began: it is told, as a read would tell it
// (NoSilentGap), but it is not read as a feeling about the question.
TEST_F(ReadsOnlyTheBeatTest, AReadingBeforeTheBeatIsToldButNotRead) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("Gesture: nod", kNodText);
  auto beat = Mark();
  feed_.React("Gesture: shake", kShakeText);

  const auto delivery = Got(Answered(beat));

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kDisagreement));
  EXPECT_THAT(Summaries(delivery),
              ElementsAre("Gesture: nod", "Gesture: shake"));
}

TEST_F(ReadsOnlyTheBeatTest, AReadingAfterTheBeatGoesToTheNextRead) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const auto delivery =
      Got(feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt));
  feed_.React("Gesture: nod", kNodText);

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kUnknown));
  EXPECT_THAT(Summaries(Got(feed_.Read("agent"))), ElementsAre("Gesture: nod"));
}

using UnknownWhenUnclearTest = BeatTest;

TEST_F(UnknownWhenUnclearTest, NothingDuringTheBeatIsUnknown) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const auto delivery =
      Got(feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt));

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kUnknown));
  EXPECT_THAT(delivery.readings, IsEmpty());
}

TEST_F(UnknownWhenUnclearTest, ANodAndAShakeAreUnknown) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark();
  feed_.React("Gesture: nod", kNodText);
  feed_.React("Gesture: shake", kShakeText);

  EXPECT_THAT(FeelingOf(Got(Answered(beat))), Eq(Feeling::kUnknown));
}

using UnknownWhenIncompleteTest = BeatTest;

// The feed keeps two readings: the first nod of the beat is dropped before
// it ends, and might as well have been a shake.
TEST_F(UnknownWhenIncompleteTest, ADroppedReadingMakesTheBeatUnknown) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark();
  feed_.React("Gesture: nod", kNodText);
  feed_.React("Gesture: nod", kNodText);
  feed_.React("Gesture: nod", kNodText);

  const auto delivery = Got(Answered(beat));

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kUnknown));
  EXPECT_THAT(delivery.missed, Eq(1));
}

// A reading dropped before the beat began leaves the beat's own window
// whole.
TEST_F(UnknownWhenIncompleteTest, AReadingDroppedBeforeTheBeatDoesNot) {
  ASSERT_TRUE(feed_.Connect("agent"));
  feed_.React("a", "{}");
  feed_.React("b", "{}");
  feed_.React("c", "{}");
  auto beat = Mark();
  feed_.React("Gesture: nod", kNodText);

  const auto delivery = Got(Answered(beat));

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kAgreement));
  EXPECT_THAT(delivery.missed, Eq(2));
}

using OneAnswerPerBeatTest = BeatTest;

TEST_F(OneAnswerPerBeatTest, EachBeatHasItsOwnInterpretation) {
  ASSERT_TRUE(feed_.Connect("agent"));
  ASSERT_TRUE(feed_.Connect("ai-pair"));
  const auto first =
      Got(feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt));
  const auto second =
      Got(feed_.Beat("ai-pair", BeatKind::kQuestion, kShort, std::nullopt));

  const auto id = [](const ReactionFeed::Delivery& delivery) {
    return delivery.interpretation.value_or(ReactionFeed::Interpretation{
        .id = 0, .feeling = Feeling::kUnknown, .beat = BeatKind::kProposal});
  };
  EXPECT_THAT(id(first).beat, Eq(BeatKind::kQuestion));
  EXPECT_THAT(id(first).id, testing::Gt(0));
  EXPECT_THAT(id(second).id, Eq(id(first).id + 1));
}

TEST_F(OneAnswerPerBeatTest, ABeatIsRefusedWhileTheClientWaits) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark();

  EXPECT_THAT(feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt),
              Eq(std::nullopt));
  EXPECT_THAT(feed_.Wait("agent", kShort), Eq(std::nullopt));
  EXPECT_THAT(feed_.Read("agent"), Eq(std::nullopt));
  EXPECT_TRUE(Answered(beat));
}

TEST_F(OneAnswerPerBeatTest, AClientNotConnectedCannotMarkABeat) {
  EXPECT_THAT(feed_.Beat("stranger", BeatKind::kQuestion, kShort, std::nullopt),
              Eq(std::nullopt));
}

TEST_F(OneAnswerPerBeatTest, DisconnectingEndsTheBeatUnanswered) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark();
  feed_.React("Gesture: nod", kNodText);

  feed_.Disconnect("agent");
  ASSERT_TRUE(feed_.Connect("agent"));

  EXPECT_THAT(Answered(beat), Eq(std::nullopt));
  EXPECT_FALSE(feed_.Waiting("agent"));
}

TEST_F(OneAnswerPerBeatTest, ForgettingEndsTheBeatUnanswered) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark();

  feed_.DisconnectAndForget("agent");

  EXPECT_THAT(Answered(beat), Eq(std::nullopt));
}

using BeatEndsTest = BeatTest;

TEST_F(BeatEndsTest, ABeatEndsWhenItsWindowDoes) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const auto began = std::chrono::steady_clock::now();
  auto beat = Mark();
  // A reading wakes the feed, but the beat waits out its window.
  feed_.React("Gesture: nod", kNodText);

  ASSERT_TRUE(Answered(beat));
  EXPECT_GE(std::chrono::steady_clock::now() - began, kBeat);
}

// The beat's evidence goes to the person's own reads (FeelingBeat!Answer,
// tuned by FeelingFeedback's counts).
using TunedReadTest = BeatTest;

TEST_F(TunedReadTest, TheBeatIsReadAsThePersonsPicksSay) {
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_CALL(tuner_,
              Read(Evidence{.sign = "brows_down", .beat = BeatKind::kQuestion},
                   Feeling::kConfused))
      .WillOnce(Return(Feeling::kAnnoyance));
  auto beat = Mark();
  feed_.React("Expression: brows_down", kFrownText);

  EXPECT_THAT(FeelingOf(Got(Answered(beat))), Eq(Feeling::kAnnoyance));
}

TEST_F(TunedReadTest, ABeatWithNoReactionHasEvidenceToo) {
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_CALL(tuner_, Read(Evidence{.sign = std::string(kNoSign),
                                    .beat = BeatKind::kProposal},
                           Feeling::kUnknown))
      .WillOnce(Return(Feeling::kEngagement));

  EXPECT_THAT(FeelingOf(Got(feed_.Beat("agent", BeatKind::kProposal, kShort,
                                       std::nullopt))),
              Eq(Feeling::kEngagement));
}

TEST_F(TunedReadTest, ABeatOfMixedReactionsKeepsTheGenericRead) {
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_CALL(tuner_, Read).Times(0);
  auto beat = Mark();
  feed_.React("Gesture: nod", kNodText);
  feed_.React("Gesture: shake", kShakeText);

  EXPECT_THAT(FeelingOf(Got(Answered(beat))), Eq(Feeling::kUnknown));
}

// ReactionFeed's own default for a caller that gives it no GazeWindowFn at
// all (every constructor below this one in the file instead gives it a
// forwarding lambda, for gaze_window_ below to redirect per test): a
// daemon with screen context disabled, following main.cc's own ternary.
TEST(ReactionFeedGazeDefaultTest, NoGazeWindowFnAnswersUnknownWithNoContent) {
  NiceMock<MockTuner> tuner;
  ON_CALL(tuner, Read).WillByDefault(ReturnArg<1>());
  ReactionFeed feed(/*capacity=*/2, tuner);
  ASSERT_TRUE(feed.Connect("agent"));

  const auto delivery =
      Got(feed.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt));

  if (!delivery.interpretation) {
    ADD_FAILURE() << "no interpretation";
    return;
  }
  EXPECT_THAT(delivery.interpretation->gaze, Eq(Gaze::kUnknown));
  EXPECT_THAT(delivery.interpretation->content, Eq(std::nullopt));
}

// The gaze tests below stand for #142's and #144's own context a beat's
// reply carries: the majority gaze over the window, and, given a content
// rectangle, whether it was mostly inside it. Neither ever changes the
// generic feeling by rule, the one exception (AQuestionEndingAverted
// ReadsUnknown below) being FeelingBeat.tla's own thinking guard.
using GazeContextTest = BeatTest;

// The test fixture's own feed_ always gives ReactionFeed a forwarding
// lambda (so gaze_window_ below can be set per test); this is about that
// lambda's own fallback when gaze_window_ is left unset, not about
// ReactionFeed's own default (ReactionFeedGazeDefaultTest above).
TEST_F(GazeContextTest, NoGazeSourceAnswersUnknownWithNoContent) {
  ASSERT_TRUE(feed_.Connect("agent"));

  const auto delivery =
      Got(feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt));

  EXPECT_THAT(GazeOf(delivery), Eq(Gaze::kUnknown));
  EXPECT_THAT(ContentOf(delivery), Eq(std::nullopt));
}

TEST_F(GazeContextTest, TheInterpretationCarriesTheWindowsGaze) {
  ASSERT_TRUE(feed_.Connect("agent"));
  gaze_window_ = [](std::int64_t /*start*/, std::int64_t /*end*/,
                    const std::optional<ContentRect>& /*content*/) {
    return GazeWindow{.gaze = Gaze::kThisScreen};
  };

  const auto delivery =
      Got(feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt));

  EXPECT_THAT(GazeOf(delivery), Eq(Gaze::kThisScreen));
}

TEST_F(GazeContextTest, TheContentFieldIsGivenWhenAsked) {
  ASSERT_TRUE(feed_.Connect("agent"));
  constexpr ContentRect kRect = {.x = 1, .y = 2, .width = 3, .height = 4};
  std::optional<ContentRect> seen;
  gaze_window_ = [&](std::int64_t /*start*/, std::int64_t /*end*/,
                     const std::optional<ContentRect>& content) {
    seen = content;
    return GazeWindow{.gaze = Gaze::kThisScreen, .content = ContentHit::kYes};
  };

  const auto delivery =
      Got(feed_.Beat("agent", BeatKind::kQuestion, kShort, kRect));

  EXPECT_THAT(ContentOf(delivery), Optional(Eq(ContentHit::kYes)));
  EXPECT_THAT(seen, Optional(Eq(kRect)));
}

TEST_F(GazeContextTest, ABeatWithNoRectangleAsksForNone) {
  ASSERT_TRUE(feed_.Connect("agent"));
  std::optional<ContentRect> seen = ContentRect{.width = 1, .height = 1};
  gaze_window_ = [&](std::int64_t /*start*/, std::int64_t /*end*/,
                     const std::optional<ContentRect>& content) {
    seen = content;
    return GazeWindow{};
  };

  feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt);

  EXPECT_THAT(seen, Eq(std::nullopt));
}

TEST_F(GazeContextTest, TheGazeWindowReadsTheBeatsOwnWindow) {
  ASSERT_TRUE(feed_.Connect("agent"));
  now_ms_ = 5000;
  std::int64_t seen_start = -1;
  std::int64_t seen_end = -1;
  gaze_window_ = [&](std::int64_t start, std::int64_t end,
                     const std::optional<ContentRect>& /*content*/) {
    seen_start = start;
    seen_end = end;
    return GazeWindow{};
  };

  feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt);

  EXPECT_THAT(seen_start, Eq(5000));
  EXPECT_THAT(seen_end, Eq(5000));
}

// FeelingBeat.tla's GuardAvertedUnknown, the thinking guard: a question
// beat ending averted reads unknown however the cues went, but still
// reports averted as its own gaze.
TEST_F(GazeContextTest, AQuestionEndingAvertedReadsUnknown) {
  ASSERT_TRUE(feed_.Connect("agent"));
  gaze_window_ = [](std::int64_t /*start*/, std::int64_t /*end*/,
                    const std::optional<ContentRect>& /*content*/) {
    return GazeWindow{.gaze = Gaze::kOtherScreen};
  };
  auto beat = Mark(BeatKind::kQuestion);
  feed_.React("Gesture: nod", kNodText);

  const auto delivery = Got(Answered(beat));

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kUnknown));
  EXPECT_THAT(delivery.interpretation,
              Optional(Field(&ReactionFeed::Interpretation::gaze,
                             Eq(Gaze::kOtherScreen))));
}

// The guard is for a question, not a proposal: eyes averted at the end of
// a proposal's own beat still only ever changes the feeling by rule.
TEST_F(GazeContextTest, AProposalEndingAvertedKeepsItsCuesFeeling) {
  ASSERT_TRUE(feed_.Connect("agent"));
  gaze_window_ = [](std::int64_t /*start*/, std::int64_t /*end*/,
                    const std::optional<ContentRect>& /*content*/) {
    return GazeWindow{.gaze = Gaze::kOtherScreen};
  };
  auto beat = Mark(BeatKind::kProposal);
  feed_.React("Gesture: nod", kNodText);

  EXPECT_THAT(FeelingOf(Got(Answered(beat))), Eq(Feeling::kAgreement));
}

// #143: the beat's own gaze is folded into the evidence before it is read,
// so the tuner's own Read sees the same gaze the reply reports, not just
// the sign and beat TunedReadTest above checks.
TEST_F(GazeContextTest, TheEvidenceReadCarriesTheWindowsGaze) {
  ASSERT_TRUE(feed_.Connect("agent"));
  gaze_window_ = [](std::int64_t /*start*/, std::int64_t /*end*/,
                    const std::optional<ContentRect>& /*content*/) {
    return GazeWindow{.gaze = Gaze::kThisScreen};
  };
  EXPECT_CALL(tuner_, Read(Evidence{.sign = "brows_down",
                                    .beat = BeatKind::kQuestion,
                                    .gaze = Gaze::kThisScreen},
                           Feeling::kConfused))
      .WillOnce(Return(Feeling::kAnnoyance));
  auto beat = Mark();
  feed_.React("Expression: brows_down", kFrownText);

  EXPECT_THAT(FeelingOf(Got(Answered(beat))), Eq(Feeling::kAnnoyance));
}

// #143: the same gaze goes into the evidence a later Feel teaches, not
// just the one the beat was read with.
TEST_F(GazeContextTest, TheEvidenceLearnedCarriesTheWindowsGaze) {
  ASSERT_TRUE(feed_.Connect("agent"));
  gaze_window_ = [](std::int64_t /*start*/, std::int64_t /*end*/,
                    const std::optional<ContentRect>& /*content*/) {
    return GazeWindow{.gaze = Gaze::kThisScreen};
  };
  auto beat = Mark();
  feed_.React("Expression: brows_down", kFrownText);
  const std::int64_t id = InterpretationOf(Answered(beat));
  EXPECT_CALL(tuner_, Learn(Evidence{.sign = "brows_down",
                                     .beat = BeatKind::kQuestion,
                                     .gaze = Gaze::kThisScreen},
                            Feeling::kAnnoyance))
      .WillOnce(Return(true));

  EXPECT_TRUE(feed_.Feel("agent", id, Feeling::kAnnoyance));
}

// ReactionFeed's own default for a caller that gives it no EyeSignsWindowFn
// at all, the same way ReactionFeedGazeDefaultTest proves it for gaze: a
// daemon with screen context disabled, following main.cc's own ternary.
// Unlike gaze and content, blink rate, a long blink and widened eyes carry
// no field of their own on Interpretation (#145 is the personal read's
// feature alone, never the live reply's), so this is read through the
// evidence tuner_.Read is given instead.
TEST(ReactionFeedEyeSignsDefaultTest,
     NoEyeSignsWindowFnAnswersEveryFieldUnknown) {
  NiceMock<MockTuner> tuner;
  ON_CALL(tuner, Read).WillByDefault(ReturnArg<1>());
  ReactionFeed feed(/*capacity=*/2, tuner);
  ASSERT_TRUE(feed.Connect("agent"));
  EXPECT_CALL(tuner,
              Read(Evidence{.sign = "brows_down", .beat = BeatKind::kQuestion},
                   Feeling::kConfused));
  auto beat = std::async(std::launch::async, [&feed] {
    return feed.Beat("agent", BeatKind::kQuestion, kBeat, std::nullopt);
  });
  while (!feed.Waiting("agent")) {
    std::this_thread::yield();
  }
  feed.React("Expression: brows_down", kFrownText);

  Answered(beat);
}

// The eye-signs tests below stand for #145's and #165's own context the
// personal read alone tunes on: blink rate, a long blink and widened
// eyes over the beat's own window. Never Rule's own input
// (specs/FeelingBeat.tla's ContextNeverChangesFeeling untouched), so,
// unlike gaze, none of this ever appears on Interpretation.
using EyeSignsContextTest = BeatTest;

// The test fixture's own feed_ always gives ReactionFeed a forwarding
// lambda (so eye_signs_window_ below can be set per test); this is about
// that lambda's own fallback when eye_signs_window_ is left unset, not
// about ReactionFeed's own default (ReactionFeedEyeSignsDefaultTest
// above).
TEST_F(EyeSignsContextTest, NoEyeSignsSourceReadsEveryFieldUnknown) {
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_CALL(tuner_,
              Read(Evidence{.sign = "brows_down", .beat = BeatKind::kQuestion},
                   Feeling::kConfused));
  auto beat = Mark();
  feed_.React("Expression: brows_down", kFrownText);

  Answered(beat);
}

TEST_F(EyeSignsContextTest, TheEyeSignsWindowReadsTheBeatsOwnWindow) {
  ASSERT_TRUE(feed_.Connect("agent"));
  now_ms_ = 5000;
  std::int64_t seen_start = -1;
  std::int64_t seen_end = -1;
  eye_signs_window_ = [&](std::int64_t start, std::int64_t end) {
    seen_start = start;
    seen_end = end;
    return EyeSigns{};
  };

  feed_.Beat("agent", BeatKind::kQuestion, kShort, std::nullopt);

  EXPECT_THAT(seen_start, Eq(5000));
  EXPECT_THAT(seen_end, Eq(5000));
}

// #145/#165: the beat's own blink rate, long blink and widened eyes are
// folded into the evidence before it is read, so the tuner's own Read
// sees the same ones the window gave, not just sign, beat and gaze.
TEST_F(EyeSignsContextTest, TheEvidenceReadCarriesTheWindowsEyeSigns) {
  ASSERT_TRUE(feed_.Connect("agent"));
  eye_signs_window_ = [](std::int64_t /*start*/, std::int64_t /*end*/) {
    return EyeSigns{.blink_rate = BlinkRate::kElevated,
                    .long_blink = LongBlink::kYes,
                    .widened_eyes = WidenedEyes::kYes};
  };
  EXPECT_CALL(tuner_, Read(Evidence{.sign = "brows_down",
                                    .beat = BeatKind::kQuestion,
                                    .blink_rate = BlinkRate::kElevated,
                                    .long_blink = LongBlink::kYes,
                                    .widened_eyes = WidenedEyes::kYes},
                           Feeling::kConfused))
      .WillOnce(Return(Feeling::kAnnoyance));
  auto beat = Mark();
  feed_.React("Expression: brows_down", kFrownText);

  EXPECT_THAT(FeelingOf(Got(Answered(beat))), Eq(Feeling::kAnnoyance));
}

// #145/#165: the same three fields go into the evidence a later Feel
// teaches, not just the one the beat was read with.
TEST_F(EyeSignsContextTest, TheEvidenceLearnedCarriesTheWindowsEyeSigns) {
  ASSERT_TRUE(feed_.Connect("agent"));
  eye_signs_window_ = [](std::int64_t /*start*/, std::int64_t /*end*/) {
    return EyeSigns{.blink_rate = BlinkRate::kElevated,
                    .long_blink = LongBlink::kYes,
                    .widened_eyes = WidenedEyes::kYes};
  };
  auto beat = Mark();
  feed_.React("Expression: brows_down", kFrownText);
  const std::int64_t id = InterpretationOf(Answered(beat));
  EXPECT_CALL(tuner_, Learn(Evidence{.sign = "brows_down",
                                     .beat = BeatKind::kQuestion,
                                     .blink_rate = BlinkRate::kElevated,
                                     .long_blink = LongBlink::kYes,
                                     .widened_eyes = WidenedEyes::kYes},
                            Feeling::kAnnoyance))
      .WillOnce(Return(true));

  EXPECT_TRUE(feed_.Feel("agent", id, Feeling::kAnnoyance));
}

class FeedbackTest : public BeatTest {
 protected:
  // The interpretation of a beat in which "agent" frowned.
  std::int64_t Frowned() {
    auto beat = Mark();
    feed_.React("Expression: brows_down", kFrownText);
    return InterpretationOf(Answered(beat));
  }

  const Evidence frown_{.sign = "brows_down", .beat = BeatKind::kQuestion};
};

using OnlyGivenTest = FeedbackTest;

TEST_F(OnlyGivenTest, AFeelingForTheClientsInterpretationIsLearned) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = Frowned();
  EXPECT_CALL(tuner_, Learn(frown_, Feeling::kAnnoyance))
      .WillOnce(Return(true));

  EXPECT_TRUE(feed_.Feel("agent", id, Feeling::kAnnoyance));
}

TEST_F(OnlyGivenTest, AnotherClientsOrAnUngivenInterpretationIsNot) {
  ASSERT_TRUE(feed_.Connect("agent"));
  ASSERT_TRUE(feed_.Connect("ai-pair"));
  const std::int64_t id = Frowned();
  EXPECT_CALL(tuner_, Learn).Times(0);

  EXPECT_FALSE(feed_.Feel("ai-pair", id, Feeling::kAnnoyance));
  EXPECT_FALSE(feed_.Feel("agent", id + 1, Feeling::kAnnoyance));
  EXPECT_FALSE(feed_.Feel("stranger", id, Feeling::kAnnoyance));
}

TEST_F(OnlyGivenTest, AnInterpretationGivenBeforeAReconnectIsNot) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = Frowned();
  feed_.Disconnect("agent");
  EXPECT_CALL(tuner_, Learn).Times(0);

  EXPECT_FALSE(feed_.Feel("agent", id, Feeling::kAnnoyance));
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_FALSE(feed_.Feel("agent", id, Feeling::kAnnoyance));
}

TEST_F(OnlyGivenTest, AFeelingWhoseSaveFailsIsRefused) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = Frowned();
  EXPECT_CALL(tuner_, Learn).WillOnce(Return(false));

  EXPECT_FALSE(feed_.Feel("agent", id, Feeling::kAnnoyance));
}

TEST_F(OnlyGivenTest, AMixedBeatTakesAFeelingButTeachesNothing) {
  ASSERT_TRUE(feed_.Connect("agent"));
  auto beat = Mark();
  feed_.React("Gesture: nod", kNodText);
  feed_.React("Gesture: shake", kShakeText);
  const std::int64_t given = InterpretationOf(Answered(beat));
  EXPECT_CALL(tuner_, Learn).Times(0);

  EXPECT_TRUE(feed_.Feel("agent", given, Feeling::kConfused));
  EXPECT_FALSE(feed_.Feel("agent", given, Feeling::kConfused));
}

using OncePerInterpretationTest = FeedbackTest;

TEST_F(OncePerInterpretationTest, ASecondFeelingIsRefused) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = Frowned();
  EXPECT_CALL(tuner_, Learn).WillOnce(Return(true));

  EXPECT_TRUE(feed_.Feel("agent", id, Feeling::kAnnoyance));
  EXPECT_FALSE(feed_.Feel("agent", id, Feeling::kConfused));
}

TEST_F(OncePerInterpretationTest, EvenWhenTheFirstFailedToSave) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = Frowned();
  EXPECT_CALL(tuner_, Learn).WillOnce(Return(false));

  EXPECT_FALSE(feed_.Feel("agent", id, Feeling::kAnnoyance));
  EXPECT_FALSE(feed_.Feel("agent", id, Feeling::kAnnoyance));
}

using OnlyRecentTest = FeedbackTest;

TEST_F(OnlyRecentTest, AFeelingCanComeUpToFiveMinutesAfterTheBeat) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = Frowned();
  now_ += ReactionFeed::kFeelingWindow;
  EXPECT_CALL(tuner_, Learn);

  EXPECT_TRUE(feed_.Feel("agent", id, Feeling::kAnnoyance));
}

TEST_F(OnlyRecentTest, NotOnceFiveMinutesHavePassed) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = Frowned();
  now_ += ReactionFeed::kFeelingWindow + std::chrono::seconds(1);
  EXPECT_CALL(tuner_, Learn).Times(0);

  EXPECT_FALSE(feed_.Feel("agent", id, Feeling::kAnnoyance));
}

using ResetForgetsTest = ReactionFeedTest;

TEST_F(ResetForgetsTest, AConnectedClientCanForgetEveryPick) {
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_CALL(tuner_, Forget).WillOnce(Return(true)).WillOnce(Return(false));

  EXPECT_TRUE(feed_.Reset("agent"));
  EXPECT_FALSE(feed_.Reset("agent"));
}

TEST_F(ResetForgetsTest, AClientNotConnectedCannot) {
  ASSERT_TRUE(feed_.Connect("left"));
  feed_.Disconnect("left");
  EXPECT_CALL(tuner_, Forget).Times(0);

  EXPECT_FALSE(feed_.Reset("stranger"));
  EXPECT_FALSE(feed_.Reset("left"));
}

}  // namespace
}  // namespace facepalm
