#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "src/camera_controller.h"
#include "src/daemon_control.h"

namespace facepalm {
namespace {
class Device : public Camera {
 public:
  void Open(std::uint64_t attempt) override {
    (void)attempt;
    ++opens;
  }
  void Close() override {}
  int opens = 0;
};
class ClientSink : public Clients {
 public:
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): implements Clients.
  void Deliver(const ClientId& client, const Reaction& reaction) override {
    (void)client;
    (void)reaction;
  }
};
class ControllerAdapter : public DaemonController {
 public:
  explicit ControllerAdapter(CameraController& controller) : c(controller) {}
  void Subscribe() override { c.Subscribe("client"); }
  void Leave() override { c.Leave("client"); }
  void Revoke() override { c.RevokeConsent(); }
  void Opened() override { c.OnOpened(); }
  void OpenFailed(OpenFailure reason,
                  std::optional<ConfigureError> detail) override {
    c.OnOpenFailed(reason, detail);
  }
  void Closed() override { c.OnClosed(); }
  void DeviceLost() override { c.OnDeviceLost(); }
  void DeviceBack() override { c.OnDeviceBack(); }
  [[nodiscard]] CameraController::State State() const override {
    return c.CurrentState();
  }
  CameraController& c;
};
class Pipeline : public DaemonPipeline {
 public:
  void Open() override { ++opens; }
  void Close(std::function<void()> done) override { retired = std::move(done); }
  int opens = 0;
  std::function<void()> retired;
};
// NOLINTBEGIN(readability-function-cognitive-complexity): one ordered trace.
TEST(CameraRecoveryIntegration, RetryCannotOvertakeModelRetirement) {
  Device device;
  ClientSink clients;
  std::function<void()> timer;
  CameraController controller(
      device, clients, true,
      [&timer](std::chrono::seconds, std::function<void()> callback) {
        timer = std::move(callback);
      });
  ControllerAdapter adapter(controller);
  Pipeline pipeline;
  DaemonControl daemon(
      adapter, pipeline, [] {}, [](const std::optional<std::string>&) {});
  daemon.Subscription(true);
  daemon.OnOpenFailed(OpenFailure::kNoDevice, std::nullopt);
  ASSERT_TRUE(timer);
  daemon.OnDeviceLost();  // Already closed; there is no model to retire.
  EXPECT_FALSE(pipeline.retired);
  const auto stale = timer;
  timer();
  EXPECT_EQ(device.opens, 2);
  daemon.OnOpened();
  EXPECT_EQ(pipeline.opens, 1);
  daemon.OnDeviceLost();
  ASSERT_TRUE(pipeline.retired);
  daemon.OnDeviceBack();
  stale();
  EXPECT_EQ(device.opens, 2);
  pipeline.retired();
  EXPECT_EQ(device.opens, 3);
}
TEST(CameraRecoveryIntegration,
     DemandChangesWaitForRetirementAndRetryAfterLoss) {
  Device device;
  ClientSink clients;
  std::function<void()> timer;
  CameraController controller(
      device, clients, true,
      [&timer](std::chrono::seconds, std::function<void()> callback) {
        timer = std::move(callback);
      });
  ControllerAdapter adapter(controller);
  Pipeline pipeline;
  DaemonControl daemon(
      adapter, pipeline, [] {}, [](const std::optional<std::string>&) {});
  daemon.Subscription(true);
  daemon.OnOpened();
  ASSERT_EQ(device.opens, 1);
  daemon.OnDeviceLost();
  ASSERT_TRUE(pipeline.retired);
  daemon.Subscription(false);
  daemon.Subscription(true);
  controller.RevokeConsent();
  controller.GrantConsent();
  daemon.OnDeviceLost();
  EXPECT_EQ(device.opens, 1);
  pipeline.retired();
  ASSERT_TRUE(timer);
  EXPECT_EQ(device.opens, 1);
  timer();
  EXPECT_EQ(device.opens, 2);
  const auto stale = timer;
  stale();
  EXPECT_EQ(device.opens, 2);
}

TEST(CameraRecoveryIntegration, DeviceBackDuringRetirementIsDeferred) {
  Device device;
  ClientSink clients;
  std::function<void()> timer;
  CameraController controller(
      device, clients, true,
      [&timer](std::chrono::seconds, std::function<void()> callback) {
        timer = std::move(callback);
      });
  ControllerAdapter adapter(controller);
  Pipeline pipeline;
  DaemonControl daemon(
      adapter, pipeline, [] {}, [](const std::optional<std::string>&) {});
  daemon.Subscription(true);
  daemon.OnOpened();
  daemon.OnDeviceLost();
  ASSERT_TRUE(pipeline.retired);
  daemon.OnDeviceBack();
  EXPECT_EQ(device.opens, 1);
  pipeline.retired();
  EXPECT_EQ(device.opens, 2);
  ASSERT_TRUE(timer);
  timer();
  EXPECT_EQ(device.opens, 2);
}
TEST(CameraRecoveryIntegration, DemandAfterRetirementArmsNewRetry) {
  Device device;
  ClientSink clients;
  std::function<void()> timer;
  CameraController controller(
      device, clients, true,
      [&timer](std::chrono::seconds, std::function<void()> callback) {
        timer = std::move(callback);
      });
  ControllerAdapter adapter(controller);
  Pipeline pipeline;
  DaemonControl daemon(
      adapter, pipeline, [] {}, [](const std::optional<std::string>&) {});
  daemon.Subscription(true);
  daemon.OnOpened();
  daemon.OnDeviceLost();
  ASSERT_TRUE(pipeline.retired);
  daemon.Subscription(false);
  pipeline.retired();
  EXPECT_FALSE(timer);
  daemon.Subscription(true);
  ASSERT_TRUE(timer);
  EXPECT_EQ(device.opens, 1);
  timer();
  EXPECT_EQ(device.opens, 2);
}
// NOLINTEND(readability-function-cognitive-complexity)
}  // namespace
}  // namespace facepalm
