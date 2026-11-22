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

}  // namespace
}  // namespace facepalm::protocol
