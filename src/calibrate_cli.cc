#include "src/calibrate_cli.h"

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace facepalm {
namespace {
constexpr std::string_view kClientName = "calibrate-cli";
// protocol::ErrorJson (src/protocol.cc) always starts a reply this way.
constexpr std::string_view kErrorPrefix = R"({"error":)";
constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

std::string Join(const std::vector<std::string>& words, std::size_t from) {
  std::string joined;
  for (std::size_t index = from; index < words.size(); ++index) {
    if (!joined.empty()) {
      joined += ' ';
    }
    joined += words[index];
  }
  return joined;
}
}  // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters): see the header.
int RunCalibrateCli(const std::vector<std::string>& arguments,
                    CalibrateClientIo& io, std::ostream& out,
                    std::ostream& err) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  if (arguments.size() < 2) {
    err << "usage: calibrate_cli SOCKET start [SCREENS]|next|cancel\n";
    return kExitUsage;
  }
  const std::string& socket_path = arguments[0];
  const std::string command = "calibrate " + Join(arguments, 1);
  if (!io.Connect(socket_path)) {
    err << "cannot connect to " << socket_path << "\n";
    return kExitError;
  }
  io.WriteLine(std::string("hello ") + std::string(kClientName));
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
