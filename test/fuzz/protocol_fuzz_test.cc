#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/protocol.h"

namespace facepalm::protocol {
namespace {

// Fuzz target 1 (#120): the hello line reaches unauthenticated local
// processes before any approval, so `Parse` must never crash on whatever
// bytes a connection happens to send.
void ParseDoesNotCrash(const std::string& line) {
  std::string error;
  Parse(line, error);
}
FUZZ_TEST(ProtocolFuzz, ParseDoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>())
    .WithSeeds(fuzztest::ReadFilesFromDirectory(FACEPALM_FUZZ_CORPUS));

// A client name Parse's IsName accepts: 1 to 64 of A-Z a-z 0-9 . _ -.
fuzztest::Domain<std::string> ValidName() {
  return fuzztest::StringOf(
             fuzztest::OneOf(fuzztest::InRange('a', 'z'),
                             fuzztest::InRange('A', 'Z'),
                             fuzztest::InRange('0', '9'),
                             fuzztest::ElementOf<char>({'.', '_', '-'})))
      .WithMinSize(1)
      .WithMaxSize(64);
}

// Property test (#120): a hello line Parse reads round-trips its name and
// token back out, whatever valid name or token it was built from.
void HelloRoundTrips(const std::string& name, const std::string& token) {
  const std::string line =
      token.empty() ? "hello " + name : "hello " + name + " " + token;
  std::string error;
  const std::optional<Request> parsed = Parse(line, error);
  ASSERT_TRUE(parsed.has_value()) << error;
  const Hello* hello = std::get_if<Hello>(&*parsed);
  ASSERT_NE(hello, nullptr);
  EXPECT_EQ(hello->name, name);
  EXPECT_EQ(hello->token, token);
}
FUZZ_TEST(ProtocolFuzz, HelloRoundTrips)
    .WithDomains(ValidName(),
                 fuzztest::OneOf(fuzztest::Just(std::string()), ValidName()));

// Property test (#144): a beat line Parse reads round-trips its kind,
// window and content rectangle alike -- the old, plain form and the new,
// with-rectangle form (#125's own fuzz target, updated for #142/#144).
void BeatRoundTrips(bool is_question, std::int64_t ms, bool with_rect,
                    std::int64_t x, std::int64_t y, std::int64_t width,
                    std::int64_t height) {
  const std::string kind = is_question ? "question" : "proposal";
  std::string line = "beat " + kind + " " + std::to_string(ms);
  if (with_rect) {
    line += " " + std::to_string(x) + " " + std::to_string(y) + " " +
            std::to_string(width) + " " + std::to_string(height);
  }
  std::string error;
  const std::optional<Request> parsed = Parse(line, error);
  ASSERT_TRUE(parsed.has_value()) << error;
  const BeatRequest* beat = std::get_if<BeatRequest>(&*parsed);
  ASSERT_NE(beat, nullptr);
  EXPECT_EQ(beat->kind,
            is_question ? BeatKind::kQuestion : BeatKind::kProposal);
  EXPECT_EQ(beat->window, std::chrono::milliseconds(ms));
  if (with_rect) {
    ASSERT_TRUE(beat->content.has_value());
    EXPECT_EQ(beat->content->x, static_cast<double>(x));
    EXPECT_EQ(beat->content->y, static_cast<double>(y));
    EXPECT_EQ(beat->content->width, static_cast<double>(width));
    EXPECT_EQ(beat->content->height, static_cast<double>(height));
  } else {
    EXPECT_EQ(beat->content, std::nullopt);
  }
}
FUZZ_TEST(ProtocolFuzz, BeatRoundTrips)
    .WithDomains(fuzztest::Arbitrary<bool>(),
                 fuzztest::InRange<std::int64_t>(1, 10000),
                 fuzztest::Arbitrary<bool>(),
                 fuzztest::InRange<std::int64_t>(-1000000, 1000000),
                 fuzztest::InRange<std::int64_t>(-1000000, 1000000),
                 fuzztest::InRange<std::int64_t>(1, 1000000),
                 fuzztest::InRange<std::int64_t>(1, 1000000));

}  // namespace
}  // namespace facepalm::protocol
