#include "src/protocol.h"

#include <chrono>
#include <optional>
#include <string>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/feed.h"

namespace facepalm::protocol {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;

// The request `line` parses to, failing the test when it does not.
Request Parsed(std::string_view line) {
  std::string error;
  const std::optional<Request> request = Parse(line, error);
  if (!request) {
    ADD_FAILURE() << line << ": " << error;
    return ReadRequest{};
  }
  EXPECT_THAT(error, IsEmpty());
  return *request;
}

// The error `line` is refused with, failing the test when it parses.
std::string Refused(std::string_view line) {
  std::string error;
  EXPECT_THAT(Parse(line, error), Eq(std::nullopt)) << line;
  return error;
}

TEST(ParseTest, ReadsHello) {
  const Request request = Parsed("hello claude-code.1_a");

  ASSERT_TRUE(std::holds_alternative<Hello>(request));
  EXPECT_THAT(std::get<Hello>(request).name, Eq("claude-code.1_a"));
}

TEST(ParseTest, ReadsRead) {
  EXPECT_TRUE(std::holds_alternative<ReadRequest>(Parsed("read")));
}

TEST(ParseTest, ReadsWait) {
  const Request request = Parsed("wait 2500");

  ASSERT_TRUE(std::holds_alternative<WaitRequest>(request));
  EXPECT_THAT(std::get<WaitRequest>(request).timeout,
              Eq(std::chrono::milliseconds(2500)));
}

TEST(ParseTest, ReadsRate) {
  const Request request = Parsed("rate 12 4");

  ASSERT_TRUE(std::holds_alternative<RateRequest>(request));
  EXPECT_THAT(std::get<RateRequest>(request).id, Eq(12));
  EXPECT_THAT(std::get<RateRequest>(request).score, Eq(4));
}

TEST(ParseTest, IgnoresATrailingCarriageReturn) {
  EXPECT_TRUE(std::holds_alternative<ReadRequest>(Parsed("read\r")));
}

TEST(ParseTest, RefusesAnUnknownRequest) {
  EXPECT_THAT(Refused("shout"), Eq("unknown request"));
  EXPECT_THAT(Refused(""), Eq("unknown request"));
  EXPECT_THAT(Refused("read now"), Eq("unknown request"));
}

TEST(ParseTest, RefusesABadName) {
  EXPECT_THAT(Refused("hello"), Eq("a name is 1 to 64 of A-Z a-z 0-9 . _ -"));
  EXPECT_THAT(Refused("hello two words"),
              Eq("a name is 1 to 64 of A-Z a-z 0-9 . _ -"));
  EXPECT_THAT(Refused("hello " + std::string(65, 'a')),
              Eq("a name is 1 to 64 of A-Z a-z 0-9 . _ -"));
  EXPECT_THAT(Refused("hello a/b"),
              Eq("a name is 1 to 64 of A-Z a-z 0-9 . _ -"));
  // Empty, and just past each end of the letters.
  for (const char* line : {"hello ", "hello a{b", "hello A[B"}) {
    EXPECT_THAT(Refused(line), Eq("a name is 1 to 64 of A-Z a-z 0-9 . _ -"));
  }
}

TEST(ParseTest, RefusesABadWait) {
  EXPECT_THAT(Refused("wait"), Eq("wait takes 0 to 60000 milliseconds"));
  EXPECT_THAT(Refused("wait -1"), Eq("wait takes 0 to 60000 milliseconds"));
  EXPECT_THAT(Refused("wait 60001"), Eq("wait takes 0 to 60000 milliseconds"));
  EXPECT_THAT(Refused("wait 5s"), Eq("wait takes 0 to 60000 milliseconds"));
}

TEST(ParseTest, RefusesABadRate) {
  EXPECT_THAT(Refused("rate"), Eq("rate takes a reading id and a score"));
  EXPECT_THAT(Refused("rate 12"), Eq("rate takes a reading id and a score"));
  EXPECT_THAT(Refused("rate x 4"), Eq("rate takes a reading id and a score"));
  EXPECT_THAT(Refused("rate 12 x"), Eq("rate takes a reading id and a score"));
  EXPECT_THAT(Refused("rate 12 4 5"),
              Eq("rate takes a reading id and a score"));
  EXPECT_THAT(Refused("rate 12 4294967300"),
              Eq("rate takes a reading id and a score"));
}

TEST(JsonTest, WritesADelivery) {
  const Feed::Delivery delivery{
      .readings = {{.id = 3, .summary = "nod", .detail = R"({"g":1})"},
                   {.id = 4, .summary = "frown", .detail = "{}"}},
      .missed = 2};

  EXPECT_THAT(DeliveryJson(delivery),
              Eq(R"({"readings":[{"id":3,"summary":"nod","detail":{"g":1}},)"
                 R"({"id":4,"summary":"frown","detail":{}}],"missed":2})"));
}

TEST(JsonTest, WritesAnEmptyDelivery) {
  EXPECT_THAT(DeliveryJson({}), Eq(R"({"readings":[],"missed":0})"));
}

TEST(JsonTest, EscapesASummary) {
  const Feed::Delivery delivery{
      .readings = {{.id = 1, .summary = "a\"b\\c\nd\x01", .detail = "{}"}}};

  EXPECT_THAT(DeliveryJson(delivery),
              Eq(R"({"readings":[{"id":1,"summary":"a\"b\\c\nd\u0001",)"
                 R"("detail":{}}],"missed":0})"));
}

TEST(JsonTest, WritesOkAndErrors) {
  EXPECT_THAT(kOkJson, Eq(R"({"ok":true})"));
  EXPECT_THAT(ErrorJson("bad \"thing\""), Eq(R"({"error":"bad \"thing\""})"));
}

}  // namespace
}  // namespace facepalm::protocol
