#include "src/approved_feed.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "src/feed.h"
#include "src/feelings.h"

// Each test stands for the specs/DaemonAdmission.tla property it is named
// for.

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::Return;

class MockFeed : public Feed {
 public:
  MOCK_METHOD(bool, Connect, (const std::string&), (override));
  MOCK_METHOD(void, Disconnect, (const std::string&), (override));
  MOCK_METHOD(std::optional<Delivery>, Read, (const std::string&), (override));
  MOCK_METHOD(std::optional<Delivery>, Wait,
              (const std::string&, std::chrono::milliseconds), (override));
  MOCK_METHOD(std::optional<Delivery>, Beat,
              (const std::string&, BeatKind, std::chrono::milliseconds),
              (override));
  MOCK_METHOD(bool, Rate, (const std::string&, std::int64_t, int), (override));
};

class ApprovedFeedTest : public ::testing::Test {
 protected:
  ApprovedFeed Make(bool ratings_enabled = true) {
    return {inner_, [this](bool joined) { changes_.push_back(joined); },
            [this](const std::string& session) { retired_.push_back(session); },
            ratings_enabled};
  }

  MockFeed inner_;
  std::vector<bool> changes_;
  std::vector<std::string> retired_;
};

using SubscriptionMatchesTest = ApprovedFeedTest;

TEST_F(SubscriptionMatchesTest, OnlyTheFirstJoinAndTheLastLeaveCount) {
  ApprovedFeed feed = Make();
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  EXPECT_CALL(inner_, Connect("approved-2")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  ASSERT_TRUE(feed.Connect("code"));
  EXPECT_THAT(changes_, ElementsAre(true));

  feed.Disconnect("desktop");
  EXPECT_THAT(changes_, ElementsAre(true));
  feed.Disconnect("code");

  EXPECT_THAT(changes_, ElementsAre(true, false));
  EXPECT_THAT(retired_, ElementsAre("approved-1", "approved-2"));
}

TEST_F(SubscriptionMatchesTest, AFailedOrDuplicateConnectDoesNotJoin) {
  ApprovedFeed feed = Make();
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(false));
  EXPECT_FALSE(feed.Connect("desktop"));
  EXPECT_CALL(inner_, Connect("approved-2")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  EXPECT_FALSE(feed.Connect("desktop"));

  EXPECT_THAT(changes_, ElementsAre(true));
}

TEST_F(SubscriptionMatchesTest, DisconnectingAStrangerChangesNothing) {
  ApprovedFeed feed = Make();
  feed.Disconnect("nobody");

  EXPECT_TRUE(changes_.empty());
  EXPECT_TRUE(retired_.empty());
}

TEST_F(SubscriptionMatchesTest, AReconnectIsANewSession) {
  ApprovedFeed feed = Make();
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  EXPECT_CALL(inner_, Connect("approved-2")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  feed.Disconnect("desktop");
  ASSERT_TRUE(feed.Connect("desktop"));

  EXPECT_THAT(changes_, ElementsAre(true, false, true));
}

// Mock expectations exercise each operation on each client's own session.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(ApprovedFeedTest, EachClientUsesItsOwnSession) {
  ApprovedFeed feed = Make();
  EXPECT_FALSE(feed.Read("desktop"));
  EXPECT_FALSE(feed.Wait("desktop", std::chrono::milliseconds(1)));
  EXPECT_FALSE(feed.Rate("desktop", 1, 4));
  EXPECT_FALSE(
      feed.Beat("desktop", BeatKind::kQuestion, std::chrono::milliseconds(1)));
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  EXPECT_CALL(inner_, Connect("approved-2")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  ASSERT_TRUE(feed.Connect("code"));
  EXPECT_CALL(inner_, Read("approved-2")).WillOnce(Return(Feed::Delivery{}));
  EXPECT_TRUE(feed.Read("code"));
  EXPECT_CALL(inner_, Wait("approved-1", std::chrono::milliseconds(4)))
      .WillOnce(Return(Feed::Delivery{}));
  EXPECT_TRUE(feed.Wait("desktop", std::chrono::milliseconds(4)));
  EXPECT_CALL(inner_, Rate("approved-2", 3, 5)).WillOnce(Return(true));
  EXPECT_TRUE(feed.Rate("code", 3, 5));
  EXPECT_CALL(inner_, Beat("approved-1", BeatKind::kProposal,
                           std::chrono::milliseconds(6)))
      .WillOnce(Return(Feed::Delivery{}));
  EXPECT_TRUE(
      feed.Beat("desktop", BeatKind::kProposal, std::chrono::milliseconds(6)));
}

TEST_F(ApprovedFeedTest, ARejectedInnerRatingIsReportedToTheClient) {
  ApprovedFeed feed = Make();
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  EXPECT_CALL(inner_, Rate("approved-1", 8, 4)).WillOnce(Return(false));
  EXPECT_FALSE(feed.Rate("desktop", 8, 4));
}

TEST_F(ApprovedFeedTest, RefusesRatingsWhenNoFeedbackSinkExists) {
  ApprovedFeed feed = Make(/*ratings_enabled=*/false);
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  EXPECT_CALL(inner_, Rate).Times(0);
  EXPECT_FALSE(feed.Rate("desktop", 1, 5));
}

}  // namespace
}  // namespace facepalm
