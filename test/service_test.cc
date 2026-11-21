#include "src/service.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/feed.h"
#include "src/feelings.h"

// Each test stands for the specs/Service.tla property it is named for. The
// protocol is pure computation, so it runs for real.

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::Return;

constexpr auto kPrompt = std::chrono::seconds(5);

class MockFeed : public Feed {
 public:
  MOCK_METHOD(bool, Connect, (const std::string& client), (override));
  MOCK_METHOD(void, Disconnect, (const std::string& client), (override));
  MOCK_METHOD(std::optional<Delivery>, Read, (const std::string& client),
              (override));
  MOCK_METHOD(std::optional<Delivery>, Wait,
              (const std::string& client, std::chrono::milliseconds timeout),
              (override));
  MOCK_METHOD(std::optional<Delivery>, Beat,
              (const std::string& client, BeatKind kind,
               std::chrono::milliseconds window),
              (override));
  MOCK_METHOD(bool, Feel,
              (const std::string& client, std::int64_t interpretation,
               Feeling feeling),
              (override));
  MOCK_METHOD(bool, Reset, (const std::string& client), (override));
};

// A connection the test writes lines into and reads replies from.
class FakeStream : public Stream {
 public:
  struct Peer {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::string> lines;
    std::vector<std::string> replies;
    bool hung_up = false;
    bool shut_down = false;

    void Send(std::string line) {
      const std::scoped_lock lock(mutex);
      lines.push_back(std::move(line));
      changed.notify_all();
    }
    void HangUp() {
      const std::scoped_lock lock(mutex);
      hung_up = true;
      changed.notify_all();
    }
    // Waits for `count` replies, and returns the last, or "" with none.
    std::string Reply(std::size_t count) {
      const std::vector<std::string> all = Replies(count);
      return all.empty() ? "" : all.back();
    }
    // Waits for `count` replies, and returns them all.
    std::vector<std::string> Replies(std::size_t count) {
      std::unique_lock lock(mutex);
      changed.wait_for(lock, kPrompt, [&] { return replies.size() >= count; });
      return replies;
    }
    bool ShutDown() {
      const std::scoped_lock lock(mutex);
      return shut_down;
    }
  };

  explicit FakeStream(std::shared_ptr<Peer> peer) : peer_(std::move(peer)) {}

  std::optional<std::string> ReadLine() override {
    std::unique_lock lock(peer_->mutex);
    peer_->changed.wait(lock, [&] {
      return !peer_->lines.empty() || peer_->hung_up || peer_->shut_down;
    });
    if (peer_->lines.empty()) {
      return std::nullopt;
    }
    std::string line = std::move(peer_->lines.front());
    peer_->lines.pop_front();
    return line;
  }

  void WriteLine(std::string_view line) override {
    const std::scoped_lock lock(peer_->mutex);
    peer_->replies.emplace_back(line);
    peer_->changed.notify_all();
  }

  void Shutdown() override {
    const std::scoped_lock lock(peer_->mutex);
    peer_->shut_down = true;
    peer_->lines.clear();
    peer_->changed.notify_all();
  }

 private:
  std::shared_ptr<Peer> peer_;
};

// Hands the service the connections the test makes.
class FakeListener : public Listener {
 public:
  struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::unique_ptr<Stream>> streams;
    bool closed = false;
    int accepted = 0;
  };

  explicit FakeListener(std::shared_ptr<Queue> queue)
      : queue_(std::move(queue)) {}

  std::unique_ptr<Stream> Accept() override {
    std::unique_lock lock(queue_->mutex);
    queue_->changed.wait(
        lock, [&] { return !queue_->streams.empty() || queue_->closed; });
    if (queue_->closed) {
      return nullptr;
    }
    auto stream = std::move(queue_->streams.front());
    queue_->streams.pop_front();
    ++queue_->accepted;
    queue_->changed.notify_all();
    return stream;
  }

  void Close() override {
    const std::scoped_lock lock(queue_->mutex);
    queue_->closed = true;
    queue_->changed.notify_all();
  }

 private:
  std::shared_ptr<Queue> queue_;
};

class ServiceTest : public ::testing::Test {
 protected:
  ServiceTest() : service_(std::make_unique<FakeListener>(queue_), feed_) {
    ON_CALL(feed_, Connect).WillByDefault(Return(true));
  }

