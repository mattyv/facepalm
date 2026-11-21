#ifndef FACEPALM_SRC_USER_SERVICE_H_
#define FACEPALM_SRC_USER_SERVICE_H_

#include <sys/types.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/user_paths.h"

namespace facepalm {

// What the user service runs: facepalm-daemon with the person's consent.
struct ServiceProgram {
  std::string daemon;  // The facepalm-daemon executable.
  std::string model;   // The face landmarker model.
  std::string config;  // The settings file, or "" for the defaults.
};

// The launchd job's label, after the app's bundle identifier, and the
// systemd unit's name.
inline constexpr std::string_view kBundleIdentifier = "com.mattyv.facepalm";
inline constexpr std::string_view kLaunchAgentLabel =
    "com.mattyv.facepalm.daemon";
inline constexpr std::string_view kSystemdUnit = "facepalm.service";

// The LaunchAgent that runs `program` (macOS): started when loaded and
// again when it fails, never when it stops cleanly; its errors go to `log`.
std::string LaunchAgentPlist(const ServiceProgram& program,
                             std::string_view log);
// The systemd --user unit that runs `program` (Linux), with the same
// restarts.
std::string SystemdUnit(const ServiceProgram& program);

// The file system and process calls the service needs, replaceable for
// failure tests.
class ServiceIo {
 public:
  virtual ~ServiceIo() = default;
  // Replaces `path` with `contents` at once, making its folder if needed.
  virtual bool WriteFile(const std::string& path,
                         std::string_view contents) = 0;
  // Removes `path`; true when it is gone, also when it was never there.
  virtual bool RemoveFile(const std::string& path) = 0;
  virtual bool Exists(const std::string& path) = 0;
  // Runs `command`, found on the PATH, with no input or output, and returns
  // its exit status, or -1 when it could not run or did not exit.
  virtual int Run(const std::vector<std::string>& command) = 0;
  virtual uid_t User() = 0;
};

// The OS's calls.
std::unique_ptr<ServiceIo> PosixServiceIo();

// The daemon and model under the first of `prefixes` that has both, as an
// install lays them out (bin/facepalm-daemon and
// share/facepalm/model/face_landmarker.task), with `config` when that file
// is there; nullopt when no prefix has them.
std::optional<ServiceProgram> FindProgram(
    const std::vector<std::string>& prefixes, const std::string& config,
    ServiceIo& io);

// The menu's Turn On and Turn Off (#37), following specs/UserService.tla:
// facepalm runs as a user service of the platform's service manager, so it
// starts again after a failure and at the next login, until turned off.
class UserService {
 public:
  // Approves the named client, as ClientApprovals::Approve does.
  using Approve =
      std::function<std::optional<std::string>(std::string_view, std::string&)>;

  UserService(Platform platform, std::string home, Approve approve,
              ServiceIo& io);

  // The service's file: the LaunchAgent or the unit.
  [[nodiscard]] std::string File() const;
  // FileMatchesMenu: facepalm is on exactly while its file is installed.
  [[nodiscard]] bool IsOn();
  // UserService!TurnOn: the person's consent. Approves the bundled MCP
  // adapter, writes the file and loads it. False, with `error` saying why,
  // when a step fails.
  bool TurnOn(const ServiceProgram& program, std::string& error);
  // UserService!TurnOff: unloads the service, which returns once the daemon
  // has exited (GuardOffWaits), and removes the file.
  bool TurnOff(std::string& error);

 private:
  // launchd's domain for the person's session, and the job's name in it.
  [[nodiscard]] std::string Domain() const;
  [[nodiscard]] std::string Job() const;
  Platform platform_;
  std::string home_;
  Approve approve_;
  ServiceIo& io_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_USER_SERVICE_H_
