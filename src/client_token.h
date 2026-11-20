#ifndef FACEPALM_SRC_CLIENT_TOKEN_H_
#define FACEPALM_SRC_CLIENT_TOKEN_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <optional>
#include <string>

namespace facepalm {
// System calls at the credential boundary, replaceable for failure tests.
class ClientTokenIo {
 public:
  virtual ~ClientTokenIo() = default;
  virtual int Open(const std::string& path) = 0;
  virtual int Stat(int fd, struct stat& result) = 0;
  virtual ssize_t Read(int fd, void* buffer, std::size_t count) = 0;
  virtual void Close(int fd) = 0;
  virtual uid_t User() = 0;
};
std::optional<std::string> LoadClientToken(const std::string& path,
                                           ClientTokenIo& io);
// Reads one 64-character lowercase hex token from a user-private regular file.
std::optional<std::string> LoadClientToken(const std::string& path);
}  // namespace facepalm
#endif  // FACEPALM_SRC_CLIENT_TOKEN_H_
