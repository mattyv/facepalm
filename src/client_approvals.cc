#include "src/client_approvals.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/random.h>
#endif

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace facepalm {
namespace {

constexpr std::size_t kLongestName = 64;
constexpr std::size_t kTokenBytes = 32;
constexpr mode_t kPrivateDirectory = 0700;
constexpr mode_t kPrivateFile = 0600;

class PosixIo : public ApprovalsIo {
 public:
  std::optional<std::vector<std::string>> List(
      const std::string& directory) override {
    DIR* const stream = opendir(directory.c_str());
    if (stream == nullptr) {
      return std::nullopt;
    }
    std::vector<std::string> names;
    while (const dirent* entry = readdir(stream)) {
      const std::string_view name(entry->d_name);
      if (name != "." && name != "..") {
        names.emplace_back(name);
      }
    }
    closedir(stream);
    return names;
  }

  bool MakePrivateDirectory(const std::string& directory) override {
    if (directory.empty()) {
      return false;
    }
    // Each component that is missing is made; one that is there must be a
    // directory. The full path itself (#114) is put back to
    // kPrivateDirectory even when it was already there, left by an older
    // build or a looser umask than this call's own explicit mode: an
    // intermediate component already there is left alone, since it is
    // not this call's own to secure (it may be a shared system directory,
    // such as the one above it). Best effort, the same as the tray's own
    // QDir().mkpath() calls: a directory that exists and is a directory
    // is still usable even if tightening its mode happens to fail, so
    // that alone is not reason enough to refuse it outright.
    for (std::size_t slash = directory.find('/', 1);;
         slash = directory.find('/', slash + 1)) {
      const std::string prefix = directory.substr(0, slash);
      const bool full = slash == std::string::npos;
      struct stat info{};
      if (stat(prefix.c_str(), &info) == 0) {
        if (!S_ISDIR(info.st_mode)) {
          return false;
        }
        if (full) {
          chmod(prefix.c_str(), kPrivateDirectory);
        }
      } else if (mkdir(prefix.c_str(), kPrivateDirectory) != 0) {
        return false;
      }
      if (full) {
        return true;
      }
    }
  }

  bool CreatePrivate(const std::string& path,
                     std::string_view contents) override {
    const int fd =
        open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
             kPrivateFile);
    if (fd < 0) {
      return false;
    }
    const bool written = write(fd, contents.data(), contents.size()) ==
                         static_cast<ssize_t>(contents.size());
    close(fd);
    return written;
  }

  bool Remove(const std::string& path) override {
    return unlink(path.c_str()) == 0;
  }

  std::optional<std::string> Random(std::size_t count) override {
    std::string bytes(count, '\0');
    if (getentropy(bytes.data(), count) != 0) {
      return std::nullopt;
    }
    return bytes;
  }
};

// Compares in time that depends on the length only, so a client cannot learn
// a token a byte at a time.
bool SameToken(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  unsigned difference = 0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    const unsigned left_byte = static_cast<unsigned char>(left[i]);
    const unsigned right_byte = static_cast<unsigned char>(right[i]);
    difference |= left_byte ^ right_byte;
  }
  return difference == 0;
}

std::string Hex(std::string_view bytes) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  constexpr unsigned kNibble = 4;
  constexpr unsigned kLowNibble = 0xf;
  std::string hex;
  for (const char byte : bytes) {
    const auto value = static_cast<unsigned char>(byte);
    hex += kDigits[value >> kNibble];
    hex += kDigits[value & kLowNibble];
  }
  return hex;
}

}  // namespace

std::unique_ptr<ApprovalsIo> PosixApprovalsIo() {
  return std::make_unique<PosixIo>();
}

ClientApprovals::ClientApprovals(std::string directory, Load load,
                                 ApprovalsIo& io)
    : directory_(std::move(directory)), load_(std::move(load)), io_(io) {}

bool ClientApprovals::IsClientName(std::string_view name) {
  if (name.empty() || name.size() > kLongestName || name.starts_with('.')) {
    return false;
  }
  return std::ranges::all_of(name, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == '-';
  });
}

// Service!Hello's GuardApproval. Entries that cannot name a client, such as
// an editor's hidden files, are not read.
bool ClientApprovals::Approved(std::string_view token) {
  if (token.empty()) {
    return false;
  }
  const std::optional<std::vector<std::string>> names = io_.List(directory_);
  if (!names) {
    return false;
  }
  return std::ranges::any_of(*names, [&](const std::string& name) {
    if (!IsClientName(name)) {
      return false;
    }
    const std::optional<std::string> held = load_(directory_ + "/" + name);
    return held && SameToken(*held, token);
  });
}

std::optional<std::string> ClientApprovals::Approve(std::string_view name,
                                                    std::string& error) {
  if (!IsClientName(name)) {
    error = "a client name is 1 to 64 of a-z 0-9 . _ -";
    return std::nullopt;
  }
  if (!io_.MakePrivateDirectory(directory_)) {
    error = "cannot make " + directory_;
    return std::nullopt;
  }
  std::string path = directory_ + "/" + std::string(name);
  if (load_(path)) {
    return path;
  }
  const std::optional<std::string> bytes = io_.Random(kTokenBytes);
  if (!bytes) {
    error = "cannot make a random token";
    return std::nullopt;
  }
  if (!io_.CreatePrivate(path, Hex(*bytes) + "\n")) {
    error = "cannot write " + path;
    return std::nullopt;
  }
  return path;
}

bool ClientApprovals::Revoke(std::string_view name) {
  return IsClientName(name) && io_.Remove(directory_ + "/" + std::string(name));
}

}  // namespace facepalm
