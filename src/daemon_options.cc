#include "src/daemon_options.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "src/user_paths.h"

namespace facepalm {
namespace {

// The option's field, or nullptr for an option the daemon does not take.
std::string* Field(DaemonOptions& options, std::string_view option) {
  if (option == "--socket") {
    return &options.socket;
  }
  if (option == "--model") {
    return &options.model;
  }
  if (option == "--clients") {
    return &options.clients;
  }
  if (option == "--config") {
    return &options.config;
  }
  if (option == "--ratings-file") {
    return &options.ratings_file;
  }
  return nullptr;
}

// Reads approve NAME or revoke NAME at the front of `arguments` into
// `options`, and returns how many arguments it took, or nullopt when the
// command has no name.
std::optional<std::size_t> ReadCommand(std::span<const char* const> arguments,
                                       DaemonOptions& options) {
  if (arguments.empty()) {
    return 0;
  }
  const std::string_view first(arguments.front());
  if (first != "approve" && first != "revoke") {
    return 0;
  }
  if (arguments.size() < 2 || *arguments[1] == '\0') {
    return std::nullopt;
  }
  options.command = first == "approve" ? DaemonOptions::Command::kApprove
                                       : DaemonOptions::Command::kRevoke;
  options.client = arguments[1];
  return 2;
}

// Reads the options from `next` on; false on one the command does not take.
bool ReadOptions(std::span<const char* const> arguments, std::size_t next,
                 DaemonOptions& options) {
  const bool running = options.command == DaemonOptions::Command::kRun;
  for (; next < arguments.size(); ++next) {
    const std::string_view option(arguments[next]);
    if (option == "--consent" && running && !options.consent) {
      options.consent = true;
      continue;
    }
    std::string* const value = Field(options, option);
    if (value == nullptr || !value->empty() || next + 1 >= arguments.size() ||
        *arguments[next + 1] == '\0' || (!running && option != "--clients")) {
      return false;
    }
    *value = arguments[++next];
  }
  return true;
}

}  // namespace

std::optional<DaemonOptions> ParseDaemonOptions(
    std::span<const char* const> arguments,
    const std::optional<UserPaths>& defaults) {
  DaemonOptions options;
  const std::optional<std::size_t> next = ReadCommand(arguments, options);
  if (!next || !ReadOptions(arguments, *next, options)) {
    return std::nullopt;
  }
  const bool running = options.command == DaemonOptions::Command::kRun;
  if (defaults) {
    if (options.socket.empty() && running) {
      options.socket = defaults->socket;
    }
    if (options.clients.empty()) {
      options.clients = defaults->clients;
    }
  }
  if (options.clients.empty() ||
      (running &&
       (!options.consent || options.model.empty() || options.socket.empty()))) {
    return std::nullopt;
  }
  return options;
}

}  // namespace facepalm
