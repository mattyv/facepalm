#include "src/session_camera.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace facepalm {

SessionCamera::SessionCamera(CaptureSession& capture, CameraReports& reports,
                             Post session, Post control, int frames_per_second)
    : capture_(capture),
      reports_(reports),
      session_(std::move(session)),
      control_(std::move(control)),
      frames_per_second_(frames_per_second) {}

// specs/CameraLifecycle.tla, Open: asks for permission the first time, then
// starts. Denied permission is the spec's OpenFailed.
void SessionCamera::Open() {
  session_([this] {
    const CaptureSession::Permission permission = capture_.CurrentPermission();
    if (permission == CaptureSession::Permission::kGranted) {
      Start();
      return;
    }
    if (permission == CaptureSession::Permission::kDenied) {
      Report(&CameraReports::OnOpenFailed);
      return;
    }
    // Undetermined: ask. The answer arrives on any thread, so starting goes
    // back to the session queue.
    capture_.RequestPermission([this](bool granted) {
      session_([this, granted] {
        if (granted) {
          Start();
        } else {
          Report(&CameraReports::OnOpenFailed);
        }
      });
    });
  });
}

// specs/CameraLifecycle.tla, Closed follows once the session has stopped.
void SessionCamera::Close() {
  session_([this] {
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

// specs/CameraLifecycle.tla, DeviceBack, for the camera in use only.
void SessionCamera::OnDeviceConnected(std::string id) {
  session_([this, id = std::move(id)] {
    if (id == device_id_) {
      Report(&CameraReports::OnDeviceBack);
    }
  });
}

// specs/CameraLifecycle.tla, Opened when the session runs, OpenFailed when
// there is no camera to use or it does not start. Asks for the supported rate
// nearest the one wanted.
void SessionCamera::Start() {
  bool running = false;
  const std::optional<std::string> device = capture_.Configure();
  if (device.has_value()) {
    device_id_ = *device;
    // A camera that reports no rates is asked for the wanted one as it is.
    const CaptureSession::FrameRates rates = capture_.SupportedFrameRates();
    const int rate = rates.max == 0
                         ? frames_per_second_
                         : std::clamp(frames_per_second_, rates.min, rates.max);
    running = capture_.Start(rate);
  }
  Report(running ? &CameraReports::OnOpened : &CameraReports::OnOpenFailed);
}

void SessionCamera::Report(void (CameraReports::*report)()) {
  control_([this, report] { (reports_.*report)(); });
}

}  // namespace facepalm
