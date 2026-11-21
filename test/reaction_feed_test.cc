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
#include "src/feelings.h"

// Each test stands for the specs/ReactionFeed.tla property it is named for.

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::MockFunction;
using ::testing::Optional;

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

class ReactionFeedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ON_CALL(on_rating_, Call).WillByDefault(testing::Return(true));
  }
  MockFunction<bool(const ReactionFeed::Rating&)> on_rating_;
  std::chrono::steady_clock::time_point now_;
  ReactionFeed feed_{/*capacity=*/2, on_rating_.AsStdFunction(),
                     [this] { return now_; }};
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

TEST_F(ReactionFeedTest, ForgetEndsAWaitAndRetiresItsRatingHistory) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const auto id = feed_.React("nod", "{}");
  ASSERT_TRUE(feed_.Read("agent"));
  auto waited = std::async(std::launch::async,
                           [this] { return feed_.Wait("agent", kLong); });
  while (!feed_.Waiting("agent")) {
    std::this_thread::yield();
  }
  feed_.DisconnectAndForget("agent");
  EXPECT_FALSE(Answered(waited));
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_FALSE(feed_.Rate("agent", id, 5));
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
  const auto id = feed_.React("nod", "{}");
  ASSERT_TRUE(feed_.Read("agent"));
  feed_.DisconnectAndForget("agent");
  ASSERT_TRUE(feed_.Connect("agent"));
  EXPECT_FALSE(feed_.Rate("agent", id, 5));
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

TEST_F(NoSilentGapTest, AClientNotConnectedGetsNothing) {
  ASSERT_TRUE(feed_.Connect("left"));
  feed_.Disconnect("left");
  feed_.React("nod", "{}");

  EXPECT_THAT(feed_.Read("stranger"), Eq(std::nullopt));
  EXPECT_THAT(feed_.Wait("stranger", kShort), Eq(std::nullopt));
  EXPECT_THAT(feed_.Read("left"), Eq(std::nullopt));
  EXPECT_FALSE(feed_.Waiting("stranger"));
}

using RateOnlyShownTest = ReactionFeedTest;

TEST_F(RateOnlyShownTest, AShownReadingIsRated) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("nod", "{}");
  feed_.Read("agent");

  EXPECT_CALL(on_rating_,
              Call(AllOf(Field(&ReactionFeed::Rating::client, Eq("agent")),
                         Field(&ReactionFeed::Rating::id, Eq(id)),
                         Field(&ReactionFeed::Rating::score, Eq(4)))));
  EXPECT_TRUE(feed_.Rate("agent", id, 4));
}

TEST_F(RateOnlyShownTest, AFailedFeedbackSinkIsNotReportedAsSuccess) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("Gesture: nod", "{}", 1234);
  ASSERT_TRUE(feed_.Read("agent"));
  EXPECT_CALL(
      on_rating_,
      Call(AllOf(Field(&ReactionFeed::Rating::summary, Eq("Gesture: nod")),
                 Field(&ReactionFeed::Rating::published_at_ms,
                       Eq(std::optional<std::int64_t>(1234))))))
      .WillOnce(testing::Return(false));
  EXPECT_FALSE(feed_.Rate("agent", id, 4));
}

TEST_F(RateOnlyShownTest, KeepsRatedMetadataAfterTimelineEviction) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("Gesture: nod", "{}", 1234);
  ASSERT_TRUE(feed_.Read("agent"));
  feed_.React("later", "{}", 2345);
  feed_.React("newest", "{}", 3456);
  EXPECT_CALL(
      on_rating_,
      Call(AllOf(Field(&ReactionFeed::Rating::summary, Eq("Gesture: nod")),
                 Field(&ReactionFeed::Rating::published_at_ms,
                       Eq(std::optional<std::int64_t>(1234))))))
      .WillOnce(testing::Return(true));
  EXPECT_TRUE(feed_.Rate("agent", id, 4));
}

TEST(ReactionFeedCallbackTest, SinkCanForgetSessionWithoutDeadlock) {
  ReactionFeed* active = nullptr;
  ReactionFeed feed(2, [&](const ReactionFeed::Rating& rating) {
    active->DisconnectAndForget(rating.client);
    return true;
  });
  active = &feed;
  ASSERT_TRUE(feed.Connect("agent"));
  const auto id = feed.React("Gesture: nod", "{}", 1234);
  ASSERT_TRUE(feed.Read("agent"));
  EXPECT_TRUE(feed.Rate("agent", id, 5));
  EXPECT_FALSE(feed.Read("agent"));
}

