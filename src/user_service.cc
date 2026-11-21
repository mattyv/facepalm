#include "src/user_service.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "src/user_paths.h"

extern char** environ;  // NOLINT(readability-redundant-declaration): POSIX.

namespace facepalm {
namespace {

// The daemon's arguments, after the executable.
std::vector<std::string> Arguments(const ServiceProgram& program) {
  std::vector<std::string> arguments{"--consent", "--model", program.model};
  if (!program.config.empty()) {
    arguments.insert(arguments.end(), {"--config", program.config});
  }
  return arguments;
}

// `text` as XML character data.
std::string Xml(std::string_view text) {
  std::string escaped;
  for (const char c : text) {
    switch (c) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped += c;
    }
  }
  return escaped;
}

// `text` as one quoted word of a systemd command line, with its specifiers
// (%) and variables ($) taken literally.
std::string Quoted(std::string_view text) {
  std::string quoted = "\"";
  for (const char c : text) {
    if (c == '\\' || c == '"') {
      quoted += '\\';
    } else if (c == '%' || c == '$') {
      quoted += c;
    }
    quoted += c;
  }
  return quoted + "\"";
}

class PosixIo : public ServiceIo {
 public:
  bool WriteFile(const std::string& path, std::string_view contents) override {
    const std::filesystem::path file(path);
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const std::string written = path + ".new";
    {
      std::ofstream out(written, std::ios::binary | std::ios::trunc);
      out << contents;
      if (!out.flush()) {
        return false;
      }
    }
    std::filesystem::rename(written, file, error);
    if (error) {
      std::filesystem::remove(written, error);
      return false;
    }
    return true;
  }

  bool RemoveFile(const std::string& path) override {
    std::error_code error;
    std::filesystem::remove(path, error);
    return !error;
  }

  bool Exists(const std::string& path) override {
    std::error_code error;
    return std::filesystem::exists(path, error);
  }

  int Run(const std::vector<std::string>& command) override {
    std::vector<char*> argv;
    for (const std::string& word : command) {
      argv.push_back(const_cast<char*>(word.c_str()));  // NOLINT: POSIX argv.
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions{};
    posix_spawn_file_actions_init(&actions);
    for (const int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
      posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", O_RDWR, 0);
    }
    pid_t child = 0;
    const int spawned =
        posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) {
      return -1;
    }
    // Starts as a signal's status, so a wait that fails counts as no exit.
    int status = SIGKILL;
    waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }

  uid_t User() override { return getuid(); }
};

}  // namespace

std::string LaunchAgentPlist(const ServiceProgram& program,
                             std::string_view log) {
  std::string arguments = "    <string>" + Xml(program.daemon) + "</string>\n";
  for (const std::string& argument : Arguments(program)) {
    arguments += "    <string>" + Xml(argument) + "</string>\n";
  }
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\">\n"
         "<dict>\n"
         "  <key>Label</key>\n  <string>" +
         std::string(kLaunchAgentLabel) +
         "</string>\n"
         "  <key>AssociatedBundleIdentifiers</key>\n  <string>" +
         std::string(kBundleIdentifier) +
         "</string>\n"
         "  <key>ProgramArguments</key>\n  <array>\n" +
         arguments +
         "  </array>\n"
         "  <key>RunAtLoad</key>\n  <true/>\n"
         "  <key>KeepAlive</key>\n  <dict>\n"
         "    <key>SuccessfulExit</key>\n    <false/>\n  </dict>\n"
         "  <key>ProcessType</key>\n  <string>Interactive</string>\n"
         "  <key>StandardErrorPath</key>\n  <string>" +
         Xml(log) +
         "</string>\n"
         "</dict>\n"
         "</plist>\n";
}

std::string SystemdUnit(const ServiceProgram& program) {
  std::string command = Quoted(program.daemon);
  for (const std::string& argument : Arguments(program)) {
    command += " " + Quoted(argument);
  }
  return "[Unit]\n"
         "Description=Facepalm perception service\n"
         "Documentation=https://github.com/mattyv/facepalm\n"
         "\n"
         "[Service]\n"
         "ExecStart=" +
         command +
         "\n"
         "Restart=on-failure\n"
         "\n"
         "[Install]\n"
         "WantedBy=default.target\n";
}

