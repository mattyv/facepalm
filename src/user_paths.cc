#include "src/user_paths.h"

#include <optional>
#include <string>
#include <string_view>

namespace facepalm {

std::optional<UserPaths> PathsFor(std::string_view home, Platform platform) {
  if (!home.starts_with('/')) {
    return std::nullopt;
  }
  while (home.ends_with('/')) {
    home.remove_suffix(1);
  }
  const std::string directory =
      std::string(home) + (platform == Platform::kMacOs
                               ? "/Library/Application Support/Facepalm"
                               : "/.local/state/facepalm");
  return UserPaths{.directory = directory,
                   .socket = directory + "/service.sock",
                   .clients = directory + "/clients",
                   .calibration = directory + "/calibration",
                   .preview_request = directory + "/preview_request",
                   .preview_frame = directory + "/preview_frame.jpg"};
}

std::string ClientTokenPath(const UserPaths& paths, std::string_view name) {
  return paths.clients + "/" + std::string(name);
}

}  // namespace facepalm
