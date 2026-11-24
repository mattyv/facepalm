#ifndef FACEPALM_SRC_MENU_STATE_H_
#define FACEPALM_SRC_MENU_STATE_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace facepalm {

// What the menu bar app (tools/menu/main.mm) and the Linux tray
// (tools/menu/main_linux.cc) show for the camera-status line and the
// "Show Camera Preview" and "Calibrate…" items, decided purely from the
// daemon's own state (#176): the per-item table in docs/local-app.md.
// Neither front end reads any of this more than once per refresh; both
// ask DecideMenuState for what to show with it, so the two platforms can
// never disagree about when an item is enabled or what it says.
struct MenuState {
  // The always-disabled line above the other items.
  std::string camera_status_title;
  std::string preview_title;
  bool preview_enabled = false;
  std::string calibrate_title;
  bool calibrate_enabled = false;
};

// The observations used to decide what the menu can offer.
struct DaemonState {
  // Installed intent is separate from a reachable socket and manager state.
  bool service_on = false;
  bool service_ready = false;
  bool service_failed = false;
  bool service_starting = false;
  // The camera-in-use indicator (ReadCameraStatus): only meaningful
  // while the service is on, since nothing publishes it otherwise, which
  // is why a missing status file already reads as off
  // (src/daemon_status.h) -- `service_on` is what tells the two apart in
  // the camera-status line, rather than leaving "Camera off" standing
  // for both "idle" and "there is no camera to be idle".
  bool camera_in_use = false;
  // Whether this menu's own preview window is showing, so its item reads
  // "Hide" rather than "Show" while it does.
  bool preview_open = false;
};

enum class ServiceHealth : std::uint8_t { kFailed, kStarting, kUnavailable };
ServiceHealth ParseServiceHealth(std::string_view active_state);

MenuState DecideMenuState(const DaemonState& state);

}  // namespace facepalm

#endif  // FACEPALM_SRC_MENU_STATE_H_
