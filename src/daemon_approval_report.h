#ifndef FACEPALM_SRC_DAEMON_APPROVAL_REPORT_H_
#define FACEPALM_SRC_DAEMON_APPROVAL_REPORT_H_

#include <optional>
#include <string>

namespace facepalm {

// What facepalm-daemon's `approve`/`revoke` commands print and exit with
// (tools/daemon/main.cc's ChangeApproval): `stdout_line` and `stderr_line`
// are printed, each followed by a newline, only when they are not empty;
// at most one of the two is ever set.
struct ApprovalReport {
  int exit_code = 0;
  std::string stdout_line;
  std::string stderr_line;
};

// facepalm-daemon's `revoke` command: `revoked` is
// ClientApprovals::Revoke's own result for `client`.
ApprovalReport RevokeReport(bool revoked, const std::string& client);

// facepalm-daemon's `approve` command: `path` is ClientApprovals::Approve's
// own result for `client`, with `error` its reason when `path` is
// nullopt.
ApprovalReport ApproveReport(const std::optional<std::string>& path,
                             const std::string& client,
                             const std::string& error);

}  // namespace facepalm

#endif  // FACEPALM_SRC_DAEMON_APPROVAL_REPORT_H_
