#ifndef FACEPALM_SRC_PREVIEW_REQUEST_H_
#define FACEPALM_SRC_PREVIEW_REQUEST_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace facepalm {

// How long a request may go untouched by the menu and still count as
// asking (#114, specs/PreviewLease.tla's StaleAge): long enough for the
// menu's own 0.5 s refresh timer (tools/menu/main.mm,
// tools/menu/main_linux.cc) to keep up, short enough that a request a
// menu left behind -- quit without cleaning up, crashed, or its session
// logged out -- stops streaming soon after.
inline constexpr std::int64_t kPreviewRequestStaleMs = 2000;

// Whether the menu is still asking for the preview (#114,
// specs/PreviewLease.tla's Fresh): `path` names a request file, and it is
// both there and has been touched -- by the menu's own write, or its
// periodic refresh while its window stays open -- within
// kPreviewRequestStaleMs of `now_ms`. `modified_ms` reports a file's
// last-modified time in milliseconds since the epoch, or nullopt when
// there is none to report (missing, or any other read error), which this
// reads the same way as too old: not fresh. Injected rather than reading
// the real filesystem, so this is testable without one.
bool IsFreshPreviewRequest(
    const std::string& path, std::int64_t now_ms,
    const std::function<std::optional<std::int64_t>(const std::string&)>&
        modified_ms);

}  // namespace facepalm

#endif  // FACEPALM_SRC_PREVIEW_REQUEST_H_
