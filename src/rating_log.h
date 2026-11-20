#ifndef FACEPALM_SRC_RATING_LOG_H_
#define FACEPALM_SRC_RATING_LOG_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "src/reaction_feed.h"

namespace facepalm {
// File operations at the persistence boundary, replaceable for failure tests.
class RatingLogIo {
 public:
  virtual ~RatingLogIo() = default;
  virtual int OpenFile(const std::string& path) = 0;
  virtual int OpenDirectory(const std::string& path) = 0;
  virtual int Stat(int fd, struct stat& result) = 0;
  virtual uid_t User() = 0;
  virtual int Lock(int fd) = 0;
  virtual ssize_t ReadAt(int fd, void* buffer, std::size_t size,
                         off_t offset) = 0;
  virtual ssize_t Write(int fd, const void* buffer, std::size_t size) = 0;
  virtual int Sync(int fd) = 0;
  virtual void Close(int fd) = 0;
};

// One opt-in, owner-private append-only feedback file. A partial write or
// sync failure disables the sink; a later open refuses an incomplete line.
class RatingLog {
 public:
  static std::unique_ptr<RatingLog> Open(const std::string& path,
                                         std::string& error,
                                         std::shared_ptr<RatingLogIo> io = {});
  ~RatingLog();
  RatingLog(const RatingLog&) = delete;
  RatingLog& operator=(const RatingLog&) = delete;
  bool Record(const ReactionFeed::Rating& rating);

 private:
  RatingLog(int fd, std::shared_ptr<RatingLogIo> io)
      : fd_(fd), io_(std::move(io)) {}
  const int fd_;
  std::shared_ptr<RatingLogIo> io_;
  std::mutex mutex_;
  bool poisoned_ = false;
};
}  // namespace facepalm
#endif  // FACEPALM_SRC_RATING_LOG_H_
