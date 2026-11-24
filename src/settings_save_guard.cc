#include "src/settings_save_guard.h"

#include <cstddef>
#include <string_view>

namespace facepalm {

bool SettingsFileTooLarge(std::size_t byte_count) {
  return byte_count > kMaxSettingsFileBytes;
}

bool SettingsFileUnchanged(std::string_view loaded_bytes, bool loaded_missing,
                           std::string_view current_bytes,
                           bool current_missing) {
  return loaded_missing == current_missing && loaded_bytes == current_bytes;
}

}  // namespace facepalm
