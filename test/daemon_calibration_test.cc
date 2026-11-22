#include "src/daemon_calibration.h"

#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/calibration.h"
#include "src/eye_gaze.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

class MockCalibrationController : public CalibrationController {
 public:
  MOCK_METHOD(std::optional<Status>, Start, (int, std::string&), (override));
  MOCK_METHOD(std::optional<Status>, Next, (std::string&), (override));
  MOCK_METHOD(bool, Cancel, (std::string&), (override));
  MOCK_METHOD(void, Update, (const HeadRotation&, const EyeSignal&), (override));
  MOCK_METHOD(void, OnCameraLost, (), (override));
  MOCK_METHOD(void, OnCameraUp, (), (override));
};

TEST(DaemonCalibrationTest, GivenControllerIsReturned) {
  MockCalibrationController controller;
  const DaemonCalibration calibration(&controller);

  EXPECT_EQ(calibration.Controller(), &controller);
}

TEST(DaemonCalibrationTest, NoControllerGivenIsUnavailable) {
  const DaemonCalibration calibration(nullptr);

  EXPECT_EQ(calibration.Controller(), nullptr);
}

TEST(DaemonCalibrationTest, ForwardsCameraLostToTheController) {
  MockCalibrationController controller;
  DaemonCalibration calibration(&controller);

  EXPECT_CALL(controller, OnCameraLost()).Times(1);
  calibration.OnCameraLost();
}

TEST(DaemonCalibrationTest, ForwardsCameraUpToTheController) {
  MockCalibrationController controller;
  DaemonCalibration calibration(&controller);

  EXPECT_CALL(controller, OnCameraUp()).Times(1);
  calibration.OnCameraUp();
}

TEST(DaemonCalibrationTest, CameraLostIsANoOpWithNoControllerGiven) {
  DaemonCalibration calibration(nullptr);

  calibration.OnCameraLost();  // Must not crash.
}

TEST(DaemonCalibrationTest, CameraUpIsANoOpWithNoControllerGiven) {
  DaemonCalibration calibration(nullptr);

  calibration.OnCameraUp();  // Must not crash.
}

}  // namespace
}  // namespace facepalm