  // A new connection to the service.
  std::shared_ptr<FakeStream::Peer> Connect() {
    auto peer = std::make_shared<FakeStream::Peer>();
    const std::scoped_lock lock(queue_->mutex);
    queue_->streams.push_back(std::make_unique<FakeStream>(peer));
    queue_->changed.notify_all();
    return peer;
  }

  // A connection whose client has said hello as `name`.
  std::shared_ptr<FakeStream::Peer> Named(const std::string& name) {
    auto peer = Connect();
    peer->Send("hello " + name);
    EXPECT_THAT(peer->Replies(1), ElementsAre(R"({"ok":true})"));
    return peer;
  }

  std::shared_ptr<FakeListener::Queue> queue_ =
      std::make_shared<FakeListener::Queue>();
  NiceMock<MockFeed> feed_;
  Service service_;
};

using CanAcceptTest = ServiceTest;

TEST_F(CanAcceptTest, AClientSaysHelloAndReads) {
  service_.Start();
  EXPECT_CALL(feed_, Connect("agent")).WillOnce(Return(true));
  EXPECT_CALL(feed_, Read("agent"))
      .WillOnce(Return(Feed::Delivery{
          .readings = {{.id = 1, .summary = "nod", .detail = "{}"}}}));
  const auto peer = Named("agent");

  peer->Send("read");

  EXPECT_THAT(
      peer->Reply(2),
      Eq(R"({"readings":[{"id":1,"summary":"nod","detail":{}}],"missed":0})"));
  service_.Stop();
}

TEST_F(CanAcceptTest, AClientWaitsAndSaysHowThePersonFelt) {
  service_.Start();
  EXPECT_CALL(feed_, Wait("agent", std::chrono::milliseconds(2000)))
      .WillOnce(Return(Feed::Delivery{}));
  EXPECT_CALL(feed_, Feel("agent", 7, Feeling::kAnnoyance))
      .WillOnce(Return(true));
  EXPECT_CALL(feed_, Feel("agent", 8, Feeling::kUnknown))
      .WillOnce(Return(false));
  const auto peer = Named("agent");

  peer->Send("wait 2000");
  peer->Send("feel 7 annoyance");
  peer->Send("feel 8 unknown");

  EXPECT_THAT(
      peer->Replies(4),
      ElementsAre(R"({"ok":true})", R"({"readings":[],"missed":0})",
                  R"({"ok":true})", R"({"error":"cannot take that feeling"})"));
  service_.Stop();
}

TEST_F(CanAcceptTest, AClientResetsThePersonsFeelings) {
  service_.Start();
  EXPECT_CALL(feed_, Reset("agent"))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  const auto peer = Named("agent");

  peer->Send("reset feelings");
  peer->Send("reset feelings");

  EXPECT_THAT(peer->Replies(3),
              ElementsAre(R"({"ok":true})", R"({"ok":true})",
                          R"({"error":"cannot reset feelings"})"));
  service_.Stop();
}

TEST_F(CanAcceptTest, AClientMarksABeatAndIsToldTheFeeling) {
  service_.Start();
  EXPECT_CALL(feed_, Beat("agent", BeatKind::kProposal,
                          std::chrono::milliseconds(1500)))
      .WillOnce(Return(Feed::Delivery{.readings = {},
                                      .missed = 0,
                                      .interpretation = Feed::Interpretation{
                                          .id = 1,
                                          .feeling = Feeling::kDisagreement,
                                          .beat = BeatKind::kProposal}}));
  const auto peer = Named("agent");

  peer->Send("beat proposal 1500");

  EXPECT_THAT(peer->Replies(2),
              ElementsAre(R"({"ok":true})",
                          R"({"readings":[],"missed":0,"interpretation":)"
                          R"({"id":1,"feeling":"disagreement",)"
                          R"("beat":"proposal","feelings":["agreement",)"
                          R"("disagreement","engagement","annoyance",)"
                          R"("confused","unknown"]}})"));
  service_.Stop();
}

TEST_F(CanAcceptTest, ABadRequestGetsAnErrorAndTheConnectionStays) {
  service_.Start();
  const auto peer = Named("agent");

  peer->Send("shout");
  peer->Send("hello again");
  peer->Send("read");

  const auto replies = peer->Replies(4);
  ASSERT_THAT(replies.size(), Eq(4));
  EXPECT_THAT(replies[1], Eq(R"({"error":"unknown request"})"));
  EXPECT_THAT(replies[2], Eq(R"({"error":"already said hello"})"));
  service_.Stop();
}