TEST_F(RateOnlyShownTest, AReadingNotShownToTheClientIsNot) {
  ASSERT_TRUE(feed_.Connect("agent"));
  ASSERT_TRUE(feed_.Connect("ai-pair"));
  const std::int64_t id = feed_.React("nod", "{}");
  feed_.Read("ai-pair");

  EXPECT_CALL(on_rating_, Call).Times(0);
  EXPECT_FALSE(feed_.Rate("agent", id, 4));
  EXPECT_FALSE(feed_.Rate("agent", id + 1, 4));
}

TEST_F(RateOnlyShownTest, AReadingShownBeforeAReconnectStillCan) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("nod", "{}");
  feed_.Read("agent");
  feed_.Disconnect("agent");
  ASSERT_TRUE(feed_.Connect("agent"));

  EXPECT_CALL(on_rating_, Call);
  EXPECT_TRUE(feed_.Rate("agent", id, 2));
}

TEST_F(RateOnlyShownTest, AScoreOutsideOneToFiveIsRefused) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("nod", "{}");
  feed_.Read("agent");

  EXPECT_CALL(on_rating_, Call).Times(0);
  EXPECT_FALSE(feed_.Rate("agent", id, 0));
  EXPECT_FALSE(feed_.Rate("agent", id, 6));
}

TEST_F(RateOnlyShownTest, AClientNotConnectedCannotRate) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("nod", "{}");
  feed_.Read("agent");
  feed_.Disconnect("agent");

  EXPECT_CALL(on_rating_, Call).Times(0);
  EXPECT_FALSE(feed_.Rate("agent", id, 3));
  EXPECT_FALSE(feed_.Rate("stranger", id, 3));
}

using RateOnlyRecentTest = ReactionFeedTest;

TEST_F(RateOnlyRecentTest, AReadingCanBeRatedUpToFiveMinutesAfterItWasShown) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("nod", "{}");
  feed_.Read("agent");
  now_ += std::chrono::minutes(5);

  EXPECT_CALL(on_rating_, Call);
  EXPECT_TRUE(feed_.Rate("agent", id, 3));
}

TEST_F(RateOnlyRecentTest, NotOnceFiveMinutesHavePassed) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("nod", "{}");
  feed_.Read("agent");
  now_ += std::chrono::minutes(5) + std::chrono::seconds(1);

  EXPECT_CALL(on_rating_, Call).Times(0);
  EXPECT_FALSE(feed_.Rate("agent", id, 3));
}

TEST_F(RateOnlyRecentTest, TheFiveMinutesRunFromWhenItWasShown) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const std::int64_t id = feed_.React("nod", "{}");
  now_ += std::chrono::minutes(4);
  feed_.Read("agent");
  now_ += std::chrono::minutes(4);

  EXPECT_CALL(on_rating_, Call);
  EXPECT_TRUE(feed_.Rate("agent", id, 3));
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
      return feed_.Beat("agent", kind, kBeat);
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
  const auto delivery = Got(feed_.Beat("agent", BeatKind::kQuestion, kShort));
  feed_.React("Gesture: nod", kNodText);

  EXPECT_THAT(FeelingOf(delivery), Eq(Feeling::kUnknown));
  EXPECT_THAT(Summaries(Got(feed_.Read("agent"))), ElementsAre("Gesture: nod"));
}

using UnknownWhenUnclearTest = BeatTest;

TEST_F(UnknownWhenUnclearTest, NothingDuringTheBeatIsUnknown) {
  ASSERT_TRUE(feed_.Connect("agent"));
  const auto delivery = Got(feed_.Beat("agent", BeatKind::kQuestion, kShort));

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
  const auto first = Got(feed_.Beat("agent", BeatKind::kQuestion, kShort));
  const auto second = Got(feed_.Beat("ai-pair", BeatKind::kQuestion, kShort));

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

  EXPECT_THAT(feed_.Beat("agent", BeatKind::kQuestion, kShort),
              Eq(std::nullopt));
  EXPECT_THAT(feed_.Wait("agent", kShort), Eq(std::nullopt));
  EXPECT_THAT(feed_.Read("agent"), Eq(std::nullopt));
  EXPECT_TRUE(Answered(beat));
}

TEST_F(OneAnswerPerBeatTest, AClientNotConnectedCannotMarkABeat) {
  EXPECT_THAT(feed_.Beat("stranger", BeatKind::kQuestion, kShort),
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

}  // namespace
}  // namespace facepalm
