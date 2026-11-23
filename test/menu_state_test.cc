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

// Disabled, so a window that was somehow still open when the service
// stopped is not offered as something to click "Hide" on; it closes on
// its own next refresh once it sees the camera gone.
TEST(DecideMenuStateTest, ServiceOffWithThePreviewStillMarkedOpen) {
  const MenuState state = DecideMenuState(
      {.service_on = false, .camera_in_use = true, .preview_open = true});

  EXPECT_EQ(state.preview_title, "Turn on Facepalm first");
  EXPECT_FALSE(state.preview_enabled);
}

TEST(DecideMenuStateTest, ServiceOffWithCameraIdleAndPreviewMarkedOpen) {
  const MenuState state = DecideMenuState(
      {.service_on = false, .camera_in_use = false, .preview_open = true});

  EXPECT_EQ(state.camera_status_title, "Facepalm off");
  EXPECT_FALSE(state.preview_enabled);
  EXPECT_FALSE(state.calibrate_enabled);
}

TEST(DecideMenuStateTest, ServiceOnWithCameraIdleAndPreviewClosed) {
  const MenuState state = DecideMenuState(
      {.service_on = true, .camera_in_use = false, .preview_open = false});

  EXPECT_EQ(state.camera_status_title, "Camera off");
  EXPECT_EQ(state.preview_title, "Show Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_EQ(state.calibrate_title, "Calibrate…");
  EXPECT_TRUE(state.calibrate_enabled);
}

TEST(DecideMenuStateTest, ServiceOnWithCameraIdleAndPreviewOpen) {
  const MenuState state = DecideMenuState(
      {.service_on = true, .camera_in_use = false, .preview_open = true});

  EXPECT_EQ(state.camera_status_title, "Camera off");
  EXPECT_EQ(state.preview_title, "Hide Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_TRUE(state.calibrate_enabled);
}

TEST(DecideMenuStateTest, ServiceOnWithCameraRunningAndPreviewClosed) {
  const MenuState state = DecideMenuState(
      {.service_on = true, .camera_in_use = true, .preview_open = false});

  EXPECT_EQ(state.camera_status_title, "Camera on");
  EXPECT_EQ(state.preview_title, "Show Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_TRUE(state.calibrate_enabled);
}

// The preview itself is why the camera is running (#176): still "Camera
// on", "Hide Camera Preview", and Calibrate… untouched by it.
TEST(DecideMenuStateTest, ServiceOnWithCameraRunningAndPreviewOpen) {
  const MenuState state = DecideMenuState(
      {.service_on = true, .camera_in_use = true, .preview_open = true});

  EXPECT_EQ(state.camera_status_title, "Camera on");
  EXPECT_EQ(state.preview_title, "Hide Camera Preview");
  EXPECT_TRUE(state.preview_enabled);
  EXPECT_EQ(state.calibrate_title, "Calibrate…");
  EXPECT_TRUE(state.calibrate_enabled);
}

}  // namespace
}  // namespace facepalm
