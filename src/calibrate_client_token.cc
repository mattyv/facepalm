#include "src/calibrate_client_token.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "src/client_approvals.h"

namespace facepalm {
namespace {
constexpr std::string_view kClientName = "calibrate";
}  // namespace

CalibrateClientToken ResolveCalibrateToken(ClientApprovals& approvals,
                                           const ClientApprovals::Load& load) {
  std::string error;
  const std::optional<std::string> path = approvals.Approve(kClientName, error);
  if (!path) {
    return {.token = std::nullopt, .error = std::move(error)};
  }
  return {.token = load(*path).value_or(std::string()), .error = {}};
}

}  // namespace facepalm
