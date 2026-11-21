#include "src/session_camera.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace facepalm {
namespace {

// The rate in `range` nearest `wanted`.
int Nearest(int wanted, const CaptureSession::FrameRateRange& range) {
  return std::clamp(wanted, range.min, range.max);
}

// The rate nearest `wanted` that one of `rates`' ranges supports, or
// `wanted` as it is when the camera reports no ranges (#43): collapsing
// them into one overall minimum and maximum let `Start` be asked for a
// rate none of the camera's actual ranges covered.
int NearestSupportedRate(int wanted, const CaptureSession::FrameRates& rates) {
  if (rates.empty()) {
    return wanted;
  }
  int best = Nearest(wanted, rates.front());
  for (const CaptureSession::FrameRateRange& range : rates) {
    const int candidate = Nearest(wanted, range);
    if (std::abs(candidate - wanted) < std::abs(best - wanted)) {
      best = candidate;
    }
  }
  return best;
}

}  // namespace

SessionCamera::SessionCamera(CaptureSession& capture, CameraReports& reports,
                             Post session, Post control, int frames_per_second)
    : capture_(capture),
      reports_(reports),
      session_(std::move(session)),
      control_(std::move(control)),
      frames_per_second_(frames_per_second) {}

// specs/CameraLifecycle.tla, Open: asks for permission the first time, then
// starts. Denied permission is the spec's OpenFailed with `~permitted`
// (#52).
void SessionCamera::Open(std::uint64_t attempt) {
  session_([this, attempt] {
    attempt_ = attempt;
    const CaptureSession::Permission permission = capture_.CurrentPermission();
    if (permission == CaptureSession::Permission::kGranted) {
      Start(attempt);
      return;
    }
    if (permission == CaptureSession::Permission::kDenied) {
      ReportOpenFailed(OpenFailure::kPermissionDenied);
      return;
    }
    // Undetermined: ask. The answer arrives on any thread, so starting goes
    // back to the session queue.
    capture_.RequestPermission([this, attempt](bool granted) {
      session_([this, attempt, granted] {
        // Start, with GuardStartChecksAttempt: an answer for an attempt that
        // was closed meanwhile starts nothing and reports nothing (#38).
        if (attempt != attempt_) {
          return;
        }
        if (granted) {
          Start(attempt);
        } else {
          ReportOpenFailed(OpenFailure::kPermissionDenied);
        }
      });
    });
  });
}

// specs/CameraLifecycle.tla, Closed follows once the session has stopped.
// The attempt ends first, so a permission answer still to come is void.
void SessionCamera::Close() {
  session_([this] {
    attempt_ = kNoAttempt;
    capture_.Stop();
    Report(&CameraReports::OnClosed);
  });
}

// specs/CameraLifecycle.tla, DeviceLost: the session failed, so it is
// stopped, and the controller treats the camera as closed.
void SessionCamera::OnSessionFailed() {
  session_([this] {
    capture_.Stop();
    Report(&CameraReports::OnDeviceLost);
  });
}

// specs/CameraLifecycle.tla, DeviceLost, for the camera in use only.
void SessionCamera::OnDeviceDisconnected(std::string id) {
  session_([this, id = std::move(id)] {
    if (id == device_id_) {
      capture_.Stop();
      Report(&CameraReports::OnDeviceLost);
    }
  });
}

// specs/CameraLifecycle.tla, DeviceBack, for the camera in use, or any camera
// while none is (GuardDiscoverAnyCamera, #39).
void SessionCamera::OnDeviceConnected(std::string id) {
  session_([this, id = std::move(id)] {
    if (id == device_id_ || device_id_.empty()) {
      Report(&CameraReports::OnDeviceBack);
    }
  });
}

// specs/CameraLifecycle.tla, Opened when the session runs; OpenFailed with
// `~device` (no camera to use, #13) or `~startWorks` (it does not start,
// #52). Asks for the supported rate nearest the one wanted, from the
// camera's actual ranges (#43).
void SessionCamera::Start(std::uint64_t attempt) {
  const std::optional<std::string> device = capture_.Configure();
  if (!device.has_value()) {
    ReportOpenFailed(OpenFailure::kNoDevice);
    return;
  }
  device_id_ = *device;
  const int rate =
      NearestSupportedRate(frames_per_second_, capture_.SupportedFrameRates());
  if (!capture_.Start(rate, attempt)) {
    ReportOpenFailed(OpenFailure::kStartFailed);
    return;
  }
  Report(&CameraReports::OnOpened);
}

void SessionCamera::Report(void (CameraReports::*report)()) {
  control_([this, report] { (reports_.*report)(); });
}

void SessionCamera::ReportOpenFailed(OpenFailure reason) {
  control_([this, reason] { reports_.OnOpenFailed(reason); });
}

}  // namespace facepalm
