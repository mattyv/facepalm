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

// A person's own reads (#35, item 4 of #61; gaze added by #143): how many
// times they picked each feeling for each evidence -- the one kind of
// reaction made at a beat, the beat's kind, and where they were looking
// during it. Pure computation; src/feeling_store.h keeps it.
//
// The rule, kept simple so it can be explained: for one evidence,
// including its own gaze, the person's read is the feeling they picked
// most, once it is at least kLeastPicks picks and more than half of them.
// It replaces the generic read only when a held-out check shows it beats
// the generic rule: each pick in turn is held out, the person's read is
// worked out from the others (the generic read when there is none), and
// the person's reads must be right more often than the generic read is.
// Below kLeastPicks picks for one evidence's own gaze, Read instead runs
// that same rule over every pick of the same sign and beat, whatever gaze
// they were made under (#143) -- so a feeling no generic rule gives
// (engagement, annoyance) can still come from the person's own labels
// before enough of them share one gaze, rather than starting a fresh count
// from nothing each time the gaze differs.
class Tuning {
 public:
  // The fewest picks a feeling needs before it can be the person's read,
  // and the fewest an evidence's own gaze needs before Read trusts it
  // alone rather than falling back to every gaze of that evidence (#143).
  static constexpr std::int64_t kLeastPicks = 2;
  // A feeling's count stops here, so the file's numbers stay small.
  static constexpr std::int64_t kMostPicks = 1000000;

  // Counts one pick of `feeling` for `evidence`, gaze and all.
  void Count(const Evidence& evidence, Feeling feeling);
  // The read for `evidence`: the person's own, where it beats `generic`,
  // the generic rule's read; otherwise `generic`. Below kLeastPicks picks
  // for `evidence`'s own gaze, reads every pick of its sign and beat
  // instead, regardless of gaze (#143, the class comment above).
  [[nodiscard]] Feeling Read(const Evidence& evidence, Feeling generic) const;

  // The counts as text: a "facepalm feelings 2" line, then one
  // "<sign> <beat> <gaze> <feeling> <count>" line for each feeling picked,
  // `gaze` in the beat's own wording (src/screen_context.h's
  // BeatGazeName, #143). Bumped from 1 to 2 for the added `gaze` word: a
  // file a pre-#143 daemon saved has none, so Parse simply refuses it,
  // the same "old data is simply not recognized" the calibration
  // profile's own version bump takes (src/calibration_store.cc), with no
  // migration needed -- the person's picks start over.
  [[nodiscard]] std::string Text() const;
  // The counts `text` holds, or nullopt when it is not what Text writes.
  static std::optional<Tuning> Parse(std::string_view text);

 private:
  using Picks = std::array<std::int64_t, kFeelings>;
  std::map<Evidence, Picks> picks_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_TUNING_H_
