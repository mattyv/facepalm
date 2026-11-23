#include "src/tuning.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/feelings.h"
#include "src/screen_context.h"

namespace facepalm {
namespace {

constexpr std::string_view kHeader = "facepalm feelings 2\n";
constexpr std::size_t kWordsPerLine = 5;

// How many picks `picks` holds in total.
std::int64_t Total(const std::array<std::int64_t, kFeelings>& picks) {
  return std::accumulate(picks.begin(), picks.end(), std::int64_t{0});
}

// The feeling that is the person's read from `picks`: at least kLeastPicks
// of them, and more than half; nullopt when none is.
std::optional<Feeling> Majority(
    const std::array<std::int64_t, kFeelings>& picks) {
  const std::int64_t total = Total(picks);
  for (std::size_t feeling = 0; feeling < picks.size(); ++feeling) {
    if (picks[feeling] >= Tuning::kLeastPicks && 2 * picks[feeling] > total) {
      return static_cast<Feeling>(feeling);
    }
  }
  return std::nullopt;
}

// The held-out read from `picks`: the person's own, where it beats
// `generic`; otherwise `generic`. Tuning::Read's own body, shared with its
// fallback for an evidence's own gaze below the minimum (#143).
Feeling ReadFromPicks(const std::array<std::int64_t, kFeelings>& picks,
                      Feeling generic) {
  // The held-out check: how many picks the person's read, from the
  // others, gets right.
  std::int64_t right = 0;
  for (std::size_t feeling = 0; feeling < picks.size(); ++feeling) {
    if (picks[feeling] == 0) {
      continue;
    }
    std::array<std::int64_t, kFeelings> others = picks;
    --others[feeling];
    if (Majority(others).value_or(generic) == static_cast<Feeling>(feeling)) {
      right += picks[feeling];
    }
  }
  if (right <= picks[static_cast<std::size_t>(generic)]) {
    return generic;
  }
  return Majority(picks).value_or(generic);
}

// The words of `line`, split at each space.
std::vector<std::string_view> Words(std::string_view line) {
  std::vector<std::string_view> words;
  for (std::size_t space = line.find(' '); space != std::string_view::npos;
       space = line.find(' ')) {
    words.push_back(line.substr(0, space));
    line.remove_prefix(space + 1);
  }
  words.push_back(line);
  return words;
}

// `word` as a count from 1 to kMostPicks, or nullopt.
std::optional<std::int64_t> CountOf(std::string_view word) {
  std::int64_t count = 0;
  const auto [end, failure] =
      std::from_chars(word.data(), word.data() + word.size(), count);
  if (failure != std::errc() || end != word.data() + word.size() || count < 1 ||
      count > Tuning::kMostPicks) {
    return std::nullopt;
  }
  return count;
}

}  // namespace

void Tuning::Count(const Evidence& evidence, Feeling feeling) {
  std::int64_t& count = picks_[evidence][static_cast<std::size_t>(feeling)];
  if (count < kMostPicks) {
    ++count;
  }
}

Feeling Tuning::Read(const Evidence& evidence, Feeling generic) const {
  const auto found = picks_.find(evidence);
  if (found != picks_.end() && Total(found->second) >= kLeastPicks) {
    return ReadFromPicks(found->second, generic);
  }
  // Below the minimum for this evidence's own gaze (#143): every pick of
  // the same sign and beat, whatever gaze it was made under, including the
  // bucket just checked above -- the class comment's own fallback, so a
  // feeling no generic rule gives (engagement, annoyance) can still come
  // from the person's labels before enough of them share one gaze.
  Picks without_gaze{};
  for (const auto& [other, picks] : picks_) {
    if (other.sign == evidence.sign && other.beat == evidence.beat) {
      for (std::size_t feeling = 0; feeling < picks.size(); ++feeling) {
        without_gaze[feeling] += picks[feeling];
      }
    }
  }
  return ReadFromPicks(without_gaze, generic);
}

std::string Tuning::Text() const {
  std::string text(kHeader);
  for (const auto& [evidence, picks] : picks_) {
    for (std::size_t feeling = 0; feeling < picks.size(); ++feeling) {
      if (picks[feeling] > 0) {
        text += evidence.sign + ' ' + std::string(Name(evidence.beat)) + ' ' +
                std::string(BeatGazeName(evidence.gaze)) + ' ' +
                std::string(Name(static_cast<Feeling>(feeling))) + ' ' +
                std::to_string(picks[feeling]) + '\n';
      }
    }
  }
  return text;
}

std::optional<Tuning> Tuning::Parse(std::string_view text) {
  if (!text.starts_with(kHeader) || !text.ends_with('\n')) {
    return std::nullopt;
  }
  text.remove_prefix(kHeader.size());
  Tuning tuning;
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    const std::vector<std::string_view> words = Words(text.substr(0, end));
    text.remove_prefix(end + 1);
    if (words.size() != kWordsPerLine || !IsSign(words[0])) {
      return std::nullopt;
    }
    const std::optional<BeatKind> beat = BeatKindNamed(words[1]);
    const std::optional<Gaze> gaze = BeatGazeNamed(words[2]);
    const std::optional<Feeling> feeling = FeelingNamed(words[3]);
    const std::optional<std::int64_t> count = CountOf(words[4]);
    if (!beat || !gaze || !feeling || !count) {
      return std::nullopt;
    }
    Picks& picks = tuning.picks_[Evidence{
        .sign = std::string(words[0]), .beat = *beat, .gaze = *gaze}];
    std::int64_t& picked = picks[static_cast<std::size_t>(*feeling)];
    if (picked != 0) {
      return std::nullopt;
    }
    picked = *count;
  }
  return tuning;
}

}  // namespace facepalm
