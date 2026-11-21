#include "src/feeling_store.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/feelings.h"
#include "src/tuning.h"

namespace facepalm {
namespace {

constexpr mode_t kPrivateFile = S_IRUSR | S_IWUSR;
constexpr mode_t kPrivateFolder = S_IRWXU;
constexpr mode_t kOthers = S_IRWXG | S_IRWXO;
constexpr int kCreate = O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC;
constexpr int kCreateNew = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC;
constexpr int kReadOnly = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
constexpr int kFolder = O_RDONLY | O_DIRECTORY | O_CLOEXEC;

class PosixFeelingStoreIo : public FeelingStoreIo {
 public:
  int MakeDirectory(const std::string& path) override {
    return mkdir(path.c_str(), kPrivateFolder);
  }
  int Open(const std::string& path, int flags) override {
    return open(path.c_str(), flags, kPrivateFile);
  }
  int Lock(int fd) override { return flock(fd, LOCK_EX | LOCK_NB); }
  int Stat(int fd, struct stat& info) override { return fstat(fd, &info); }
  uid_t User() override { return geteuid(); }
  ssize_t Read(int fd, void* buffer, std::size_t size) override {
    return read(fd, buffer, size);
  }
  ssize_t Write(int fd, const void* buffer, std::size_t size) override {
    return write(fd, buffer, size);
  }
  int Sync(int fd) override { return fsync(fd); }
  int Rename(const std::string& from, const std::string& to) override {
    return rename(from.c_str(), to.c_str());
  }
  int Unlink(const std::string& path) override { return unlink(path.c_str()); }
  void Close(int fd) override { close(fd); }
};

// The folder `path` is in.
std::string Parent(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  return slash == 0 ? "/" : path.substr(0, slash);
}

// Makes `folder` and the folders above it that are missing.
bool MakeFolders(const std::string& folder, FeelingStoreIo& io) {
  std::vector<std::string> missing;
  std::string current = folder;
  while (io.MakeDirectory(current) != 0 && errno != EEXIST) {
    const std::size_t slash = current.find_last_of('/');
    if (errno != ENOENT || slash == std::string::npos || slash == 0) {
      return false;
    }
    missing.push_back(current);
    current.resize(slash);
  }
  for (const std::string& made : std::views::reverse(missing)) {
    if (io.MakeDirectory(made) != 0 && errno != EEXIST) {
      return false;
    }
  }
  return true;
}

// Whether `fd` is a regular file of at most kLargestFile bytes that only
// the user can open; its size in `size`.
bool Private(int fd, FeelingStoreIo& io, off_t& size) {
  struct stat info{};
  if (io.Stat(fd, info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_uid != io.User() || (info.st_mode & kOthers) != 0 ||
      info.st_size > FeelingStore::kLargestFile) {
    return false;
  }
  size = info.st_size;
  return true;
}

// The picks the file at `path` holds; none when it does not exist, and
// nullopt when it is not private or not facepalm's.
std::optional<Tuning> Load(const std::string& path, FeelingStoreIo& io) {
  const int fd = io.Open(path, kReadOnly);
  if (fd < 0) {
    return errno == ENOENT ? std::optional<Tuning>(Tuning()) : std::nullopt;
  }
  off_t size = 0;
  std::string text;
  const bool read =
      Private(fd, io, size) &&
      (text.resize(static_cast<std::size_t>(size)),
       io.Read(fd, text.data(), text.size()) == static_cast<ssize_t>(size));
  io.Close(fd);
  return read ? Tuning::Parse(text) : std::nullopt;
}

}  // namespace

std::unique_ptr<FeelingStore> FeelingStore::Open(
    const std::string& path, std::string& error,
    std::shared_ptr<FeelingStoreIo> io) {
  if (!io) {
    io = std::make_shared<PosixFeelingStoreIo>();
  }
  if (!MakeFolders(Parent(path), *io)) {
    error = "cannot make the feelings file's folder";
    return nullptr;
  }
  const int lock = io->Open(path + ".lock", kCreate);
  if (lock < 0) {
    error = "cannot open the feelings file";
    return nullptr;
  }
  off_t size = 0;
  if (!Private(lock, *io, size) || io->Lock(lock) != 0) {
    io->Close(lock);
    error = "the feelings file is unsafe or in use";
    return nullptr;
  }
  std::optional<Tuning> tuning = Load(path, *io);
  if (!tuning) {
    io->Close(lock);
    error = "the feelings file is unsafe or not facepalm's";
    return nullptr;
  }
  return std::unique_ptr<FeelingStore>(
      new FeelingStore(path, lock, std::move(*tuning), std::move(io)));
}

FeelingStore::~FeelingStore() { io_->Close(lock_); }

Feeling FeelingStore::Read(const Evidence& evidence, Feeling generic) {
  const std::scoped_lock lock(mutex_);
  return tuning_.Read(evidence, generic);
}

// FeelingFeedback!Begin: one save at a time, from the counts the reads use
// (GuardOneWriter).
bool FeelingStore::Learn(const Evidence& evidence, Feeling feeling) {
  const std::scoped_lock lock(mutex_);
  Tuning next = tuning_;
  next.Count(evidence, feeling);
  return Commit(std::move(next));
}

// FeelingFeedback!Begin for a reset: it saves empty counts
// (GuardResetEmpties).
bool FeelingStore::Forget() {
  const std::scoped_lock lock(mutex_);
  return Commit(Tuning());
}

// The reads take the new counts only once they are renamed in
// (GuardCommitOnRename), and the answer is ok only once the folder is
// synced (GuardAckDurable): FeelingFeedback!ReplyOk or ReplyError.
bool FeelingStore::Commit(Tuning next) {
  const Saved saved = Save(next);
  if (saved != Saved::kNot) {
    tuning_ = std::move(next);
  }
  return saved == Saved::kDurable;
}

// A fresh temporary file (GuardTempFile), synced before it is renamed over
// the file (GuardSyncBeforeRename), then the folder synced
// (FeelingFeedback!SyncDirectory). A step that fails is FeelingFeedback!Fail.
FeelingStore::Saved FeelingStore::Save(const Tuning& tuning) {
  const std::string text = tuning.Text();
  const std::string temp = path_ + ".tmp";
  io_->Unlink(temp);
  const int fd = io_->Open(temp, kCreateNew);
  if (fd < 0) {
    return Saved::kNot;
  }
  const bool written = io_->Write(fd, text.data(), text.size()) ==
                           static_cast<ssize_t>(text.size()) &&
                       io_->Sync(fd) == 0;
  io_->Close(fd);
  if (!written || io_->Rename(temp, path_) != 0) {
    return Saved::kNot;
  }
  const int folder = io_->Open(Parent(path_), kFolder);
  if (folder < 0) {
    return Saved::kRenamed;
  }
  const bool synced = io_->Sync(folder) == 0;
  io_->Close(folder);
  return synced ? Saved::kDurable : Saved::kRenamed;
}

std::string DefaultFeelingsPath(
    const std::function<const char*(const char*)>& variable) {
  const char* config_home = variable("XDG_CONFIG_HOME");
  const std::string_view config = config_home == nullptr ? "" : config_home;
  if (!config.empty()) {
    return std::string(config) + "/facepalm/feelings";
  }
  const char* home = variable("HOME");
  const std::string_view user = home == nullptr ? "" : home;
  return user.empty() ? std::string()
                      : std::string(user) + "/.config/facepalm/feelings";
}

}  // namespace facepalm
