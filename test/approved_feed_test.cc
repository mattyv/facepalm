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
  MOCK_METHOD(bool, Feel, (const std::string&, std::int64_t, Feeling),
              (override));
  MOCK_METHOD(bool, Reset, (const std::string&), (override));
};

class ApprovedFeedTest : public ::testing::Test {
 protected:
  ApprovedFeed Make() {
    return {inner_, [this](bool joined) { changes_.push_back(joined); },
            [this](const std::string& session) { retired_.push_back(session); },
            [this](const Feed::Interpretation& interpretation) {
              beats_.push_back(interpretation);
            }};
  }

  MockFeed inner_;
  std::vector<bool> changes_;
  std::vector<std::string> retired_;
  std::vector<Feed::Interpretation> beats_;
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
  EXPECT_FALSE(feed.Feel("desktop", 1, Feeling::kAgreement));
  EXPECT_FALSE(feed.Reset("desktop"));
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
  EXPECT_CALL(inner_, Feel("approved-2", 3, Feeling::kConfused))
      .WillOnce(Return(true));
  EXPECT_TRUE(feed.Feel("code", 3, Feeling::kConfused));
  EXPECT_CALL(inner_, Reset("approved-2")).WillOnce(Return(true));
  EXPECT_TRUE(feed.Reset("code"));
  EXPECT_CALL(inner_, Beat("approved-1", BeatKind::kProposal,
                           std::chrono::milliseconds(6)))
      .WillOnce(Return(Feed::Delivery{}));
  EXPECT_TRUE(
      feed.Beat("desktop", BeatKind::kProposal, std::chrono::milliseconds(6)));
}

using BeatCallbackTest = ApprovedFeedTest;

TEST_F(BeatCallbackTest, AnInterpretationReachesOnBeat) {
  ApprovedFeed feed = Make();
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  const Feed::Interpretation interpretation{
      .id = 5, .feeling = Feeling::kConfused, .beat = BeatKind::kQuestion};
  EXPECT_CALL(inner_, Beat("approved-1", BeatKind::kQuestion,
                           std::chrono::milliseconds(6)))
      .WillOnce(Return(
          Feed::Delivery{.readings = {}, .interpretation = interpretation}));

  feed.Beat("desktop", BeatKind::kQuestion, std::chrono::milliseconds(6));

  EXPECT_THAT(beats_, ElementsAre(interpretation));
}

TEST_F(BeatCallbackTest, ARefusedInnerBeatCallsNothing) {
  ApprovedFeed feed = Make();
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  EXPECT_CALL(inner_, Beat("approved-1", BeatKind::kQuestion,
                           std::chrono::milliseconds(6)))
      .WillOnce(Return(std::nullopt));

  EXPECT_FALSE(
      feed.Beat("desktop", BeatKind::kQuestion, std::chrono::milliseconds(6))
          .has_value());

  EXPECT_TRUE(beats_.empty());
}

TEST_F(BeatCallbackTest, NoInterpretationCallsNothing) {
  ApprovedFeed feed = Make();
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  EXPECT_CALL(inner_, Beat("approved-1", BeatKind::kQuestion,
                           std::chrono::milliseconds(6)))
      .WillOnce(Return(Feed::Delivery{}));

  feed.Beat("desktop", BeatKind::kQuestion, std::chrono::milliseconds(6));

  EXPECT_TRUE(beats_.empty());
}

TEST_F(BeatCallbackTest, AnUnrecognizedClientNeverReachesTheInnerBeatOrOnBeat) {
  ApprovedFeed feed = Make();

  EXPECT_FALSE(
      feed.Beat("stranger", BeatKind::kQuestion, std::chrono::milliseconds(6)));

  EXPECT_TRUE(beats_.empty());
}

TEST_F(BeatCallbackTest, ACallbackIsOptional) {
  ApprovedFeed feed(inner_, [](bool) {}, [](const std::string&) {});
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  EXPECT_CALL(inner_, Beat("approved-1", BeatKind::kQuestion,
                           std::chrono::milliseconds(6)))
      .WillOnce(Return(Feed::Delivery{.readings = {},
                                      .interpretation = Feed::Interpretation{
                                          .id = 1,
                                          .feeling = Feeling::kUnknown,
                                          .beat = BeatKind::kQuestion}}));

  EXPECT_TRUE(
      feed.Beat("desktop", BeatKind::kQuestion, std::chrono::milliseconds(6))
          .has_value());
}

TEST_F(ApprovedFeedTest, ARefusedFeelingOrResetIsReportedToTheClient) {
  ApprovedFeed feed = Make();
  EXPECT_CALL(inner_, Connect("approved-1")).WillOnce(Return(true));
  ASSERT_TRUE(feed.Connect("desktop"));
  EXPECT_CALL(inner_, Feel("approved-1", 8, Feeling::kUnknown))
      .WillOnce(Return(false));
  EXPECT_FALSE(feed.Feel("desktop", 8, Feeling::kUnknown));
  EXPECT_CALL(inner_, Reset("approved-1")).WillOnce(Return(false));
  EXPECT_FALSE(feed.Reset("desktop"));
}

}  // namespace
}  // namespace facepalm
