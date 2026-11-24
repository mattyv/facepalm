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

namespace facepalm {
namespace {
TEST(CameraHealthTest, VerifiedRetryKeepsPreviewAndNamesReason) {
  const DaemonHealth health{.phase = ServicePhase::kRetrying,
                            .reason = ServiceReason::kPermissionDenied,
                            .detail = "",
                            .pid = 42,
                            .alive = true};
  const auto view = DescribeCameraHealth(health, true);
  EXPECT_EQ(view.title, "Camera retrying: permission denied");
  EXPECT_TRUE(view.keep_preview);
}
TEST(CameraHealthTest, UnverifiedFailureIsHistoricalAndNeverCurrent) {
  const DaemonHealth health{.phase = ServicePhase::kFailed,
                            .reason = ServiceReason::kNoCandidate,
                            .detail = "",
                            .pid = 42,
                            .alive = false};
  const auto view = DescribeCameraHealth(health, false);
  EXPECT_EQ(view.title, "Last camera failure: no camera available");
  EXPECT_FALSE(view.keep_preview);
}
TEST(CameraHealthTest, CurrentPhasesAreDistinctAndRecoveryClearsReason) {
  DaemonHealth health{.phase = ServicePhase::kOpening,
                      .reason = ServiceReason::kNone,
                      .detail = "",
                      .pid = 42,
                      .alive = true};
  EXPECT_EQ(DescribeCameraHealth(health, true).title, "Camera opening");
  health.phase = ServicePhase::kIdle;
  EXPECT_EQ(DescribeCameraHealth(health, true).title, "Camera idle");
  health.phase = ServicePhase::kActive;
  health.reason = ServiceReason::kPermissionDenied;
  EXPECT_EQ(DescribeCameraHealth(health, true).title, "Camera on");
  EXPECT_TRUE(DescribeCameraHealth(health, true).verified);
  EXPECT_FALSE(DescribeCameraHealth(health, false).verified);
}
TEST(CameraHealthTest, ManagerPidMustMatchTheSnapshot) {
  EXPECT_TRUE(HealthMatchesManager(42, 42, "active"));
  EXPECT_TRUE(HealthMatchesManager(42, 42, "activating"));
  EXPECT_TRUE(HealthMatchesManager(42, 42, "failed"));
  EXPECT_FALSE(HealthMatchesManager(42, 43, "active"));
  EXPECT_FALSE(HealthMatchesManager(42, 0, "failed"));
  EXPECT_FALSE(HealthMatchesManager(42, 42, "inactive"));
}
}  // namespace
}  // namespace facepalm

namespace facepalm {
namespace {
TEST(CameraHealthTest, ParsesOnlyCompleteManagerIdentitySnapshots) {
  const auto manager =
      ParseManagerSnapshot("ExecMainPID=42\nActiveState=active\n");
  ASSERT_TRUE(manager.has_value());
  EXPECT_EQ(manager->pid, 42);
  EXPECT_EQ(manager->state, "active");
  EXPECT_FALSE(ParseManagerSnapshot("ExecMainPID=42\n"));
  EXPECT_FALSE(ParseManagerSnapshot("ExecMainPID=42x\nActiveState=active\n"));
  EXPECT_FALSE(ParseManagerSnapshot(
      "ExecMainPID=42\nExecMainPID=43\nActiveState=active\n"));
  EXPECT_FALSE(
      ParseManagerSnapshot("ExecMainPID=42\nActiveState=active\nextra\n"));
}
}  // namespace
}  // namespace facepalm

namespace facepalm {
namespace {
TEST(CameraHealthTest, FailureReasonsHaveUsefulUserMessages) {
  DaemonHealth health{.phase = ServicePhase::kFailed,
                      .reason = ServiceReason::kNone,
                      .detail = "",
                      .pid = 42,
                      .alive = false};
  for (const auto& [reason, expected] :
       {std::pair{ServiceReason::kNone,
                  "Camera service failed: unknown reason"},
        std::pair{ServiceReason::kQueryFailed,
                  "Camera service failed: camera query failed"},
        std::pair{ServiceReason::kUnsupported,
                  "Camera service failed: camera unsupported"},
        std::pair{ServiceReason::kStartFailed,
                  "Camera service failed: camera could not start"},
        std::pair{ServiceReason::kModelFailure,
                  "Camera service failed: face model failed"},
        std::pair{static_cast<ServiceReason>(255),
                  "Camera service failed: unknown reason"}}) {
    health.reason = reason;
    EXPECT_EQ(DescribeCameraHealth(health, true).title, expected);
  }
  health.phase = static_cast<ServicePhase>(255);
  EXPECT_TRUE(DescribeCameraHealth(health, true).title.empty());
}
TEST(CameraHealthTest, UnverifiedNonFailureCannotInventCurrentStatus) {
  DaemonHealth health{.phase = ServicePhase::kIdle,
                      .reason = ServiceReason::kNone,
                      .detail = "",
                      .pid = 42,
                      .alive = true};
  EXPECT_TRUE(DescribeCameraHealth(health, false).title.empty());
  health.reason = ServiceReason::kPermissionDenied;
  EXPECT_TRUE(DescribeCameraHealth(health, false).title.empty());
  health.phase = ServicePhase::kRetrying;
  EXPECT_EQ(DescribeCameraHealth(health, false).title,
            "Last camera failure: permission denied");
  EXPECT_FALSE(HealthMatchesManager(0, 0, "active"));
}
TEST(CameraHealthTest, RejectsMalformedAndIncompleteManagerOutput) {
  EXPECT_FALSE(ParseManagerSnapshot("ActiveState=active\n"));
  EXPECT_FALSE(ParseManagerSnapshot("ExecMainPID=42\nActiveState=\n"));
  EXPECT_FALSE(ParseManagerSnapshot("ExecMainPID=xyz\nActiveState=active\n"));
  EXPECT_FALSE(ParseManagerSnapshot("ExecMainPID=-1\nActiveState=active\n"));
  EXPECT_FALSE(ParseManagerSnapshot("ExecMainPID=42\nActiveState=active"));
  EXPECT_FALSE(ParseManagerSnapshot(
      "ExecMainPID=42\nActiveState=active\nActiveState=failed\n"));
}
TEST(CameraHealthTest, MenuUsesVerifiedOrHistoricalCameraStatus) {
  EXPECT_EQ(DecideMenuState({.service_on = true,
                             .service_ready = false,
                             .camera_status_override =
                                 "Last camera failure: permission denied"})
                .camera_status_title,
            "Last camera failure: permission denied");
  EXPECT_EQ(DecideMenuState({.service_on = true,
                             .service_ready = true,
                             .camera_status_override = "Camera opening"})
                .camera_status_title,
            "Camera opening");
}
}  // namespace
}  // namespace facepalm
