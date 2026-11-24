#ifndef FACEPALM_SRC_MENU_FIELD_FORMAT_H_
#define FACEPALM_SRC_MENU_FIELD_FORMAT_H_

#include <optional>
#include <string>
#include <string_view>

namespace facepalm {

// `value`, formatted as plain decimal text (#64): tools/menu/main.mm's
// FormatFloat and main_linux.cc's Number each wrapped this same
// `std::to_chars` formatting in their own platform's string type, for a
// Settings field with no particular precision asked of it.
std::string FormatFloat(float value);

// `text` read as a whole number in [`min`, `max`], the exact text and
// nothing else (leading or trailing whitespace trimmed first); nullopt
// otherwise. tools/menu/main.mm's saveSettings: validates its
// frames-per-second field this way by hand, since NSTextField, unlike
// Linux's QSpinBox, does not clamp its own input.
std::optional<int> ParseWholeNumberInRange(std::string_view text, int min,
                                           int max);

}  // namespace facepalm

#endif  // FACEPALM_SRC_MENU_FIELD_FORMAT_H_
