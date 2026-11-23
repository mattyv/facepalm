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

// A person's own reads (#35, item 4 of #61; gaze added by #143; blink
// rate, a long blink and widened eyes added by #145): how many times they
// picked each feeling for each evidence -- the one kind of reaction made
// at a beat, the beat's kind, and the four context fields during it.
// Pure computation; src/feeling_store.h keeps it.
//
// The rule, kept simple so it can be explained: for one evidence,
// including its own gaze, blink rate, long blink and widened eyes, the
// person's read is the feeling they picked most, once it is at least
// kLeastPicks picks and more than half of them. It replaces the generic
// read only when a held-out check shows it beats the generic rule: each
// pick in turn is held out, the person's read is worked out from the
// others (the generic read when there is none), and the person's reads
// must be right more often than the generic read is. Below kLeastPicks
// picks for one evidence's own exact bucket (every one of the four
// context fields matching), Read instead runs that same rule over every
// pick of the same sign and beat, whatever the other three fields were
// made under (#143, #145) -- so a feeling no generic rule gives
// (engagement, annoyance) can still come from the person's own labels
// before enough of them share one exact bucket, rather than starting a
// fresh count from nothing each time any of the three varies.
class Tuning {
 public:
  // The fewest picks a feeling needs before it can be the person's read,
  // and the fewest an evidence's own exact bucket needs before Read
  // trusts it alone rather than falling back to every bucket of that
  // sign and beat (#143, #145).
  static constexpr std::int64_t kLeastPicks = 2;
  // A feeling's count stops here, so the file's numbers stay small.
  static constexpr std::int64_t kMostPicks = 1000000;

  // Counts one pick of `feeling` for `evidence`, every context field and
  // all.
  void Count(const Evidence& evidence, Feeling feeling);
  // The read for `evidence`: the person's own, where it beats `generic`,
  // the generic rule's read; otherwise `generic`. Below kLeastPicks picks
  // for `evidence`'s own exact bucket, reads every pick of its sign and
  // beat instead, whatever the other three context fields were (#143,
  // #145, the class comment above).
  [[nodiscard]] Feeling Read(const Evidence& evidence, Feeling generic) const;

  // The counts as text: a "facepalm feelings 3" line, then one
  // "<sign> <beat> <gaze> <blink_rate> <long_blink> <widened_eyes>
  // <feeling> <count>" line for each feeling picked, each context field
  // in its own wording (src/screen_context.h's BeatGazeName, #143;
  // src/eye_signs.h's Name(BlinkRate), Name(LongBlink) and
  // Name(WidenedEyes), #145). Always v3, even when Parse read a v1 or v2
  // file: the next save upgrades it.
  [[nodiscard]] std::string Text() const;
  // The counts `text` holds, read as v3 or, migrated, as v2 or v1 (#143,
  // #145: a pre-#143 (v1) file has no gaze word, and a pre-#145 (v2) file
  // has no blink-rate, long-blink or widened-eyes word, so each of a v1
  // file's picks is read with all four context fields unknown, and each
  // of a v2 file's with the three new ones unknown and gaze kept -- the
  // gaze, or the three, they would have carried had that context always
  // been read -- unlike a calibration profile (#137's own version
  // bump), refusing either outright would throw away the person's own
  // picks for a file they never asked to change. nullopt when it is none
  // of the three.
  static std::optional<Tuning> Parse(std::string_view text);

 private:
  using Picks = std::array<std::int64_t, kFeelings>;
  std::map<Evidence, Picks> picks_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_TUNING_H_
