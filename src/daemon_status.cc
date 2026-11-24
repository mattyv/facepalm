#include "src/daemon_status.h"

#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

namespace facepalm {
namespace {
constexpr std::string_view kOn = "on";
constexpr std::string_view kOff = "off";

// specs/CameraStatus.tla's GuardReadChecksAlive: whether `pid` names a
// process still alive, standing for the daemon that last published "on"
// still being the one running. kill(pid, 0) sends no signal, only asks:
// 0 means it exists; ESRCH means no such process (#184's stale "on");
// EPERM means one exists but is owned by someone else, which still
// counts as alive.
bool ProcessAlive(std::int64_t pid) {
  if (pid <= 0) {
    return false;
  }
  if (::kill(static_cast<pid_t>(pid), 0) == 0) {
    return true;
  }
  return errno == EPERM;
}
}  // namespace

bool PublishCameraStatus(const std::string& path, bool on, std::string& error) {
  const std::string temporary = path + ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << (on ? kOn : kOff) << '\n' << ::getpid() << '\n';
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
  std::string state_line;
  if (!file.is_open() || !std::getline(file, state_line) ||
      state_line != "on") {
    return false;
  }
  std::string pid_line;
  if (!std::getline(file, pid_line)) {
    return false;
  }
  std::int64_t pid = 0;
  const char* const begin = pid_line.data();
  const char* const end = begin + pid_line.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, pid);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return false;
  }
  return ProcessAlive(pid);
}

}  // namespace facepalm
