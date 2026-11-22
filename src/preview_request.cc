#include "src/preview_request.h"

namespace facepalm {

bool IsFreshPreviewRequest(
    const std::string& path, std::int64_t now_ms,
    const std::function<std::optional<std::int64_t>(const std::string&)>&
        modified_ms) {
  const std::optional<std::int64_t> modified = modified_ms(path);
  return modified.has_value() && now_ms - *modified <= kPreviewRequestStaleMs;
}

}  // namespace facepalm
