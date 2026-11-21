#ifndef FACEPALM_SRC_USER_PATHS_H_
#define FACEPALM_SRC_USER_PATHS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace facepalm {

enum class Platform : std::uint8_t { kMacOs, kLinux };

#ifdef __APPLE__
inline constexpr Platform kThisPlatform = Platform::kMacOs;
#else
inline constexpr Platform kThisPlatform = Platform::kLinux;
#endif

// Where facepalm keeps its per-user files (#37): fixed under the person's
// home, so the daemon, the menu and every client find them without being
// told. mcp/main.js computes the same paths.
struct UserPaths {
  // macOS: ~/Library/Application Support/Facepalm; Linux:
  // ~/.local/state/facepalm. Private to the user.
  std::string directory;
  std::string socket;   // The service's socket, in `directory`.
  std::string clients;  // One token file per approved client, in `directory`.
  // The saved calibration profile (#36, #35, item 12 of #61), in
  // `directory`, if a session has ever finished one.
  std::string calibration;
};

// The paths under `home`, or nullopt when `home` is not an absolute path.
std::optional<UserPaths> PathsFor(std::string_view home, Platform platform);

// The token file of the client called `name` (ClientApprovals::IsClientName).
std::string ClientTokenPath(const UserPaths& paths, std::string_view name);

}  // namespace facepalm

#endif  // FACEPALM_SRC_USER_PATHS_H_
