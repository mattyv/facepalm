#ifndef FACEPALM_SRC_CALIBRATION_STORE_H_
#define FACEPALM_SRC_CALIBRATION_STORE_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "src/calibration.h"

namespace facepalm {

// File operations at the persistence boundary, replaceable for failure
// tests, as RatingLogIo and ClientTokenIo are.
class CalibrationFileIo {
 public:
  virtual ~CalibrationFileIo() = default;
  // `path` + ".tmp" for owner-only writing, created or truncated empty, or
  // -1.
  virtual int OpenTemp(const std::string& path) = 0;
  virtual ssize_t Write(int fd, const void* data, std::size_t size) = 0;
  virtual int Sync(int fd) = 0;
  virtual void Close(int fd) = 0;
  // Atomically replaces `path` with `path` + ".tmp".
  virtual int Rename(const std::string& path) = 0;
  // Opens a directory for reading, so it can be fsynced.
  virtual int OpenDirectory(const std::string& path) = 0;
  // `path` for reading, or -1 when there is none.
  virtual int OpenForRead(const std::string& path) = 0;
  virtual int Stat(int fd, struct stat& result) = 0;
  virtual uid_t User() = 0;
  virtual ssize_t Read(int fd, void* buffer, std::size_t size) = 0;
};

// Saves one CalibrationProfile to a private file the daemon owns (#36,
// #35, item 12 of #61): written to a temporary file beside it and renamed
// over it, so a reader never sees a partial profile, and kept readable and
// writable by its owner alone.
class CalibrationFileStore : public CalibrationStore {
 public:
  explicit CalibrationFileStore(std::string path,
                                std::shared_ptr<CalibrationFileIo> io = {});

  bool Save(const CalibrationProfile& profile, std::string& error) override;

 private:
  std::string path_;
  std::shared_ptr<CalibrationFileIo> io_;
};

// The text CalibrationFileStore saves `profile` as.
std::string SerializeCalibrationProfile(const CalibrationProfile& profile);

// `text`, as SerializeCalibrationProfile writes it, parsed back; nullopt
// with `error` saying why not.
std::optional<CalibrationProfile> ParseCalibrationProfile(std::string_view text,
                                                          std::string& error);

// Reads the profile at `path` (#36): a regular file owned, and readable
// and writable by, no one but its owner. nullopt when there is none yet
// (`error` is then empty) or when it cannot be read, is unsafe to read, or
// does not parse (`error` then says which). Coarse gaze (#36) loads the
// profile this way.
std::optional<CalibrationProfile> LoadCalibrationProfile(
    const std::string& path, std::string& error,
    std::shared_ptr<CalibrationFileIo> io = {});

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATION_STORE_H_
