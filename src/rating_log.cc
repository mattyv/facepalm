#include "src/rating_log.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "src/protocol.h"
#include "src/reaction_feed.h"

namespace facepalm {
namespace {
constexpr off_t kMaximumBytes = 10LL * 1024 * 1024;
constexpr mode_t kPrivateMode = S_IRUSR | S_IWUSR;
constexpr int kMaximumScore = 5;
constexpr std::size_t kMaximumSummaryBytes = 256;
constexpr mode_t kOtherPermissions = S_IRWXG | S_IRWXO;
class PosixRatingLogIo : public RatingLogIo {
 public:
  int OpenFile(const std::string& path) override {
    return open(path.c_str(),
                O_RDWR | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                kPrivateMode);
  }
  int OpenDirectory(const std::string& path) override {
    return open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  }
  int Stat(int fd, struct stat& result) override { return fstat(fd, &result); }
  uid_t User() override { return geteuid(); }
  int Lock(int fd) override { return flock(fd, LOCK_EX | LOCK_NB); }
  ssize_t ReadAt(int fd, void* buffer, std::size_t size,
                 off_t offset) override {
    return pread(fd, buffer, size, offset);
  }
  ssize_t Write(int fd, const void* buffer, std::size_t size) override {
    return write(fd, buffer, size);
  }
  int Sync(int fd) override { return fsync(fd); }
  void Close(int fd) override { close(fd); }
};

bool SyncParent(const std::string& path, RatingLogIo& io) {
  const auto slash = path.find_last_of('/');
  std::string parent = ".";
  if (slash != std::string::npos) {
    parent = slash == 0 ? "/" : path.substr(0, slash);
  }
  const int directory = io.OpenDirectory(parent);
  if (directory < 0) {
    return false;
  }
  const bool synced = io.Sync(directory) == 0;
  io.Close(directory);
  return synced;
}

bool ValidFile(int fd, RatingLogIo& io) {
  struct stat info{};
  if (io.Stat(fd, info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_uid != io.User() || (info.st_mode & kOtherPermissions) != 0 ||
      info.st_size < 0 || info.st_size > kMaximumBytes) {
    return false;
  }
  if (info.st_size == 0) {
    return true;
  }
  constexpr std::string_view kPrefix = R"({"version":1,)";
  if (std::cmp_less(info.st_size, kPrefix.size())) {
    return false;
  }
  std::string prefix(kPrefix.size(), '\0');
  if (io.ReadAt(fd, prefix.data(), prefix.size(), 0) !=
          static_cast<ssize_t>(prefix.size()) ||
      prefix != kPrefix) {
    return false;
  }
  char last = 0;
  return io.ReadAt(fd, &last, 1, info.st_size - 1) == 1 && last == '\n';
}
}  // namespace

std::unique_ptr<RatingLog> RatingLog::Open(const std::string& path,
                                           std::string& error,
                                           std::shared_ptr<RatingLogIo> io) {
  if (!io) {
    io = std::make_shared<PosixRatingLogIo>();
  }
  const int fd = io->OpenFile(path);
  if (fd < 0) {
    error = "cannot open feedback file";
    return nullptr;
  }
  if (io->Lock(fd) != 0 || !ValidFile(fd, *io) || !SyncParent(path, *io)) {
    io->Close(fd);
    error = "feedback file is unavailable, unsafe or incomplete";
    return nullptr;
  }
  return std::unique_ptr<RatingLog>(new RatingLog(fd, std::move(io)));
}

RatingLog::~RatingLog() { io_->Close(fd_); }

bool RatingLog::Record(const ReactionFeed::Rating& rating) {
  const std::scoped_lock lock(mutex_);
  if (poisoned_ || rating.id <= 0 || rating.score < 1 ||
      rating.score > kMaximumScore || rating.summary.empty() ||
      rating.summary.size() > kMaximumSummaryBytes || !rating.published_at_ms ||
      *rating.published_at_ms <= 0) {
    return false;
  }
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  const std::string line =
      R"({"version":1,"id":)" + std::to_string(rating.id) +
      ",\"summary\":" + protocol::Quoted(rating.summary) +
      ",\"published_at_ms\":" + std::to_string(*rating.published_at_ms) +
      ",\"rated_at_ms\":" + std::to_string(now) +
      ",\"score\":" + std::to_string(rating.score) + "}\n";
  struct stat info{};
  if (io_->Stat(fd_, info) != 0 || info.st_size < 0 ||
      info.st_size > kMaximumBytes - static_cast<off_t>(line.size())) {
    return false;
  }
  ssize_t written;
  do {
    written = io_->Write(fd_, line.data(), line.size());
  } while (written < 0 && errno == EINTR);
  if (std::cmp_not_equal(written, line.size()) || io_->Sync(fd_) != 0) {
    poisoned_ = true;
    return false;
  }
  return true;
}
}  // namespace facepalm
