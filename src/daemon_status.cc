#include "src/daemon_status.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

namespace facepalm {
namespace {
constexpr std::string_view kOn = "on\n";
constexpr std::string_view kOff = "off\n";
}  // namespace

bool PublishCameraStatus(const std::string& path, bool on, std::string& error) {
  const std::string temporary = path + ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << (on ? kOn : kOff);
    if (!file.good()) {
      error = "cannot write " + temporary + ": " + std::strerror(errno);
      return false;
    }
  }
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    error = "cannot replace " + path + ": " + std::strerror(errno);
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

bool ReadCameraStatus(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::string first_line;
  if (!file.is_open() || !std::getline(file, first_line)) {
    return false;
  }
  return first_line == "on";
}

}  // namespace facepalm
