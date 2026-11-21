#include "src/approved_feed.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "src/feed.h"
#include "src/feelings.h"

namespace facepalm {
namespace {
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

TEST(ApprovedFeedTest, OnlyApprovedClientCanSubscribeAndLeave) {
  MockFeed inner;
  std::vector<bool> changes;
  ApprovedFeed feed(
      inner, "secret", [&](bool joined) { changes.push_back(joined); },
      [&](const std::string& name) { inner.Disconnect(name); });
  EXPECT_FALSE(feed.Connect("wrong"));
  EXPECT_TRUE(changes.empty());
  EXPECT_CALL(inner, Connect("approved-1")).WillOnce(testing::Return(true));
  EXPECT_TRUE(feed.Connect("secret"));
  EXPECT_CALL(inner, Disconnect("approved-1"));
  feed.Disconnect("secret");
  EXPECT_THAT(changes, testing::ElementsAre(true, false));
}

TEST(ApprovedFeedTest, FailedAndDuplicateConnectionsDoNotSubscribe) {
  MockFeed inner;
  std::vector<bool> changes;
  ApprovedFeed feed(
      inner, "secret", [&](bool joined) { changes.push_back(joined); },
      [&](const std::string& name) { inner.Disconnect(name); });
  EXPECT_CALL(inner, Connect("approved-1")).WillOnce(testing::Return(false));
  EXPECT_FALSE(feed.Connect("secret"));
  feed.Disconnect("wrong");
  EXPECT_TRUE(changes.empty());
}

// Mock expectations exercise each operation on the same admitted session.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(ApprovedFeedTest, OperationsUseOnlyTheAuthorizedFeedIdentity) {
  MockFeed inner;
  ApprovedFeed feed(
      inner, "secret", [](bool) {},
      [&](const std::string& name) { inner.Disconnect(name); });
  EXPECT_FALSE(feed.Read("wrong"));
  EXPECT_FALSE(feed.Wait("wrong", std::chrono::milliseconds(1)));
  EXPECT_FALSE(feed.Rate("wrong", 1, 4));
  EXPECT_FALSE(
      feed.Beat("wrong", BeatKind::kQuestion, std::chrono::milliseconds(1)));
  EXPECT_CALL(inner, Connect("approved-1")).WillOnce(testing::Return(true));
  ASSERT_TRUE(feed.Connect("secret"));
  EXPECT_CALL(inner, Read("approved-1"))
      .WillOnce(testing::Return(Feed::Delivery{}));
  EXPECT_TRUE(feed.Read("secret"));
  EXPECT_CALL(inner, Wait("approved-1", std::chrono::milliseconds(4)))
      .WillOnce(testing::Return(Feed::Delivery{}));
  EXPECT_TRUE(feed.Wait("secret", std::chrono::milliseconds(4)));
  EXPECT_CALL(inner, Rate("approved-1", 3, 5)).WillOnce(testing::Return(true));
  EXPECT_TRUE(feed.Rate("secret", 3, 5));
  EXPECT_CALL(inner, Beat("approved-1", BeatKind::kProposal,
                          std::chrono::milliseconds(6)))
      .WillOnce(testing::Return(Feed::Delivery{}));
  EXPECT_TRUE(
      feed.Beat("secret", BeatKind::kProposal, std::chrono::milliseconds(6)));
}
TEST(ApprovedFeedTest, DuplicateApprovalAndIdleDisconnectDoNotRetire) {
  MockFeed inner;
  std::vector<bool> changes;
  ApprovedFeed feed(
      inner, "secret", [&](bool joined) { changes.push_back(joined); },
      [&](const std::string& name) { inner.Disconnect(name); });
  EXPECT_CALL(inner, Disconnect).Times(0);
  feed.Disconnect("secret");
  EXPECT_CALL(inner, Connect("approved-1")).WillOnce(testing::Return(true));
  ASSERT_TRUE(feed.Connect("secret"));
  EXPECT_FALSE(feed.Connect("secret"));
  EXPECT_THAT(changes, testing::ElementsAre(true));
}

TEST(ApprovedFeedTest, RejectedInnerRatingIsReportedToTheClient) {
  MockFeed inner;
  ApprovedFeed feed(inner, "secret", [](bool) {}, [](const std::string&) {});
  EXPECT_CALL(inner, Connect("approved-1")).WillOnce(testing::Return(true));
  ASSERT_TRUE(feed.Connect("secret"));
  EXPECT_CALL(inner, Rate("approved-1", 8, 4)).WillOnce(testing::Return(false));
  EXPECT_FALSE(feed.Rate("secret", 8, 4));
}

TEST(ApprovedFeedTest, CanRefuseRatingsWhenNoFeedbackSinkExists) {
  MockFeed inner;
  ApprovedFeed feed(
      inner, "secret", [](bool) {},
      [&](const std::string& name) { inner.Disconnect(name); }, false);
  EXPECT_CALL(inner, Connect("approved-1")).WillOnce(testing::Return(true));
  ASSERT_TRUE(feed.Connect("secret"));
  EXPECT_CALL(inner, Rate).Times(0);
  EXPECT_FALSE(feed.Rate("secret", 1, 5));
}

}  // namespace
}  // namespace facepalm
