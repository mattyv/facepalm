#ifndef FACEPALM_SRC_ATOMIC_PRIVATE_FILE_H_
#define FACEPALM_SRC_ATOMIC_PRIVATE_FILE_H_

#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace facepalm {

// File operations at the persistence boundary, replaceable for failure
// tests, as CalibrationFileIo and RatingLogIo are.
class AtomicPrivateFileIo {
 public:
  virtual ~AtomicPrivateFileIo() = default;
  // `path` + ".tmp", owner-only (0600), created or truncated empty, or -1.
  virtual int OpenTemp(const std::string& path) = 0;
  // Enforces 0600 on `fd` unconditionally (#129): OpenTemp's own mode
  // argument only applies the first time O_CREAT actually creates the
  // file, so an existing ".tmp" an earlier failed write left behind --
  // possibly under a looser mode, from before this fix -- would otherwise
  // keep it.
  virtual bool Chmod(int fd) = 0;
  virtual ssize_t Write(int fd, const void* data, std::size_t size) = 0;
  virtual void Close(int fd) = 0;
  // Atomically replaces `path` with `path` + ".tmp".
  virtual bool Rename(const std::string& path) = 0;
  // Removes `path` + ".tmp", left behind by a failure.
  virtual void Remove(const std::string& path) = 0;
};

// Replaces `path` with `bytes` atomically, private to the user (#37 item
// 8, #114, #129): a temporary file beside it, kept at exactly 0600
// regardless of umask or of whatever an earlier failed write already left
// it at, renamed over `path` only once fully written, so a reader never
// sees a half-written or loosely-permissioned file. Every failure path
// removes the temporary file it leaves behind. tools/daemon/main.cc's
// Engine uses this for the camera preview's own frame and readings files.
bool WriteFileAtomically(const std::string& path, std::string_view bytes,
                         std::shared_ptr<AtomicPrivateFileIo> io = {});

}  // namespace facepalm

#endif  // FACEPALM_SRC_ATOMIC_PRIVATE_FILE_H_
