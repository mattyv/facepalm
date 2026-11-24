#include "src/menu_state.h"

namespace facepalm {

ServiceHealth ParseServiceHealth(std::string_view active_state) {
  if (active_state == "failed\n") return ServiceHealth::kFailed;
  if (active_state == "activating\n") return ServiceHealth::kStarting;
  return ServiceHealth::kUnavailable;
}

MenuState DecideMenuState(const DaemonState& state) {
  if (!state.service_on) {
    return MenuState{.camera_status_title = "Facepalm off",
                     .preview_title = state.preview_open
                                          ? "Hide Camera Preview"
                                          : "Turn on Facepalm first",
                     .preview_enabled = state.preview_open,
                     .calibrate_title = "Turn on Facepalm first",
                     .calibrate_enabled = false};
  }
  if (!state.service_ready) {
    const std::string title = state.service_failed ? "Service failed to start"
                              : state.service_starting ? "Facepalm starting"
                                                       : "Facepalm unavailable";
    return MenuState{
        .camera_status_title = title,
        .preview_title = state.preview_open ? "Hide Camera Preview" : title,
        .preview_enabled = state.preview_open,
        .calibrate_title = title,
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
