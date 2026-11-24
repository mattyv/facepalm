#ifndef FACEPALM_SRC_SETTINGS_SAVE_GUARD_H_
#define FACEPALM_SRC_SETTINGS_SAVE_GUARD_H_

#include <cstddef>
#include <string_view>

namespace facepalm {

// Settings windows refuse a file past this size outright, rather than
// trying to parse it (#64): tools/menu/main.mm's ReadSettingsFile and
// main_linux.cc's ReadConfig each held this same limit by hand, against
// NSData's and QByteArray's own length. Comfortably past any
// facepalm.conf a person would hand-edit.
inline constexpr std::size_t kMaxSettingsFileBytes = 1024UL * 1024;

// Whether `byte_count`, already read from the settings file, is too
// large to treat as the settings document.
bool SettingsFileTooLarge(std::size_t byte_count);

// Whether the settings file this window loaded (`loaded_bytes`,
// `loaded_missing`) still matches what was just read fresh from disk
// (`current_bytes`, `current_missing`) (#64): false means it changed
// underneath this window while open, so main.mm's SaveSettingsFile and
// main_linux.cc's Save must refuse to overwrite a change they never saw,
// rather than clobber it.
bool SettingsFileUnchanged(std::string_view loaded_bytes, bool loaded_missing,
                           std::string_view current_bytes,
                           bool current_missing);

}  // namespace facepalm

#endif  // FACEPALM_SRC_SETTINGS_SAVE_GUARD_H_