std::unique_ptr<ServiceIo> PosixServiceIo() {
  return std::make_unique<PosixIo>();
}

std::optional<ServiceProgram> FindProgram(
    const std::vector<std::string>& prefixes, const std::string& config,
    ServiceIo& io) {
  for (const std::string& prefix : prefixes) {
    ServiceProgram program{
        .daemon = prefix + "/bin/facepalm-daemon",
        .model = prefix + "/share/facepalm/model/face_landmarker.task",
        .config = !config.empty() && io.Exists(config) ? config : ""};
    if (io.Exists(program.daemon) && io.Exists(program.model)) {
      return program;
    }
  }
  return std::nullopt;
}

UserService::UserService(Platform platform, std::string home, Approve approve,
                         ServiceIo& io)
    : platform_(platform),
      home_(std::move(home)),
      approve_(std::move(approve)),
      io_(io) {}

std::string UserService::File() const {
  return platform_ == Platform::kMacOs
             ? home_ + "/Library/LaunchAgents/" +
                   std::string(kLaunchAgentLabel) + ".plist"
             : home_ + "/.config/systemd/user/" + std::string(kSystemdUnit);
}

std::string UserService::Domain() const {
  return "gui/" + std::to_string(io_.User());
}

std::string UserService::Job() const {
  return Domain() + "/" + std::string(kLaunchAgentLabel);
}

// specs/UserService.tla, FileMatchesMenu.
bool UserService::IsOn() { return io_.Exists(File()); }

// specs/UserService.tla, TurnOn: a service that will not load leaves no
// file, so the menu still shows it off.
bool UserService::TurnOn(const ServiceProgram& program, std::string& error) {
  if (!approve_("mcp", error)) {
    return false;
  }
  const bool macos = platform_ == Platform::kMacOs;
  const std::string file = File();
  if (!io_.WriteFile(
          file, macos ? LaunchAgentPlist(program, home_ + "/Library/Logs/"
                                                          "facepalm-daemon.log")
                      : SystemdUnit(program))) {
    error = "cannot write " + file;
    return false;
  }
  bool loaded = false;
  if (macos) {
    // A job left loaded from an earlier run would refuse the bootstrap.
    io_.Run({"launchctl", "bootout", Job()});
    loaded = io_.Run({"launchctl", "bootstrap", Domain(), file}) == 0;
  } else {
    loaded = io_.Run({"systemctl", "--user", "daemon-reload"}) == 0 &&
             io_.Run({"systemctl", "--user", "enable", "--now",
                      std::string(kSystemdUnit)}) == 0;
  }
  if (!loaded) {
    io_.RemoveFile(file);
    error = (macos ? "launchctl" : "systemctl") +
            std::string(" could not load ") + file;
    return false;
  }
  return true;
}

// specs/UserService.tla, TurnOff, with GuardUnloadOnTurnOff and
// GuardRemoveOnTurnOff: the unload waits for the daemon to exit
// (GuardOffWaits). One that fails while the service still runs leaves it
// on, file and all.
bool UserService::TurnOff(std::string& error) {
  const bool macos = platform_ == Platform::kMacOs;
  const std::vector<std::string> stop =
      macos ? std::vector<std::string>{"launchctl", "bootout", Job()}
            : std::vector<std::string>{"systemctl", "--user", "disable",
                                       "--now", std::string(kSystemdUnit)};
  if (io_.Run(stop) != 0) {
    const std::vector<std::string> check =
        macos ? std::vector<std::string>{"launchctl", "print", Job()}
              : std::vector<std::string>{"systemctl", "--user", "is-active",
                                         "--quiet", std::string(kSystemdUnit)};
    if (io_.Run(check) == 0) {
      error = stop.front() + " could not stop facepalm";
      return false;
    }
  }
  const std::string file = File();
  if (!io_.RemoveFile(file)) {
    error = "cannot remove " + file;
    return false;
  }
  if (!macos) {
    io_.Run({"systemctl", "--user", "daemon-reload"});
  }
  return true;
}

}  // namespace facepalm
