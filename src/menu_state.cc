#include "src/menu_state.h"

namespace facepalm {

MenuState DecideMenuState(const DaemonState& state) {
  if (!state.service_on) {
    return MenuState{.camera_status_title = "Facepalm off",
                     .preview_title = "Turn on Facepalm first",
                     .preview_enabled = false,
                     .calibrate_title = "Turn on Facepalm first",
                     .calibrate_enabled = false};
  }
  return MenuState{
      .camera_status_title = state.camera_in_use ? "Camera on" : "Camera off",
      .preview_title =
          state.preview_open ? "Hide Camera Preview" : "Show Camera Preview",
      .preview_enabled = true,
      .calibrate_title = "Calibrate…",
      .calibrate_enabled = true};
}

}  // namespace facepalm