TEST_F(CanAcceptTest, AConnectionMustSayHelloFirst) {
  service_.Start();
  EXPECT_CALL(feed_, Connect).Times(0);
  const auto peer = Connect();

  peer->Send("read");

  EXPECT_THAT(peer->Replies(1), ElementsAre(R"({"error":"say hello first"})"));
  service_.Stop();
}

TEST_F(CanAcceptTest, ABadHelloIsRefused) {
  service_.Start();
  const auto peer = Connect();

  peer->Send("hello a/b");

  EXPECT_THAT(
      peer->Replies(1),
      ElementsAre(R"({"error":"a name is 1 to 64 of A-Z a-z 0-9 . _ -"})"));
  service_.Stop();
}

using UniqueNamesTest = ServiceTest;

TEST_F(UniqueNamesTest, ANameAlreadyConnectedIsRefused) {
  service_.Start();
  const auto first = Named("agent");
  EXPECT_CALL(feed_, Connect("agent")).WillOnce(Return(false));
  EXPECT_CALL(feed_, Disconnect).Times(0);
  const auto second = Connect();

  second->Send("hello agent");

  EXPECT_THAT(second->Replies(1),
              ElementsAre(R"({"error":"client not accepted"})"));
  testing::Mock::VerifyAndClearExpectations(&feed_);
  service_.Stop();
}

using FeedMatchesConnsTest = ServiceTest;

TEST_F(FeedMatchesConnsTest, AHangUpTakesTheClientOutOfTheFeed) {
  service_.Start();
  const auto peer = Named("agent");
  std::promise<void> left;
  EXPECT_CALL(feed_, Disconnect("agent")).WillOnce([&] { left.set_value(); });

  peer->HangUp();

  EXPECT_THAT(left.get_future().wait_for(kPrompt),
              Eq(std::future_status::ready));
  service_.Stop();
}

TEST_F(FeedMatchesConnsTest, AHangUpBeforeHelloLeavesTheFeedAlone) {
  service_.Start();
  EXPECT_CALL(feed_, Connect).Times(0);
  EXPECT_CALL(feed_, Disconnect).Times(0);

  Connect()->HangUp();

  service_.Stop();
}

using StopWakesWaitersTest = ServiceTest;

TEST_F(StopWakesWaitersTest, StoppingEndsAWaitAndTheConnection) {
  service_.Start();
  std::promise<void> waiting;
  std::promise<void> disconnected;
  auto woken = disconnected.get_future().share();
  std::once_flag once;
  EXPECT_CALL(feed_, Wait("agent", _)).WillOnce([&] {
    waiting.set_value();
    // As ReactionFeed: Disconnect ends the wait. Bounded, so a Stop that
    // never disconnects fails the test instead of hanging it.
    woken.wait_for(2 * kPrompt);
    return std::optional<Feed::Delivery>();
  });
  EXPECT_CALL(feed_, Disconnect("agent")).WillRepeatedly([&] {
    std::call_once(once, [&] { disconnected.set_value(); });
  });
  const auto peer = Named("agent");
  peer->Send("wait 60000");
  ASSERT_THAT(waiting.get_future().wait_for(kPrompt),
              Eq(std::future_status::ready));

  const auto stopped = std::async(std::launch::async, [&] { service_.Stop(); });

  EXPECT_THAT(stopped.wait_for(kPrompt), Eq(std::future_status::ready));
  EXPECT_THAT(peer->Reply(2), Eq(R"({"error":"closing"})"));
}

// A beat is a wait like any other: Stop ends it too.
TEST_F(StopWakesWaitersTest, StoppingEndsABeatAndTheConnection) {
  service_.Start();
  std::promise<void> beating;
  std::promise<void> disconnected;
  auto woken = disconnected.get_future().share();
  std::once_flag once;
  EXPECT_CALL(feed_, Beat("agent", BeatKind::kQuestion, _)).WillOnce([&] {
    beating.set_value();
    woken.wait_for(2 * kPrompt);
    return std::optional<Feed::Delivery>();
  });
  EXPECT_CALL(feed_, Disconnect("agent")).WillRepeatedly([&] {
    std::call_once(once, [&] { disconnected.set_value(); });
  });
  const auto peer = Named("agent");
  peer->Send("beat question 10000");
  ASSERT_THAT(beating.get_future().wait_for(kPrompt),
              Eq(std::future_status::ready));

  const auto stopped = std::async(std::launch::async, [&] { service_.Stop(); });

  EXPECT_THAT(stopped.wait_for(kPrompt), Eq(std::future_status::ready));
  EXPECT_THAT(peer->Reply(2), Eq(R"({"error":"closing"})"));
}

