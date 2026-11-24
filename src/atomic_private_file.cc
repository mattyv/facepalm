#include "src/atomic_private_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace facepalm {
namespace {

constexpr mode_t kPrivateMode = S_IRUSR | S_IWUSR;

class PosixAtomicPrivateFileIo : public AtomicPrivateFileIo {
 public:
  int OpenTemp(const std::string& path) override {
    return open((path + ".tmp").c_str(),
                O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC,
                kPrivateMode);
  }
  bool Chmod(int fd) override { return fchmod(fd, kPrivateMode) == 0; }
  ssize_t Write(int fd, const void* data, std::size_t size) override {
    return write(fd, data, size);
  }
  void Close(int fd) override { close(fd); }
  bool Rename(const std::string& path) override {
    return std::rename((path + ".tmp").c_str(), path.c_str()) == 0;
  }
  void Remove(const std::string& path) override {
    std::remove((path + ".tmp").c_str());
  }
};

}  // namespace

bool WriteFileAtomically(const std::string& path, std::string_view bytes,
                         std::shared_ptr<AtomicPrivateFileIo> io) {
  if (!io) {
    io = std::make_shared<PosixAtomicPrivateFileIo>();
  }
  const int fd = io->OpenTemp(path);
  if (fd < 0) {
    return false;
  }
  if (!io->Chmod(fd)) {
    io->Close(fd);
    io->Remove(path);
    return false;
  }
  const bool written = io->Write(fd, bytes.data(), bytes.size()) ==
                       static_cast<ssize_t>(bytes.size());
  io->Close(fd);
  if (!written) {
    io->Remove(path);
    return false;
  }
  if (!io->Rename(path)) {
    io->Remove(path);
    return false;
  }
  return true;
}

}  // namespace facepalm
