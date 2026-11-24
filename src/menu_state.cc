#include "src/menu_state.h"

#include <charconv>

namespace facepalm {

std::optional<ManagerSnapshot> ParseManagerSnapshot(std::string_view output) {
  ManagerSnapshot snapshot;
  bool have_pid = false;
  bool have_state = false;
  while (!output.empty()) {
    const auto end = output.find('\n');
    if (end == std::string_view::npos) {
      return std::nullopt;
    }
    const auto line = output.substr(0, end);
    output.remove_prefix(end + 1);
    if (line.starts_with("ExecMainPID=") && !have_pid) {
      const auto value = line.substr(sizeof("ExecMainPID=") - 1);
      const auto result = std::from_chars(
          value.data(), value.data() + value.size(), snapshot.pid);
      if (result.ec != std::errc{} ||
          result.ptr != value.data() + value.size() || snapshot.pid < 0) {
        return std::nullopt;
      }
      have_pid = true;
    } else if (line.starts_with("ActiveState=") && !have_state) {
      const auto value = line.substr(sizeof("ActiveState=") - 1);
      if (value.empty()) {
        return std::nullopt;
      }
      snapshot.state = value;
      have_state = true;
    } else {
      return std::nullopt;
    }
  }
  if (!have_pid || !have_state) {
    return std::nullopt;
  }
  return snapshot;
}

namespace {
std::string_view ReasonText(ServiceReason reason) {
  switch (reason) {
    case ServiceReason::kNone:
      return "unknown reason";
    case ServiceReason::kNoCandidate:
      return "no camera available";
    case ServiceReason::kPermissionDenied:
      return "permission denied";
    case ServiceReason::kQueryFailed:
      return "camera query failed";
    case ServiceReason::kUnsupported:
      return "camera unsupported";
    case ServiceReason::kStartFailed:
      return "camera could not start";
    case ServiceReason::kModelFailure:
      return "face model failed";
  }
  return "unknown reason";
}
}  // namespace

bool HealthMatchesManager(std::int64_t health_pid, std::int64_t manager_pid,
                          std::string_view active_state) {
  return health_pid > 0 && health_pid == manager_pid &&
         (active_state == "active" || active_state == "activating" ||
          active_state == "failed");
}

CameraHealthView DescribeCameraHealth(const DaemonHealth& health,
                                      bool identity_verified) {
  if (!identity_verified) {
    if (health.reason == ServiceReason::kNone ||
        (health.phase != ServicePhase::kRetrying &&
         health.phase != ServicePhase::kFailed)) {
      return {};
    }
    return {.title = "Last camera failure: " +
                     std::string(ReasonText(health.reason)),
            .keep_preview = false};
  }
  switch (health.phase) {
    case ServicePhase::kIdle:
      return {.title = "Camera idle", .verified = true};
    case ServicePhase::kOpening:
      return {.title = "Camera opening",
              .keep_preview = health.alive,
              .verified = true};
    case ServicePhase::kActive:
      return {.title = "Camera on", .active = true, .verified = true};
    case ServicePhase::kRetrying:
      return {
          .title = "Camera retrying: " + std::string(ReasonText(health.reason)),
          .keep_preview = health.alive,
          .verified = true};
    case ServicePhase::kFailed:
      return {.title = "Camera service failed: " +
                       std::string(ReasonText(health.reason)),
              .verified = true};
  }
  return {};
}

ServiceHealth ParseServiceHealth(std::string_view active_state) {
  if (active_state == "failed\n") {
    return ServiceHealth::kFailed;
  }
  if (active_state == "activating\n") {
    return ServiceHealth::kStarting;
  }
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
    std::string title = "Facepalm unavailable";
    if (state.service_failed) {
      title = "Service failed to start";
    } else if (state.service_starting) {
      title = "Facepalm starting";
    }
    return MenuState{
        .camera_status_title = state.camera_status_override.empty()
                                   ? title
                                   : state.camera_status_override,
        .preview_title = state.preview_open ? "Hide Camera Preview" : title,
        .preview_enabled = state.preview_open,
        .calibrate_title = title,
        .calibrate_enabled = false};
  }
  const std::string camera_title =
      state.camera_in_use ? "Camera on" : "Camera off";
  return MenuState{.camera_status_title = state.camera_status_override.empty()
                                              ? camera_title
                                              : state.camera_status_override,
                   .preview_title = state.preview_open ? "Hide Camera Preview"
                                                       : "Show Camera Preview",
                   .preview_enabled = true,
                   .calibrate_title = "Calibrate…",
                   .calibrate_enabled = true};
}

}  // namespace facepalm
