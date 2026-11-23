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

#include "src/eye_signs.h"
#include "src/feelings.h"
#include "src/screen_context.h"

namespace facepalm {
namespace {

// v1 (pre-#143) had no gaze: "<sign> <beat> <feeling> <count>". v2
// (#143) adds it as a third word: "<sign> <beat> <gaze> <feeling>
// <count>". v3 (#145) adds blink rate, a long blink and widened eyes as
// three more: "<sign> <beat> <gaze> <blink_rate> <long_blink>
// <widened_eyes> <feeling> <count>". A v1 or v2 file is read, not
// refused: refusing it would throw away the person's own picks for a
// file they never asked to change, unlike a calibration profile, which
// the person simply redoes (#137's own version bump) -- every v1 pick is
// read with all four context fields unknown, and every v2 pick with the
// three new ones unknown and gaze kept, the same values they would have
// carried had that context always been read; Text always writes v3, so
// the next save upgrades the file in place.
constexpr std::string_view kHeaderV1 = "facepalm feelings 1\n";
constexpr std::string_view kHeaderV2 = "facepalm feelings 2\n";
constexpr std::string_view kHeaderV3 = "facepalm feelings 3\n";
constexpr std::size_t kWordsPerLineV1 = 4;
constexpr std::size_t kWordsPerLineV2 = 5;
constexpr std::size_t kWordsPerLineV3 = 8;

// The file format a line is read as: v1 and v2 are migrated in (#143,
// #145's own class comment and Tuning::Parse's own doc comment); Text
// always writes v3.
enum class Version : std::uint8_t { kV1, kV2, kV3 };

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
// fallback for an evidence's own exact bucket below the minimum (#143,
// #145).
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

// One line's own evidence, feeling and count.
struct ParsedLine {
  Evidence evidence;
  Feeling feeling;
  std::int64_t count;
};

// The four context fields Evidence carries beyond sign and beat, read
// apart so a missing or malformed one never leaves Evidence's own beat
// field touched (Context{} defaults every field here to unknown, unlike
// BeatKind, which has no unknown value of its own).
struct Context {
  Gaze gaze = Gaze::kUnknown;
  BlinkRate blink_rate = BlinkRate::kUnknown;
  LongBlink long_blink = LongBlink::kUnknown;
  WidenedEyes widened_eyes = WidenedEyes::kUnknown;
};

// `words`' own gaze, blink rate, long blink and widened eyes, starting at
// `words[2]` (v2 and v3 both keep gaze there): v1 leaves every one
// unknown; v2 leaves the three new ones unknown; v3 reads all four.
// nullopt when one of the words present for `version` does not name its
// own field.
std::optional<Context> ContextOf(const std::vector<std::string_view>& words,
                                 Version version) {
  Context context;
  if (version == Version::kV1) {
    return context;
  }
  const std::optional<Gaze> gaze = BeatGazeNamed(words[2]);
  if (!gaze) {
    return std::nullopt;
  }
  context.gaze = *gaze;
  if (version == Version::kV2) {
    return context;
  }
  const std::optional<BlinkRate> blink_rate = BlinkRateNamed(words[3]);
  const std::optional<LongBlink> long_blink = LongBlinkNamed(words[4]);
  const std::optional<WidenedEyes> widened_eyes = WidenedEyesNamed(words[5]);
  if (!blink_rate || !long_blink || !widened_eyes) {
    return std::nullopt;
  }
  context.blink_rate = *blink_rate;
  context.long_blink = *long_blink;
  context.widened_eyes = *widened_eyes;
  return context;
}

// `words` as one line, at `version`'s own word count and layout. nullopt
// when malformed.
std::optional<ParsedLine> LineOf(const std::vector<std::string_view>& words,
                                 Version version) {
  if (!IsSign(words[0])) {
    return std::nullopt;
  }
  const std::optional<BeatKind> beat = BeatKindNamed(words[1]);
  const std::optional<Context> context = ContextOf(words, version);
  if (!beat || !context) {
    return std::nullopt;
  }
  // The feeling word's own index: right after sign and beat for v1, after
  // gaze too for v2, and after the three eye-sign words too for v3.
  constexpr std::size_t kFeelingIndexV1 = 2;
  constexpr std::size_t kFeelingIndexV2 = 3;
  constexpr std::size_t kFeelingIndexV3 = 6;
  std::size_t next = kFeelingIndexV1;
  if (version == Version::kV2) {
    next = kFeelingIndexV2;
  } else if (version == Version::kV3) {
    next = kFeelingIndexV3;
  }
  const std::optional<Feeling> feeling = FeelingNamed(words[next]);
  const std::optional<std::int64_t> count = CountOf(words[next + 1]);
  if (!feeling || !count) {
    return std::nullopt;
  }
  return ParsedLine{.evidence = Evidence{.sign = std::string(words[0]),
                                         .beat = *beat,
                                         .gaze = context->gaze,
                                         .blink_rate = context->blink_rate,
                                         .long_blink = context->long_blink,
                                         .widened_eyes = context->widened_eyes},
                    .feeling = *feeling,
                    .count = *count};
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
  // Below the minimum for this evidence's own exact bucket (#143, #145):
  // every pick of the same sign and beat, whatever the other three
  // context fields were made under, including the bucket just checked
  // above -- the class comment's own fallback, so a feeling no generic
  // rule gives (engagement, annoyance) can still come from the person's
  // labels before enough of them share one exact bucket.
  Picks pooled{};
  for (const auto& [other, picks] : picks_) {
    if (other.sign == evidence.sign && other.beat == evidence.beat) {
      for (std::size_t feeling = 0; feeling < picks.size(); ++feeling) {
        pooled[feeling] += picks[feeling];
      }
    }
  }
  return ReadFromPicks(pooled, generic);
}

std::string Tuning::Text() const {
  std::string text(kHeaderV3);
  for (const auto& [evidence, picks] : picks_) {
    for (std::size_t feeling = 0; feeling < picks.size(); ++feeling) {
      if (picks[feeling] > 0) {
        text += evidence.sign + ' ' + std::string(Name(evidence.beat)) + ' ' +
                std::string(BeatGazeName(evidence.gaze)) + ' ' +
                std::string(Name(evidence.blink_rate)) + ' ' +
                std::string(Name(evidence.long_blink)) + ' ' +
                std::string(Name(evidence.widened_eyes)) + ' ' +
                std::string(Name(static_cast<Feeling>(feeling))) + ' ' +
                std::to_string(picks[feeling]) + '\n';
      }
    }
  }
  return text;
}

std::optional<Tuning> Tuning::Parse(std::string_view text) {
  Version version = Version::kV3;
  std::size_t header_size = 0;
  std::size_t words_per_line = kWordsPerLineV3;
  if (text.starts_with(kHeaderV3)) {
    header_size = kHeaderV3.size();
  } else if (text.starts_with(kHeaderV2)) {
    header_size = kHeaderV2.size();
    version = Version::kV2;
    words_per_line = kWordsPerLineV2;
  } else if (text.starts_with(kHeaderV1)) {
    header_size = kHeaderV1.size();
    version = Version::kV1;
    words_per_line = kWordsPerLineV1;
  } else {
    return std::nullopt;
  }
  if (!text.ends_with('\n')) {
    return std::nullopt;
  }
  text.remove_prefix(header_size);
  Tuning tuning;
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    const std::vector<std::string_view> words = Words(text.substr(0, end));
    text.remove_prefix(end + 1);
    if (words.size() != words_per_line) {
      return std::nullopt;
    }
    const std::optional<ParsedLine> line = LineOf(words, version);
    if (!line) {
      return std::nullopt;
    }
    std::int64_t& picked =
        tuning.picks_[line->evidence][static_cast<std::size_t>(line->feeling)];
    if (picked != 0) {
      return std::nullopt;
    }
    picked = line->count;
  }
  return tuning;
}

}  // namespace facepalm
