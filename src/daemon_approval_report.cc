#include "src/daemon_approval_report.h"

#include <optional>
#include <string>

namespace facepalm {

ApprovalReport RevokeReport(bool revoked, const std::string& client) {
  if (!revoked) {
    return {.exit_code = 1,
            .stdout_line = {},
            .stderr_line = "no approved client called " + client};
  }
  return {.exit_code = 0, .stdout_line = {}, .stderr_line = {}};
}

ApprovalReport ApproveReport(const std::optional<std::string>& path,
                             const std::string& client,
                             const std::string& error) {
  if (!path) {
    return {.exit_code = 1,
            .stdout_line = {},
            .stderr_line = "cannot approve " + client + ": " + error};
  }
  return {.exit_code = 0, .stdout_line = *path, .stderr_line = {}};
}

}  // namespace facepalm
