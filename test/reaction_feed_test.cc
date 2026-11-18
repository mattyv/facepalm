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
  MockFunction<void(const ReactionFeed::Rating&)> on_rating_;
  ReactionFeed feed_{/*capacity=*/2, on_rating_.AsStdFunction()};
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

}  // namespace
}  // namespace facepalm
