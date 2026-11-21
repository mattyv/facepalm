#include "src/protocol.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/feed.h"

namespace facepalm::protocol {
namespace {

constexpr std::size_t kLongestName = 64;
constexpr std::int64_t kLongestWaitMs = 60000;

// The words of `line`, split at each space.
std::vector<std::string_view> Words(std::string_view line) {
  std::vector<std::string_view> words;
  std::size_t start = 0;
  for (std::size_t space = line.find(' '); space != std::string_view::npos;
       space = line.find(' ', start)) {
    words.push_back(line.substr(start, space - start));
    start = space + 1;
  }
  words.push_back(line.substr(start));
  return words;
}

// `word` as a whole number, or nullopt.
std::optional<std::int64_t> Number(std::string_view word) {
  std::int64_t value = 0;
  const auto [end, failure] =
      std::from_chars(word.data(), word.data() + word.size(), value);
  if (failure != std::errc() || end != word.data() + word.size()) {
    return std::nullopt;
  }
  return value;
}

bool IsName(std::string_view word) {
  if (word.empty() || word.size() > kLongestName) {
    return false;
  }
  return std::ranges::all_of(word, [](char c) {
    const bool letter_or_digit = (c >= 'a' && c <= 'z') ||
                                 (c >= 'A' && c <= 'Z') ||
                                 (c >= '0' && c <= '9');
    return letter_or_digit || c == '.' || c == '_' || c == '-';
  });
}

}  // namespace

std::string Quoted(std::string_view text) {
  std::string quoted = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      quoted += '\\';
      quoted += c;
    } else if (c == '\n') {
      quoted += "\\n";
    } else if (static_cast<unsigned char>(c) < ' ') {
      constexpr std::size_t kEscapeLength = sizeof("\\u0000");
      std::array<char, kEscapeLength> escape{};
      std::snprintf(escape.data(), escape.size(), "\\u%04x",
                    static_cast<unsigned>(c));
      quoted += escape.data();
    } else {
      quoted += c;
    }
  }
  return quoted + "\"";
}

std::optional<Request> Parse(std::string_view line, std::string& error) {
  if (line.ends_with('\r')) {
    line.remove_suffix(1);
  }
  const std::vector<std::string_view> words = Words(line);
  const std::string_view verb = words.front();
  if (verb == "hello") {
    constexpr std::size_t kWithToken = 3;
    if (words.size() < 2 || words.size() > kWithToken || !IsName(words[1]) ||
        (words.size() == kWithToken && !IsName(words[2]))) {
      error = "a name is 1 to 64 of A-Z a-z 0-9 . _ -";
      return std::nullopt;
    }
    return Hello{.name = std::string(words[1]),
                 .token = words.size() == kWithToken ? std::string(words[2])
                                                     : std::string()};
  }
  if (verb == "read" && words.size() == 1) {
    return ReadRequest{};
  }
  if (verb == "wait") {
    const std::optional<std::int64_t> ms =
        words.size() == 2 ? Number(words[1]) : std::nullopt;
    if (!ms || *ms < 0 || *ms > kLongestWaitMs) {
      error = "wait takes 0 to 60000 milliseconds";
      return std::nullopt;
    }
    return WaitRequest{.timeout = std::chrono::milliseconds(*ms)};
  }
  if (verb == "rate") {
    const std::optional<std::int64_t> id =
        words.size() == 3 ? Number(words[1]) : std::nullopt;
    const std::optional<std::int64_t> score =
        words.size() == 3 ? Number(words[2]) : std::nullopt;
    if (!id || !score || !std::in_range<int>(*score)) {
      error = "rate takes a reading id and a score";
      return std::nullopt;
    }
    // The feed refuses a score outside 1 to 5; this only keeps it an int.
    return RateRequest{.id = *id, .score = static_cast<int>(*score)};
  }
  error = "unknown request";
  return std::nullopt;
}

std::string DeliveryJson(const Feed::Delivery& delivery) {
  std::string json = R"({"readings":[)";
  for (const Feed::Reading& reading : delivery.readings) {
    if (json.back() != '[') {
      json += ',';
    }
    json += R"({"id":)" + std::to_string(reading.id) + R"(,"summary":)" +
            Quoted(reading.summary) + R"(,"detail":)" + reading.detail;
    if (reading.published_at_ms) {
      json +=
          R"(,"published_at_ms":)" + std::to_string(*reading.published_at_ms);
    }
    json += '}';
  }
  return json + R"(],"missed":)" + std::to_string(delivery.missed) + "}";
}

std::string ErrorJson(std::string_view message) {
  return R"({"error":)" + Quoted(message) + "}";
}

}  // namespace facepalm::protocol
