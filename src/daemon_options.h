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
//       [--status PATH]
//   facepalm-daemon approve NAME [--clients DIRECTORY]
//   facepalm-daemon revoke NAME [--clients DIRECTORY]
//   facepalm-daemon cameras
//
// The socket, the clients directory and the status file default to the
// person's UserPaths. `cameras` (item 10 of #61, #13) takes no options: it
// lists the cameras available right now, one a line (id, name, built in,
// tab-separated), needing no running daemon or approval, so Settings can
// offer a picker whether or not Facepalm is on. An approved client asks
// the same of a running daemon with the protocol's `cameras` line.
struct DaemonOptions {
  enum class Command : std::uint8_t { kRun, kApprove, kRevoke, kCameras };
  Command command = Command::kRun;
  std::string client;  // The client to approve or revoke.
  bool consent = false;
  std::string socket;
  std::string model;
  std::string clients;
  std::string config;         // Empty for the defaults.
  std::string feelings_file;  // Empty for the default (DefaultFeelingsPath).
  // The camera-in-use file (#37 item 8); empty to run without publishing
  // one, which running needs no other default to allow.
  std::string status;
};

inline constexpr std::string_view kDaemonUsage =
    "usage: facepalm-daemon --consent --model PATH [--socket PATH]\n"
    "         [--clients DIRECTORY] [--config PATH] [--feelings-file PATH]\n"
    "         [--status PATH]\n"
    "       facepalm-daemon approve NAME [--clients DIRECTORY]\n"
    "       facepalm-daemon revoke NAME [--clients DIRECTORY]\n"
    "       facepalm-daemon cameras\n";

// The options `arguments` give, after the program's name, or nullopt when
// they are not one of kDaemonUsage's forms. `defaults` are the person's
// paths, when the daemon knows their home.
std::optional<DaemonOptions> ParseDaemonOptions(
    std::span<const char* const> arguments,
    const std::optional<UserPaths>& defaults);

}  // namespace facepalm

#endif  // FACEPALM_SRC_DAEMON_OPTIONS_H_
