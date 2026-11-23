#ifndef FACEPALM_SRC_MENU_STATE_H_
#define FACEPALM_SRC_MENU_STATE_H_

#include <string>

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

// What DecideMenuState reads to decide MenuState, each independent of the
// other two, so they are an options object rather than three positional
// bools a call site could swap by accident.
struct DaemonState {
  // Facepalm's own user service (UserService::IsOn). Once it is on,
  // nothing it asks of the daemon -- opening the preview, now a camera
  // demand of its own (#176, src/camera_demand.h), or starting a
  // calibration session -- can fail for lack of one to reach.
  bool service_on = false;
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

MenuState DecideMenuState(const DaemonState& state);

}  // namespace facepalm

#endif  // FACEPALM_SRC_MENU_STATE_H_
