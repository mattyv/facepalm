#include "src/calibration_reader.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/calibration.h"
#include "src/camera_controller.h"
#include "src/eye_gaze.h"
#include "src/face_model.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

using ::testing::DoubleNear;
using ::testing::IsEmpty;

constexpr double kDegree = std::numbers::pi / 180;

// A face whose transformation matrix is turned `yaw` degrees, as
// LookAwayReaderTest's FaceAt is (src/head_pose.cc's HeadRotationFrom
// order).
Face FaceAt(double yaw) {
  const double cy = std::cos(yaw * kDegree);
  const double sy = std::sin(yaw * kDegree);
  const std::array<std::array<double, 3>, 3> rows = {
      {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}};
  Face face{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      face.pose_column_major[(column * 4) + row] =
          static_cast<float>(rows.at(row).at(column));
    }
  }
  face.pose_column_major[15] = 1;
  return face;
}

class MockCalibrationController : public CalibrationController {
 public:
  MOCK_METHOD(std::optional<Status>, Start, (int, std::string&), (override));
  MOCK_METHOD(std::optional<Status>, Next, (std::string&), (override));
  MOCK_METHOD(bool, Cancel, (std::string&), (override));
  MOCK_METHOD(void, Update, (const HeadRotation&, const EyeSignal&),
              (override));
  MOCK_METHOD(void, OnCameraLost, (), (override));
  MOCK_METHOD(void, OnCameraUp, (), (override));
};

constexpr double kBlinkThreshold = 0.5;

class CalibrationReaderTest : public ::testing::Test {
 protected:
  testing::NiceMock<MockCalibrationController> controller_;
  CalibrationReader reader_{controller_, kBlinkThreshold};
  std::vector<Reaction> reactions_;
};

TEST_F(CalibrationReaderTest, PassesTheFacesPoseToTheController) {
  EXPECT_CALL(controller_, Update)
      .WillOnce([](const HeadRotation& pose, const EyeSignal&) {
        EXPECT_THAT(pose.yaw_degrees, DoubleNear(30, 0.001));
      });

  reader_.OnFace(FaceAt(30), /*width=*/640, /*height=*/480, /*timestamp_ms=*/0,
                 reactions_);

  EXPECT_THAT(reactions_, IsEmpty());
}

TEST_F(CalibrationReaderTest, PassesTheFacesEyeSignalToTheController) {
  Face face = FaceAt(0);
  face.blendshapes[9] = 0.9F;   // eyeBlinkLeft.
  face.blendshapes[10] = 0.9F;  // eyeBlinkRight: a clear blink.
  EXPECT_CALL(controller_, Update)
      .WillOnce([](const HeadRotation&, const EyeSignal& eye) {
        EXPECT_TRUE(eye.blink);
      });

  reader_.OnFace(face, 640, 480, 0, reactions_);
}

TEST_F(CalibrationReaderTest, ReportsNoReactionOfItsOwn) {
  EXPECT_CALL(controller_, Update).Times(1);

  reader_.OnFace(FaceAt(0), 640, 480, 0, reactions_);

  EXPECT_THAT(reactions_, IsEmpty());
}

// A face whose transformation matrix has no rotation to read (#44) is
// skipped, as LookAwayReader's is.
TEST_F(CalibrationReaderTest, ADegeneratePoseIsSkipped) {
  EXPECT_CALL(controller_, Update).Times(0);

  reader_.OnFace(Face{}, 640, 480, 0, reactions_);
}

TEST_F(CalibrationReaderTest, OnNoFaceDropsNothingOfItsOwn) {
  EXPECT_CALL(controller_, Update).Times(0);

  reader_.OnNoFace();
}

}  // namespace
}  // namespace facepalm
