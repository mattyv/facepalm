#include "src/feed_clients.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::Ge;
using ::testing::Le;

class FeedClientsTest : public ::testing::Test {
 protected:
  ::testing::MockFunction<void(std::string, std::string, std::int64_t)> react_;
  FeedClients clients_{react_.AsStdFunction(), [] { return 1234; }};
};

TEST_F(FeedClientsTest, AGestureIsAReading) {
  EXPECT_CALL(react_, Call("Gesture: nod", R"({"gesture":"nod"})", 1234));

  clients_.Deliver("feed", R"({"gesture":"nod"})");
}

TEST_F(FeedClientsTest, AnExpressionIsAReading) {
  EXPECT_CALL(react_, Call("Expression: brows_down",
                           R"({"expression":"brows_down"})", 1234));

  clients_.Deliver("feed", R"({"expression":"brows_down"})");
}

TEST_F(FeedClientsTest, AnythingElseIsNot) {
  EXPECT_CALL(react_, Call).Times(0);

  for (const char* reaction :
       {R"({"yaw":1.5,"pitch":0.0,"roll":0.0})", R"({"gesture":"nod",)",
        R"({"gesture":"nod","x":"y"})", "", R"({"expression":")",
        R"({"gesture":"})", R"({"gesture":""})", R"({"looking":"away"})"}) {
    clients_.Deliver("feed", reaction);
  }
}

TEST(UnixMillisecondsTest, ReadsTheSystemClock) {
  const auto before = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const std::int64_t now = UnixMilliseconds();
  const auto after = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

  EXPECT_THAT(now, Ge(before));
  EXPECT_THAT(now, Le(after));
}

}  // namespace
}  // namespace facepalm
