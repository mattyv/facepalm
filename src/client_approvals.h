#ifndef FACEPALM_SRC_CLIENT_APPROVALS_H_
#define FACEPALM_SRC_CLIENT_APPROVALS_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/service.h"

namespace facepalm {

// File system calls for the approvals directory, replaceable for failure
// tests.
class ApprovalsIo {
 public:
  virtual ~ApprovalsIo() = default;
  // The names of the entries in `directory`; nullopt when it cannot be read.
  virtual std::optional<std::vector<std::string>> List(
      const std::string& directory) = 0;
  // Makes `directory` and any parent missing, each private to the user.
  virtual bool MakePrivateDirectory(const std::string& directory) = 0;
  // Writes a new file, private to the user; false when one is already there.
  virtual bool CreatePrivate(const std::string& path,
                             std::string_view contents) = 0;
  virtual bool Remove(const std::string& path) = 0;
  // `count` random bytes, from the OS.
  virtual std::optional<std::string> Random(std::size_t count) = 0;
};

// The OS's calls.
std::unique_ptr<ApprovalsIo> PosixApprovalsIo();

// The clients the person approved (#37): one token file per client in a
// directory private to the user, so a client finds its token by name, and
// approving one needs no hand-made secret. Approving a client gives it a
// new random token; revoking it removes the file. Service asks it whether
// a token is approved at each hello, so a change applies to the next
// connection without restarting the daemon.
class ClientApprovals : public Approvals {
 public:
  // Reads a token file as LoadClientToken does.
  using Load = std::function<std::optional<std::string>(const std::string&)>;

  ClientApprovals(std::string directory, Load load, ApprovalsIo& io);

  // Whether `name` can name a client: 1 to 64 of a-z 0-9 . _ -, not
  // starting with a dot.
  static bool IsClientName(std::string_view name);

  // Service!Hello's GuardApproval: whether any approved client's file holds
  // `token`.
  [[nodiscard]] bool Approved(std::string_view token) override;
  // The path of the token file for the client called `name`, made with a
  // new token unless it already holds one; nullopt, with `error` saying
  // why, when it cannot be.
  std::optional<std::string> Approve(std::string_view name, std::string& error);
  // Removes the client's token file; false when there was none to remove.
  bool Revoke(std::string_view name);

 private:
  std::string directory_;
  Load load_;
  ApprovalsIo& io_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CLIENT_APPROVALS_H_
