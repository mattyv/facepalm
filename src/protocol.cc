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

#include "src/calibration.h"
#include "src/feed.h"
#include "src/feelings.h"

namespace facepalm::protocol {
namespace {

constexpr std::size_t kLongestName = 64;
constexpr std::int64_t kLongestWaitMs = 60000;
// A reaction to a question comes within a few seconds of it, if at all.
constexpr std::int64_t kLongestBeatMs = 10000;
constexpr std::size_t kBeatWords = 3;
constexpr std::size_t kFeelWords = 3;
constexpr std::size_t kCalibrateWords = 2;
constexpr std::size_t kCalibrateStartWithScreens = 3;
constexpr auto kCalibrateUsage =
    "calibrate takes start [screens], next or cancel";

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

// The beat request of `words`: beat, its kind, and its window.
std::optional<Request> ParseBeat(const std::vector<std::string_view>& words,
                                 std::string& error) {
  const std::optional<BeatKind> kind =
      words.size() == kBeatWords ? BeatKindNamed(words[1]) : std::nullopt;
  const std::optional<std::int64_t> ms =
      words.size() == kBeatWords ? Number(words[2]) : std::nullopt;
  if (!kind || !ms || *ms < 1 || *ms > kLongestBeatMs) {
    error = "beat takes question or proposal and 1 to 10000 milliseconds";
    return std::nullopt;
  }
  return BeatRequest{.kind = *kind, .window = std::chrono::milliseconds(*ms)};
}

// The feel request of `words`: feel, an interpretation's id, and a feeling.
std::optional<Request> ParseFeel(const std::vector<std::string_view>& words,
                                 std::string& error) {
  const std::optional<std::int64_t> id =
      words.size() == kFeelWords ? Number(words[1]) : std::nullopt;
  const std::optional<Feeling> feeling =
      words.size() == kFeelWords ? FeelingNamed(words[2]) : std::nullopt;
  if (!id || !feeling) {
    error = "feel takes an interpretation id and a feeling: ";
    for (std::size_t i = 0; i < kEveryFeeling.size(); ++i) {
      error += Name(kEveryFeeling.at(i));
      if (i + 2 < kEveryFeeling.size()) {
        error += ", ";
      } else if (i + 2 == kEveryFeeling.size()) {
        error += " or ";
      }
    }
    return std::nullopt;
  }
  return FeelRequest{.interpretation = *id, .feeling = *feeling};
}

// The calibrate request of `words`: start (with an optional screen count),
// next or cancel.
std::optional<Request> ParseCalibrate(
    const std::vector<std::string_view>& words, std::string& error) {
  using Action = CalibrateRequest::Action;
  if (words.size() == kCalibrateWords) {
    if (words[1] == "start") {
      return CalibrateRequest{.action = Action::kStart, .screens = 1};
    }
    if (words[1] == "next") {
      return CalibrateRequest{.action = Action::kNext};
    }
    if (words[1] == "cancel") {
      return CalibrateRequest{.action = Action::kCancel};
    }
  } else if (words.size() == kCalibrateStartWithScreens &&
             words[1] == "start") {
    const std::optional<std::int64_t> screens = Number(words[2]);
    if (screens && *screens >= 1 && std::in_range<int>(*screens)) {
      return CalibrateRequest{.action = Action::kStart,
                              .screens = static_cast<int>(*screens)};
    }
  }
  error = kCalibrateUsage;
  return std::nullopt;
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
  if (verb == "beat") {
    return ParseBeat(words, error);
  }
  if (verb == "feel") {
    return ParseFeel(words, error);
  }
  if (verb == "reset") {
    if (words.size() != 2 || words[1] != "feelings") {
      error = "reset takes feelings";
      return std::nullopt;
    }
    return ResetRequest{};
  }
  if (verb == "calibrate") {
    return ParseCalibrate(words, error);
  }
  if (verb == "cameras" && words.size() == 1) {
    return CamerasRequest{};
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
    if (reading.context) {
      json += R"(,"context":)" + *reading.context;
    }
    if (reading.published_at_ms) {
      json +=
          R"(,"published_at_ms":)" + std::to_string(*reading.published_at_ms);
    }
    json += '}';
  }
  json += R"(],"missed":)" + std::to_string(delivery.missed);
  if (const auto& interpretation = delivery.interpretation) {
    json += R"(,"interpretation":{"id":)" + std::to_string(interpretation->id) +
            R"(,"feeling":)" + Quoted(Name(interpretation->feeling)) +
            R"(,"beat":)" + Quoted(Name(interpretation->beat)) +
            R"(,"feelings":[)";
    for (const Feeling feeling : kEveryFeeling) {
      if (json.back() != '[') {
        json += ',';
      }
      json += Quoted(Name(feeling));
    }
    json += "]}";
  }
  return json + "}";
}

std::string ErrorJson(std::string_view message) {
  return R"({"error":)" + Quoted(message) + "}";
}

std::string CamerasJson(const std::vector<CameraDevice>& devices) {
  std::string json = R"({"ok":true,"cameras":[)";
  for (const CameraDevice& device : devices) {
    if (json.back() != '[') {
      json += ',';
    }
    json += R"({"id":)" + Quoted(device.id) + R"(,"name":)" +
            Quoted(device.name) + R"(,"built_in":)" +
            (device.built_in ? "true" : "false") + '}';
  }
  return json + "]}";
}

std::string CalibrationStatusJson(const CalibrationController::Status& status) {
  if (!status.running) {
    return status.done ? R"({"ok":true,"calibration":{"state":"done"}})"
                       : R"({"ok":true,"calibration":{"state":"idle"}})";
  }
  std::string json = R"({"ok":true,"calibration":{"state":"running","index":)" +
                     std::to_string(status.index) + R"(,"total":)" +
                     std::to_string(status.total);
  if (status.resting_step) {
    json += R"(,"step":"resting"})";
  } else {
    json += R"(,"step":"screen","screen":)" + std::to_string(status.screen) +
            R"(,"point":)" + Quoted(Name(status.point)) + "}";
  }
  return json + "}";
}

}  // namespace facepalm::protocol
