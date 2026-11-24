#include "src/daemon_status.h"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <limits>
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
  if (pid <= 0 || pid > std::numeric_limits<pid_t>::max()) {
    return false;
  }
  if (::kill(static_cast<pid_t>(pid), 0) == 0) {
    return true;
  }
  return errno == EPERM;
}
constexpr std::size_t kMaxDetail = 256;
constexpr std::size_t kMaxRecord = 512;
constexpr std::string_view kVersion = "health-v1";
constexpr std::size_t kFieldCount = 6;
constexpr std::size_t kDetailField = 5;

bool ParseInteger(std::string_view text, std::int64_t& value) {
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::string SafeDetail(std::string_view input) {
  std::string result;
  for (unsigned char const byte : input) {
    if (result.size() == kMaxDetail) {
      break;
    }
    result.push_back(byte >= ' ' && byte <= '~' ? static_cast<char>(byte)
                                                : ' ');
  }
  return result;
}

bool WriteRecord(const std::string& path, std::string_view record,
                 std::string& error) {
  const std::string temporary = path + ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << record;
    file.flush();
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

}  // namespace

bool PublishCameraStatus(const std::string& path, bool on, std::string& error) {
  const std::string record =
      std::string(on ? kOn : kOff) + "\n" + std::to_string(::getpid()) + "\n";
  return WriteRecord(path, record, error);
}

bool PublishDaemonHealth(const std::string& path, bool camera_on,
                         ServicePhase phase, ServiceReason reason,
                         std::string_view detail, std::string& error) {
  const auto phase_value = static_cast<unsigned int>(phase);
  const auto reason_value = static_cast<unsigned int>(reason);
  if (phase_value > static_cast<unsigned int>(ServicePhase::kFailed) ||
      reason_value > static_cast<unsigned int>(ServiceReason::kModelFailure)) {
    error = "invalid health state";
    return false;
  }
  const std::string record =
      std::string(camera_on ? kOn : kOff) + "\n" + std::to_string(::getpid()) +
      "\n" + std::string(kVersion) + "\n" + std::to_string(phase_value) + "\n" +
      std::to_string(reason_value) + "\n" + SafeDetail(detail) + "\n";
  return WriteRecord(path, record, error);
}

std::optional<DaemonHealth> ReadDaemonHealth(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }
  std::array<char, kMaxRecord + 1> buffer{};
  file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  const auto size = static_cast<std::size_t>(file.gcount());
  if (size > kMaxRecord || file.bad()) {
    return std::nullopt;
  }
  std::string_view record(buffer.data(), size);
  std::array<std::string_view, kFieldCount> fields{};
  for (auto& field : fields) {
    const auto end = record.find('\n');
    if (end == std::string_view::npos) {
      return std::nullopt;
    }
    field = record.substr(0, end);
    record.remove_prefix(end + 1);
  }
  if (!record.empty() || (fields[0] != kOn && fields[0] != kOff) ||
      fields[2] != kVersion || fields[kDetailField].size() > kMaxDetail ||
      SafeDetail(fields[kDetailField]) != fields[kDetailField]) {
    return std::nullopt;
  }
  std::int64_t pid = 0;
  std::int64_t phase = 0;
  std::int64_t reason = 0;
  if (!ParseInteger(fields[1], pid) || pid <= 0 ||
      pid > std::numeric_limits<pid_t>::max() ||
      !ParseInteger(fields[3], phase) || phase < 0 ||
      phase > static_cast<std::int64_t>(ServicePhase::kFailed) ||
      !ParseInteger(fields[4], reason) || reason < 0 ||
      reason > static_cast<std::int64_t>(ServiceReason::kModelFailure)) {
    return std::nullopt;
  }
  return DaemonHealth{.phase = static_cast<ServicePhase>(phase),
                      .reason = static_cast<ServiceReason>(reason),
                      .detail = std::string(fields[kDetailField]),
                      .pid = pid,
                      .alive = ProcessAlive(pid)};
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
