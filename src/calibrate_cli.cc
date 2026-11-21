#include "src/calibrate_cli.h"

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace facepalm {
namespace {
// The client name this tool shows the service; approve it with
// `facepalm-daemon approve calibrate` (src/client_approvals.h).
constexpr std::string_view kClientName = "calibrate";
// protocol::ErrorJson (src/protocol.cc) always starts a reply this way.
constexpr std::string_view kErrorPrefix = R"({"error":)";
constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

std::string Join(const std::vector<std::string>& words) {
  std::string joined;
  for (const std::string& word : words) {
    if (!joined.empty()) {
      joined += ' ';
    }
    joined += word;
  }
  return joined;
}
}  // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters): see the header.
int RunCalibrateCli(const std::vector<std::string>& arguments,
                    const std::string& socket_path, const std::string& token,
                    CalibrateClientIo& io, std::ostream& out,
                    std::ostream& err) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  if (arguments.empty()) {
    err << "usage: calibrate_cli start [SCREENS]|next|cancel\n";
    return kExitUsage;
  }
  const std::string command = "calibrate " + Join(arguments);
  if (!io.Connect(socket_path)) {
    err << "cannot connect to " << socket_path << "\n";
    return kExitError;
  }
  std::string hello_line = "hello " + std::string(kClientName);
  if (!token.empty()) {
    hello_line += " " + token;
  }
  io.WriteLine(hello_line);
  const std::optional<std::string> hello = io.ReadLine();
  if (!hello || hello->starts_with(kErrorPrefix)) {
    err << "daemon refused the connection" << (hello ? ": " + *hello : "")
        << "\n";
    return kExitError;
  }
  io.WriteLine(command);
  const std::optional<std::string> reply = io.ReadLine();
  if (!reply) {
    err << "connection closed before a reply arrived\n";
    return kExitError;
  }
  out << *reply << "\n";
  return reply->starts_with(kErrorPrefix) ? kExitError : kExitOk;
}

}  // namespace facepalm
