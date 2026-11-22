// A thin wrapper around src/calibrate_cli.h's tested logic, following
// tools/gesture_replay/main.cc's pattern: this file is not unit tested
// itself, only the real POSIX socket it opens
// (tools/posix_calibrate_client_io.h, shared with tools/menu/main.mm, #118).
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "src/calibrate_cli.h"
#include "src/client_token.h"
#include "src/user_paths.h"
#include "tools/posix_calibrate_client_io.h"

// NOLINTNEXTLINE(misc-const-correctness,bugprone-exception-escape)
int main(int argc, char** argv) {
  const std::vector<std::string> arguments(argv + 1, argv + argc);
  const char* home = std::getenv("HOME");
  const std::optional<facepalm::UserPaths> paths =
      home == nullptr ? std::nullopt
                      : facepalm::PathsFor(home, facepalm::kThisPlatform);
  if (!paths) {
    std::cerr << "cannot find the per-user facepalm directory ($HOME)\n";
    return 1;
  }
  const std::string token =
      facepalm::LoadClientToken(facepalm::ClientTokenPath(*paths, "calibrate"))
          .value_or(std::string());
  facepalm::PosixCalibrateClientIo io;
  return facepalm::RunCalibrateCli(arguments, paths->socket, token, io,
                                   std::cout, std::cerr);
}
