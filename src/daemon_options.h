#ifndef FACEPALM_SRC_DAEMON_OPTIONS_H_
#define FACEPALM_SRC_DAEMON_OPTIONS_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "src/user_paths.h"

namespace facepalm {

// What facepalm-daemon was asked to do (#37):
//
//   facepalm-daemon --consent --model PATH [--socket PATH]
//       [--clients DIRECTORY] [--config PATH] [--feelings-file PATH]
//   facepalm-daemon approve NAME [--clients DIRECTORY]
//   facepalm-daemon revoke NAME [--clients DIRECTORY]
//
// The socket and the clients directory default to the person's UserPaths.
struct DaemonOptions {
  enum class Command : std::uint8_t { kRun, kApprove, kRevoke };
  Command command = Command::kRun;
  std::string client;  // The client to approve or revoke.
  bool consent = false;
  std::string socket;
  std::string model;
  std::string clients;
  std::string config;         // Empty for the defaults.
  std::string feelings_file;  // Empty for the default (DefaultFeelingsPath).
};

inline constexpr std::string_view kDaemonUsage =
    "usage: facepalm-daemon --consent --model PATH [--socket PATH]\n"
    "         [--clients DIRECTORY] [--config PATH] [--feelings-file PATH]\n"
    "       facepalm-daemon approve NAME [--clients DIRECTORY]\n"
    "       facepalm-daemon revoke NAME [--clients DIRECTORY]\n";

// The options `arguments` give, after the program's name, or nullopt when
// they are not one of kDaemonUsage's forms. `defaults` are the person's
// paths, when the daemon knows their home.
std::optional<DaemonOptions> ParseDaemonOptions(
    std::span<const char* const> arguments,
    const std::optional<UserPaths>& defaults);

}  // namespace facepalm

#endif  // FACEPALM_SRC_DAEMON_OPTIONS_H_
