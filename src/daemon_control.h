#ifndef FACEPALM_SRC_DAEMON_CONTROL_H_
#define FACEPALM_SRC_DAEMON_CONTROL_H_

#include <functional>

#include "src/camera_controller.h"

namespace facepalm {

// All calls run on the daemon's control queue.
class DaemonController {
 public:
  virtual ~DaemonController() = default;
  virtual void Subscribe() = 0;
  virtual void Leave() = 0;
  virtual void Revoke() = 0;
  virtual void Opened() = 0;
  virtual void OpenFailed() = 0;
  virtual void Closed() = 0;
  virtual void DeviceLost() = 0;
  virtual void DeviceBack() = 0;
  [[nodiscard]] virtual CameraController::State State() const = 0;
};

class DaemonPipeline {
 public:
  virtual ~DaemonPipeline() = default;
  virtual void Open() = 0;
  virtual void Close(std::function<void()> done) = 0;
};

// Joins the existing controller and pipeline. Reopen and process exit wait
// for the preceding analyser to be destroyed on the inference queue.
class DaemonControl : public CameraReports {
 public:
  DaemonControl(DaemonController& controller, DaemonPipeline& pipeline,
                std::function<void()> stop_service,
                std::function<void()> stopped);
  void Subscription(bool joined);
  void Stop();
  void OnOpened() override;
  void OnOpenFailed() override;
  void OnClosed() override;
  void OnDeviceLost() override;
  void OnDeviceBack() override;

 private:
  void Retire(bool acknowledge_close);
  DaemonController& controller_;
  DaemonPipeline& pipeline_;
  std::function<void()> stop_service_;
  std::function<void()> stopped_;
  bool stopping_ = false;
  bool retiring_ = false;
  bool device_back_ = false;
};
}  // namespace facepalm
#endif  // FACEPALM_SRC_DAEMON_CONTROL_H_
