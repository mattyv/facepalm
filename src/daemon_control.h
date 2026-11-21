#ifndef FACEPALM_SRC_DAEMON_CONTROL_H_
#define FACEPALM_SRC_DAEMON_CONTROL_H_

#include <functional>

#include "src/camera_controller.h"

namespace facepalm {

// The camera controller, as the daemon drives it. All calls run on the
// daemon's control queue.
class DaemonController {
 public:
  virtual ~DaemonController() = default;
  virtual void Subscribe() = 0;
  virtual void Leave() = 0;
  virtual void Revoke() = 0;
  virtual void Opened() = 0;
  virtual void OpenFailed(OpenFailure reason) = 0;
  virtual void Closed() = 0;
  virtual void DeviceLost() = 0;
  virtual void DeviceBack() = 0;
  [[nodiscard]] virtual CameraController::State State() const = 0;
};

// The frame pipeline's face model, as the daemon drives it.
class DaemonPipeline {
 public:
  virtual ~DaemonPipeline() = default;
  // Creates the face model on the inference queue.
  virtual void Open() = 0;
  // Destroys the face model on the inference queue, then calls `done` on the
  // control queue.
  virtual void Close(std::function<void()> done) = 0;
};

// The daemon's control logic (#37, #63), following specs/DaemonAdmission.tla:
// it joins the camera controller and the frame pipeline, so a camera reopens
// and the daemon exits only once the previous face model is destroyed. Each
// method names the spec action it implements. Call it from the control
// queue only.
class DaemonControl : public CameraReports {
 public:
  // `stop_service` stops the socket service, which disconnects every client;
  // `exit` ends the process, told whether it stopped as a failure.
  DaemonControl(DaemonController& controller, DaemonPipeline& pipeline,
                std::function<void()> stop_service,
                std::function<void(bool failed)> exit);
  void Subscription(bool joined);
  // Stops the daemon: the person turned Facepalm off, or a signal came.
  void Stop();
  // Stops the daemon as a failure, as when the face model failed.
  void Fail();
  void OnOpened() override;
  void OnOpenFailed(OpenFailure reason) override;
  void OnClosed() override;
  void OnDeviceLost() override;
  void OnDeviceBack() override;

 private:
  void Retire(bool acknowledge_close);
  DaemonController& controller_;
  DaemonPipeline& pipeline_;
  std::function<void()> stop_service_;
  std::function<void(bool)> exit_;
  bool failed_ = false;
  bool stopping_ = false;
  bool retiring_ = false;
  bool device_back_ = false;
};
}  // namespace facepalm
#endif  // FACEPALM_SRC_DAEMON_CONTROL_H_
