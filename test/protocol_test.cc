#include "src/protocol.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/feed.h"
#include "src/feelings.h"

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
  EXPECT_THAT(std::get<Hello>(request).token, Eq(""));
}

TEST(ParseTest, ReadsHelloWithAToken) {
  const std::string token(64, 'f');
  const Request request = Parsed("hello mcp-1 " + token);

  ASSERT_TRUE(std::holds_alternative<Hello>(request));
  EXPECT_THAT(std::get<Hello>(request).name, Eq("mcp-1"));
  EXPECT_THAT(std::get<Hello>(request).token, Eq(token));
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

TEST(ParseTest, ReadsFeel) {
  const Request request = Parsed("feel 12 annoyance");

  ASSERT_TRUE(std::holds_alternative<FeelRequest>(request));
  EXPECT_THAT(std::get<FeelRequest>(request).interpretation, Eq(12));
  EXPECT_THAT(std::get<FeelRequest>(request).feeling, Eq(Feeling::kAnnoyance));
}

TEST(ParseTest, ReadsEveryFeeling) {
  for (const Feeling feeling : kEveryFeeling) {
    const Request request =
        Parsed("feel 1 " + std::string(facepalm::Name(feeling)));
    ASSERT_TRUE(std::holds_alternative<FeelRequest>(request));
    EXPECT_THAT(std::get<FeelRequest>(request).feeling, Eq(feeling));
  }
}

TEST(ParseTest, ReadsReset) {
  EXPECT_TRUE(std::holds_alternative<ResetRequest>(Parsed("reset feelings")));
}

TEST(ParseTest, ReadsABeat) {
  const Request question = Parsed("beat question 1500");
  ASSERT_TRUE(std::holds_alternative<BeatRequest>(question));
  EXPECT_THAT(std::get<BeatRequest>(question).kind, Eq(BeatKind::kQuestion));
  EXPECT_THAT(std::get<BeatRequest>(question).window,
              Eq(std::chrono::milliseconds(1500)));

  const Request proposal = Parsed("beat proposal 10000");
  ASSERT_TRUE(std::holds_alternative<BeatRequest>(proposal));
  EXPECT_THAT(std::get<BeatRequest>(proposal).kind, Eq(BeatKind::kProposal));
  EXPECT_THAT(std::get<BeatRequest>(Parsed("beat question 1")).window,
              Eq(std::chrono::milliseconds(1)));
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
  EXPECT_THAT(Refused("hello three more words"),
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

TEST(ParseTest, RefusesABadToken) {
  EXPECT_THAT(Refused("hello name " + std::string(65, 'a')),
              Eq("a name is 1 to 64 of A-Z a-z 0-9 . _ -"));
  EXPECT_THAT(Refused("hello name a/b"),
              Eq("a name is 1 to 64 of A-Z a-z 0-9 . _ -"));
}

TEST(ParseTest, RefusesABadWait) {
  EXPECT_THAT(Refused("wait"), Eq("wait takes 0 to 60000 milliseconds"));
  EXPECT_THAT(Refused("wait -1"), Eq("wait takes 0 to 60000 milliseconds"));
  EXPECT_THAT(Refused("wait 60001"), Eq("wait takes 0 to 60000 milliseconds"));
  EXPECT_THAT(Refused("wait 5s"), Eq("wait takes 0 to 60000 milliseconds"));
}

TEST(ParseTest, RefusesABadBeat) {
  constexpr auto kBadBeat =
      "beat takes question or proposal and 1 to 10000 milliseconds";
  EXPECT_THAT(Refused("beat"), Eq(kBadBeat));
  EXPECT_THAT(Refused("beat question"), Eq(kBadBeat));
  EXPECT_THAT(Refused("beat answer 1500"), Eq(kBadBeat));
  EXPECT_THAT(Refused("beat question 0"), Eq(kBadBeat));
  EXPECT_THAT(Refused("beat question 10001"), Eq(kBadBeat));
  EXPECT_THAT(Refused("beat question 1.5s"), Eq(kBadBeat));
  EXPECT_THAT(Refused("beat question 1500 1"), Eq(kBadBeat));
}

TEST(ParseTest, RefusesABadFeel) {
  constexpr std::string_view kError =
      "feel takes an interpretation id and a feeling: agreement, "
      "disagreement, engagement, annoyance, confused or unknown";
  for (const std::string_view line :
       {"feel", "feel 12", "feel x agreement", "feel 12 happy",
        "feel 12 Agreement", "feel 12 agreement x"}) {
    EXPECT_THAT(Refused(line), Eq(kError)) << line;
  }
}

TEST(ParseTest, RefusesABadReset) {
  for (const std::string_view line :
       {"reset", "reset all", "reset feelings now"}) {
    EXPECT_THAT(Refused(line), Eq("reset takes feelings")) << line;
  }
}

TEST(ParseTest, NoLongerReadsARating) {
  EXPECT_THAT(Refused("rate 12 4"), Eq("unknown request"));
}

TEST(JsonTest, WritesADelivery) {
  const Feed::Delivery delivery{
      .readings = {{.id = 3,
                    .summary = "nod",
                    .detail = R"({"g":1})",
                    .published_at_ms = 1763658000123},
                   {.id = 4, .summary = "frown", .detail = "{}"}},
      .missed = 2};

  EXPECT_THAT(
      DeliveryJson(delivery),
      Eq(R"({"readings":[{"id":3,"summary":"nod","detail":{"g":1},"published_at_ms":1763658000123},)"
         R"({"id":4,"summary":"frown","detail":{}}],"missed":2})"));
}

TEST(JsonTest, WritesAReadingsContextWhenPresent) {
  const Feed::Delivery delivery{
      .readings = {{.id = 3,
                    .summary = "nod",
                    .detail = R"({"g":1})",
                    .context = R"({"gaze":"this_screen"})"},
                   {.id = 4, .summary = "frown", .detail = "{}"}},
      .missed = 0};

  EXPECT_THAT(DeliveryJson(delivery),
              Eq(R"({"readings":[{"id":3,"summary":"nod","detail":{"g":1},)"
                 R"("context":{"gaze":"this_screen"}},)"
                 R"({"id":4,"summary":"frown","detail":{}}],"missed":0})"));
}

TEST(JsonTest, WritesAnEmptyDelivery) {
  EXPECT_THAT(DeliveryJson({}), Eq(R"({"readings":[],"missed":0})"));
}

TEST(JsonTest, WritesABeatsInterpretation) {
  const Feed::Delivery delivery{
      .readings = {{.id = 7, .summary = "Gesture: nod", .detail = "{}"}},
      .missed = 0,
      .interpretation = Feed::Interpretation{.id = 2,
                                             .feeling = Feeling::kAgreement,
                                             .beat = BeatKind::kQuestion}};

  EXPECT_THAT(
      DeliveryJson(delivery),
      Eq(R"({"readings":[{"id":7,"summary":"Gesture: nod","detail":{}}],)"
         R"("missed":0,"interpretation":{"id":2,"feeling":"agreement",)"
         R"("beat":"question","feelings":["agreement","disagreement",)"
         R"("engagement","annoyance","confused","unknown"]}})"));
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
