#include "src/daemon_approval_report.h"

#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;

TEST(RevokeReportTest, SucceedsWithNoMessage) {
  const ApprovalReport report = RevokeReport(/*revoked=*/true, "claude");
  EXPECT_THAT(report.exit_code, Eq(0));
  EXPECT_THAT(report.stdout_line, IsEmpty());
  EXPECT_THAT(report.stderr_line, IsEmpty());
}

TEST(RevokeReportTest, FailsNamingTheClientOnStderr) {
  const ApprovalReport report = RevokeReport(/*revoked=*/false, "claude");
  EXPECT_THAT(report.exit_code, Eq(1));
  EXPECT_THAT(report.stdout_line, IsEmpty());
  EXPECT_THAT(report.stderr_line, Eq("no approved client called claude"));
}

TEST(ApproveReportTest, SucceedsPrintingThePathOnStdout) {
  const ApprovalReport report =
      ApproveReport("/private/clients/claude", "claude", "");
  EXPECT_THAT(report.exit_code, Eq(0));
  EXPECT_THAT(report.stdout_line, Eq("/private/clients/claude"));
  EXPECT_THAT(report.stderr_line, IsEmpty());
}

TEST(ApproveReportTest, FailsNamingTheClientAndReasonOnStderr) {
  const ApprovalReport report =
      ApproveReport(std::nullopt, "claude", "cannot make clients directory");
  EXPECT_THAT(report.exit_code, Eq(1));
  EXPECT_THAT(report.stdout_line, IsEmpty());
  EXPECT_THAT(report.stderr_line,
              Eq("cannot approve claude: cannot make clients directory"));
}

}  // namespace
}  // namespace facepalm
