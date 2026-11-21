#ifndef FACEPALM_SRC_FEELING_STORE_H_
#define FACEPALM_SRC_FEELING_STORE_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "src/feelings.h"
#include "src/tuner.h"
#include "src/tuning.h"

namespace facepalm {

// The file operations the store makes, replaceable for failure tests.
class FeelingStoreIo {
 public:
  virtual ~FeelingStoreIo() = default;
  // mkdir, private to the user.
  virtual int MakeDirectory(const std::string& path) = 0;
  // open; a file it creates is private to the user.
  virtual int Open(const std::string& path, int flags) = 0;
  // An exclusive lock that does not wait.
  virtual int Lock(int fd) = 0;
  virtual int Stat(int fd, struct stat& info) = 0;
  virtual uid_t User() = 0;
  virtual ssize_t Read(int fd, void* buffer, std::size_t size) = 0;
  virtual ssize_t Write(int fd, const void* buffer, std::size_t size) = 0;
  virtual int Sync(int fd) = 0;
  virtual int Rename(const std::string& from, const std::string& to) = 0;
  virtual int Unlink(const std::string& path) = 0;
  virtual void Close(int fd) = 0;
};

// The person's picks (#35, item 4 of #61), in one file private to them
// that only this store writes, following specs/FeelingFeedback.tla. It
// holds `<path>.lock` while it is open, so a second daemon cannot open it
// (the spec's one writer, across processes), and saves one pick at a time
// (GuardOneWriter) by renaming a synced temporary file, `<path>.tmp`, over
// the file, so a crash leaves the old counts or the new.
class FeelingStore : public Tuner {
 public:
  // The largest file the store reads.
  static constexpr off_t kLargestFile = off_t{64} * 1024;

  // The store of the file at `path`, making its folders as needed; nullptr,
  // with `error` saying why, when the file or its lock is not private to
  // the user, another store holds it, or it is not facepalm's.
  static std::unique_ptr<FeelingStore> Open(
      const std::string& path, std::string& error,
      std::shared_ptr<FeelingStoreIo> io = {});
  ~FeelingStore() override;
  FeelingStore(const FeelingStore&) = delete;
  FeelingStore& operator=(const FeelingStore&) = delete;

  Feeling Read(const Evidence& evidence, Feeling generic) override;
  bool Learn(const Evidence& evidence, Feeling feeling) override;
  bool Forget() override;

 private:
  // How far a save got.
  enum class Saved : std::uint8_t { kNot, kRenamed, kDurable };

  FeelingStore(std::string path, int lock, Tuning tuning,
               std::shared_ptr<FeelingStoreIo> io)
      : path_(std::move(path)),
        lock_(lock),
        io_(std::move(io)),
        tuning_(std::move(tuning)) {}

  // Saves `next`, and reads from it once it is in the file. Under mutex_.
  bool Commit(Tuning next);
  // FeelingFeedback!Write, SyncTemp, Rename and SyncDirectory.
  Saved Save(const Tuning& tuning);

  const std::string path_;
  const int lock_;
  std::shared_ptr<FeelingStoreIo> io_;
  std::mutex mutex_;
  Tuning tuning_;  // Under mutex_: what the file holds.
};

// Where the daemon keeps the person's picks by default: `facepalm/feelings`
// in $XDG_CONFIG_HOME, or else in ~/.config, as the settings are, with
// `variable` giving the environment's values, as std::getenv does. Empty
// when neither XDG_CONFIG_HOME nor HOME is set.
std::string DefaultFeelingsPath(
    const std::function<const char*(const char*)>& variable);

}  // namespace facepalm

#endif  // FACEPALM_SRC_FEELING_STORE_H_
