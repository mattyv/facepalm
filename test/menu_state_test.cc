#include "src/menu_state.h"

#include "gtest/gtest.h"

// Each test stands for a row of docs/local-app.md's per-item state table
// (#176): every combination of (service on, camera in use, preview open).

namespace facepalm {
namespace {

TEST(DecideMenuStateTest, ServiceOffWithCameraIdleAndPreviewClosed) {
  const MenuState state = DecideMenuState(
      {.service_on = false, .camera_in_use = false, .preview_open = false});

  EXPECT_EQ(state.camera_status_title, "Facepalm off");
  EXPECT_EQ(state.preview_title, "Turn on Facepalm first");
  EXPECT_FALSE(state.preview_enabled);
  EXPECT_EQ(state.calibrate_title, "Turn on Facepalm first");
  EXPECT_FALSE(state.calibrate_enabled);
}

// A stale camera-in-use reading left over from before the service
// stopped never contradicts "Facepalm off": the service being off is
// what decides the line, not a status file nothing is publishing to
// any more.
TEST(DecideMenuStateTest, ServiceOffWithACameraInUseReadingStillStale) {
  const MenuState state = DecideMenuState(
      {.service_on = false, .camera_in_use = true, .preview_open = false});

  EXPECT_EQ(state.camera_status_title, "Facepalm off");
  EXPECT_FALSE(state.preview_enabled);
  EXPECT_FALSE(state.calibrate_enabled);
}

// A preview already open can still be hidden after the service stops.
TEST(DecideMenuStateTest, ServiceOffWithThePreviewStillMarkedOpen) {
  const MenuState state = DecideMenuState(
      {.service_on = false, .camera_in_use = true, .preview_open = true});

  EXPECT_EQ(state.preview_title, "Hide Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
}

TEST(DecideMenuStateTest, ServiceOffWithCameraIdleAndPreviewMarkedOpen) {
  const MenuState state = DecideMenuState(
      {.service_on = false, .camera_in_use = false, .preview_open = true});

  EXPECT_EQ(state.camera_status_title, "Facepalm off");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_FALSE(state.calibrate_enabled);
}

TEST(DecideMenuStateTest, ServiceOnWithCameraIdleAndPreviewClosed) {
  const MenuState state = DecideMenuState({.service_on = true,
                                           .service_ready = true,
                                           .camera_in_use = false,
                                           .preview_open = false});

  EXPECT_EQ(state.camera_status_title, "Camera off");
  EXPECT_EQ(state.preview_title, "Show Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_EQ(state.calibrate_title, "Calibrate…");
  EXPECT_TRUE(state.calibrate_enabled);
}

TEST(DecideMenuStateTest, ServiceOnWithCameraIdleAndPreviewOpen) {
  const MenuState state = DecideMenuState({.service_on = true,
                                           .service_ready = true,
                                           .camera_in_use = false,
                                           .preview_open = true});

  EXPECT_EQ(state.camera_status_title, "Camera off");
  EXPECT_EQ(state.preview_title, "Hide Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_TRUE(state.calibrate_enabled);
}

TEST(DecideMenuStateTest, ServiceOnWithCameraRunningAndPreviewClosed) {
  const MenuState state = DecideMenuState({.service_on = true,
                                           .service_ready = true,
                                           .camera_in_use = true,
                                           .preview_open = false});

  EXPECT_EQ(state.camera_status_title, "Camera on");
  EXPECT_EQ(state.preview_title, "Show Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_TRUE(state.calibrate_enabled);
}

// The preview itself is why the camera is running (#176): still "Camera
// on", "Hide Camera Preview", and Calibrate… untouched by it.
TEST(DecideMenuStateTest, ServiceOnWithCameraRunningAndPreviewOpen) {
  const MenuState state = DecideMenuState({.service_on = true,
                                           .service_ready = true,
                                           .camera_in_use = true,
                                           .preview_open = true});

  EXPECT_EQ(state.camera_status_title, "Camera on");
  EXPECT_EQ(state.preview_title, "Hide Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_EQ(state.calibrate_title, "Calibrate…");
  EXPECT_TRUE(state.calibrate_enabled);
}

}  // namespace
}  // namespace facepalm

namespace facepalm {
namespace {
TEST(DecideMenuStateTest, FailedServiceIsNotReady) {
  const auto state = DecideMenuState(
      {.service_on = true, .service_ready = false, .service_failed = true});
  EXPECT_EQ(state.camera_status_title, "Service failed to start");
  EXPECT_FALSE(state.preview_enabled);
  EXPECT_FALSE(state.calibrate_enabled);
}
TEST(DecideMenuStateTest, StartingServiceIsNotReady) {
  const auto state = DecideMenuState(
      {.service_on = true, .service_ready = false, .service_starting = true});
  EXPECT_EQ(state.camera_status_title, "Facepalm starting");
  EXPECT_FALSE(state.preview_enabled);
  EXPECT_FALSE(state.calibrate_enabled);
}
TEST(DecideMenuStateTest, UnavailableServiceAllowsHidingPreview) {
  const auto state = DecideMenuState(
      {.service_on = true, .service_ready = false, .preview_open = true});
  EXPECT_EQ(state.camera_status_title, "Facepalm unavailable");
  EXPECT_EQ(state.preview_title, "Hide Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_FALSE(state.calibrate_enabled);
}
}  // namespace
}  // namespace facepalm

namespace facepalm {
namespace {
TEST(ServiceManagerHealthTest, ParsesOnlyAuthoritativeActiveState) {
  EXPECT_EQ(ParseServiceHealth("failed\n"), ServiceHealth::kFailed);
  EXPECT_EQ(ParseServiceHealth("activating\n"), ServiceHealth::kStarting);
  EXPECT_EQ(ParseServiceHealth("active\n"), ServiceHealth::kUnavailable);
  EXPECT_EQ(ParseServiceHealth("failed stale\n"), ServiceHealth::kUnavailable);
  EXPECT_EQ(ParseServiceHealth(""), ServiceHealth::kUnavailable);
}
}  // namespace
}  // namespace facepalm

namespace facepalm {
namespace {
TEST(DecideMenuStateTest, ReachableSocketWinsOverStaleFailure) {
  const auto state = DecideMenuState(
      {.service_on = true, .service_ready = true, .service_failed = true});
  EXPECT_EQ(state.camera_status_title, "Camera off");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_TRUE(state.calibrate_enabled);
}
}  // namespace
}  // namespace facepalm