using StopEndsTest = ServiceTest;

TEST_F(StopEndsTest, StoppingShutsEveryConnection) {
  service_.Start();
  const auto named = Named("agent");
  const auto unnamed = Connect();
  {
    std::unique_lock lock(queue_->mutex);
    ASSERT_TRUE(queue_->changed.wait_for(
        lock, kPrompt, [&] { return queue_->accepted == 2; }));
  }

  const auto stopped = std::async(std::launch::async, [&] { service_.Stop(); });

  EXPECT_THAT(stopped.wait_for(kPrompt), Eq(std::future_status::ready));
  EXPECT_TRUE(named->ShutDown());
  EXPECT_TRUE(unnamed->ShutDown());
}

TEST_F(StopEndsTest, StoppingANeverStartedServiceReturns) { service_.Stop(); }

TEST_F(StopEndsTest, DestroyingAStartedServiceStopsIt) {
  const auto queue = std::make_shared<FakeListener::Queue>();
  NiceMock<MockFeed> feed;
  {
    Service service(std::make_unique<FakeListener>(queue), feed);
    service.Start();
  }
  EXPECT_TRUE(queue->closed);
}

using NothingAfterStopTest = ServiceTest;

// A listener that, as it is closed, hands over one last connection: the
// one that arrives after Stop begins and before the listener is closed.
class LateListener : public FakeListener {
 public:
  LateListener(std::shared_ptr<Queue> queue,
               std::shared_ptr<FakeStream::Peer> late)
      : FakeListener(queue), queue_(std::move(queue)), late_(std::move(late)) {}

  void Close() override {
    {
      const std::scoped_lock lock(queue_->mutex);
      queue_->streams.push_back(std::make_unique<FakeStream>(late_));
      queue_->changed.notify_all();
    }
    {
      std::unique_lock lock(late_->mutex);
      late_->changed.wait_for(lock, kPrompt, [&] { return late_->shut_down; });
    }
    // Were it served instead, this ends it, so the test fails, not hangs.
    late_->HangUp();
    FakeListener::Close();
  }

 private:
  std::shared_ptr<Queue> queue_;
  std::shared_ptr<FakeStream::Peer> late_;
};

TEST(NothingAfterStopLateTest, AConnectionArrivingWhileStoppingIsShut) {
  const auto queue = std::make_shared<FakeListener::Queue>();
  const auto late = std::make_shared<FakeStream::Peer>();
  NiceMock<MockFeed> feed;
  EXPECT_CALL(feed, Connect).Times(0);
  Service service(std::make_unique<LateListener>(queue, late), feed);
  service.Start();

  service.Stop();

  EXPECT_TRUE(late->ShutDown());
}

TEST_F(NothingAfterStopTest, AHelloWhileStoppingLeavesTheFeed) {
  service_.Start();
  auto peer = Connect();
  std::promise<void> connecting;
  EXPECT_CALL(feed_, Connect("late")).WillOnce([&] {
    connecting.set_value();
    // Stop shuts the stream before this hello is through.
    std::unique_lock lock(peer->mutex);
    peer->changed.wait_for(lock, kPrompt, [&] { return peer->shut_down; });
    return true;
  });
  EXPECT_CALL(feed_, Disconnect("late"));
  peer->Send("hello late");
  ASSERT_THAT(connecting.get_future().wait_for(kPrompt),
              Eq(std::future_status::ready));

  service_.Stop();

  EXPECT_THAT(peer->Replies(0), testing::IsEmpty());
}

TEST_F(NothingAfterStopTest, StoppingClosesTheListener) {
  service_.Start();

  service_.Stop();

  const std::scoped_lock lock(queue_->mutex);
  EXPECT_TRUE(queue_->closed);
}

TEST_F(NothingAfterStopTest, FinishedConnectionsAreCleanedUp) {
  service_.Start();
  for (int i = 0; i < 3; ++i) {
    const auto peer = Named("agent" + std::to_string(i));
    peer->HangUp();
  }
  const auto last = Named("last");

  service_.Stop();

  EXPECT_TRUE(last->ShutDown());
}

}  // namespace
}  // namespace facepalm
