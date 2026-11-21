#ifndef FACEPALM_SRC_TUNING_H_
#define FACEPALM_SRC_TUNING_H_

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "src/feelings.h"

namespace facepalm {

// A person's own reads (#35, item 4 of #61): how many times they picked
// each feeling for each evidence, the one kind of reaction made at a beat
// and the beat's kind. Pure computation; src/feeling_store.h keeps it.
//
// The rule, kept simple so it can be explained: for one evidence, the
// person's read is the feeling they picked most, once it is at least
// kLeastPicks picks and more than half of them. It replaces the generic
// read only when a held-out check shows it beats the generic rule: each
// pick in turn is held out, the person's read is worked out from the
// others (the generic read when there is none), and the person's reads
// must be right more often than the generic read is.
class Tuning {
 public:
  // The fewest picks a feeling needs before it can be the person's read.
  static constexpr std::int64_t kLeastPicks = 2;
  // A feeling's count stops here, so the file's numbers stay small.
  static constexpr std::int64_t kMostPicks = 1000000;

  // Counts one pick of `feeling` for `evidence`.
  void Count(const Evidence& evidence, Feeling feeling);
  // The read for `evidence`: the person's own, where it beats `generic`,
  // the generic rule's read; otherwise `generic`.
  [[nodiscard]] Feeling Read(const Evidence& evidence, Feeling generic) const;

  // The counts as text: a "facepalm feelings 1" line, then one
  // "<sign> <beat> <feeling> <count>" line for each feeling picked.
  [[nodiscard]] std::string Text() const;
  // The counts `text` holds, or nullopt when it is not what Text writes.
  static std::optional<Tuning> Parse(std::string_view text);

 private:
  using Picks = std::array<std::int64_t, kFeelings>;
  std::map<Evidence, Picks> picks_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_TUNING_H_
