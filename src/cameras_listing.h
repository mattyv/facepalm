#ifndef FACEPALM_SRC_CAMERAS_LISTING_H_
#define FACEPALM_SRC_CAMERAS_LISTING_H_

#include <string>
#include <string_view>
#include <vector>

namespace facepalm {

// One camera facepalm-daemon's `cameras` command listed: its id, its name,
// and whether it is the built-in camera (item 10 of #61, #13).
struct CamerasListingEntry {
  std::string id;
  std::string name;
  bool built_in = false;
};

// Parses `text`, as facepalm-daemon's `cameras` command (tools/daemon's
// ListCameras) writes it: one line a camera, three fields tab-separated
// (id, name, "1" or "0" for built in), "\n"-terminated. A line without
// exactly three fields is skipped, and a third field other than "1" reads
// as not built in, following the menu apps' own plain splits
// (tools/menu/main.mm, tools/menu/main_linux.cc) exactly, so the daemon's
// own stdout, from any version of itself, is read the same way and never
// crashes this parser (#120).
std::vector<CamerasListingEntry> ParseCamerasListing(std::string_view text);

// The inverse of ParseCamerasListing: facepalm-daemon's own `cameras`
// command (tools/daemon/main.cc's ListCameras) writes its list this way,
// one line a camera, so ParseCamerasListing reads back exactly what this
// writes.
std::string FormatCamerasListing(
    const std::vector<CamerasListingEntry>& cameras);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CAMERAS_LISTING_H_
