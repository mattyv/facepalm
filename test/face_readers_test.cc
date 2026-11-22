#include "src/face_readers.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/calibration.h"
#include "src/camera_controller.h"
#include "src/config.h"
#include "src/expression_reader.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
#include "src/face_model.h"
#include "src/gesture_reader.h"
#include "src/head_pose.h"
#include "src/look_away_reader.h"
#include "src/screen_context_reader.h"

// The readers are pure computation, so the test makes them for real.
// Calibration is the one other exception, alongside screen context: its
// own test file covers CalibrationReader's forwarding
// (test/calibration_reader_test.cc), so here a mock is enough to show it
// is the controller given.

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::IsEmpty;
using ::testing::SizeIs;

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

TEST(FaceReadersTest, EveryReaderTheDefaultsEnableInOrder) {
  const std::vector<std::unique_ptr<FaceReader>> readers =
      MakeFaceReaders(Config{});

  ASSERT_THAT(readers, SizeIs(3));
  EXPECT_NE(dynamic_cast<GestureReader*>(readers[0].get()), nullptr);
  EXPECT_NE(dynamic_cast<ExpressionReader*>(readers[1].get()), nullptr);
  EXPECT_NE(dynamic_cast<LookAwayReader*>(readers[2].get()), nullptr);
}

TEST(FaceReadersTest, NoReaderTheConfigTurnsOff) {
  Config config;
  config.gestures_enabled = false;
  config.expressions_enabled = false;
  config.looking_away_enabled = false;

  EXPECT_THAT(MakeFaceReaders(config), IsEmpty());
}

// Screen context (#61 item 11): its own test file covers the forwarding
// itself (test/screen_context_reader_test.cc); here it is only wiring, so
// a real face through the last reader is enough to show the given
// ScreenContextReader is the one fed.
TEST(FaceReadersTest, ScreenContextReaderIsAppendedWhenGiven) {
  ScreenContextReader reader{ExpressionTracker::Settings{}, EyeGazeTracker::Settings{}};
  const std::vector<std::unique_ptr<FaceReader>> readers =
      MakeFaceReaders(Config{}, &reader);

  ASSERT_THAT(readers, SizeIs(4));
  EXPECT_EQ(reader.Pose(), std::nullopt);
  std::vector<Reaction> reactions;
  Face face{};
  face.pose_column_major[0] = 1;
  face.pose_column_major[5] = 1;
  face.pose_column_major[10] = 1;
  face.pose_column_major[15] = 1;
  readers[3]->OnFace(face, /*width=*/640, /*height=*/480, /*timestamp_ms=*/0,
                     reactions);

  EXPECT_NE(reader.Pose(), std::nullopt);
  EXPECT_THAT(reactions, IsEmpty());

  readers[3]->OnNoFace();

  EXPECT_EQ(reader.Pose(), std::nullopt);
}

TEST(FaceReadersTest, NoScreenContextReaderGivenIsNoReader) {
  EXPECT_THAT(MakeFaceReaders(Config{}, nullptr), SizeIs(3));
}

TEST(FaceReadersTest, ScreenContextDisabledSkipsItEvenWhenGiven) {
  Config config;
  config.screen_context_enabled = false;
  ScreenContextReader reader{ExpressionTracker::Settings{}, EyeGazeTracker::Settings{}};

  EXPECT_THAT(MakeFaceReaders(config, &reader), SizeIs(3));
}

// Calibration (#116): always appended when given, since it has no config
// flag of its own (src/daemon_calibration.h decides availability instead).
TEST(FaceReadersTest, CalibrationReaderIsAppendedWhenGiven) {
  MockCalibrationController calibration;
  const std::vector<std::unique_ptr<FaceReader>> readers = MakeFaceReaders(
      Config{}, /*screen_context_reader=*/nullptr, &calibration);

  ASSERT_THAT(readers, SizeIs(4));
  Face face{};
  face.pose_column_major[0] = 1;
  face.pose_column_major[5] = 1;
  face.pose_column_major[10] = 1;
  face.pose_column_major[15] = 1;
  std::vector<Reaction> reactions;
  EXPECT_CALL(calibration, Update).Times(1);
  readers[3]->OnFace(face, /*width=*/640, /*height=*/480, /*timestamp_ms=*/0,
                     reactions);

  EXPECT_THAT(reactions, IsEmpty());

  readers[3]->OnNoFace();  // Must not crash, and calls nothing (no window).
}

TEST(FaceReadersTest, NoCalibrationControllerGivenIsNoReader) {
  EXPECT_THAT(
      MakeFaceReaders(Config{}, /*screen_context_reader=*/nullptr, nullptr),
      SizeIs(3));
}

TEST(FaceReadersTest, ScreenContextAndCalibrationBothAppendedWhenGiven) {
  ScreenContextReader reader{ExpressionTracker::Settings{}, EyeGazeTracker::Settings{}};
  MockCalibrationController calibration;

  EXPECT_THAT(MakeFaceReaders(Config{}, &reader, &calibration), SizeIs(5));
}

}  // namespace
}  // namespace facepalm
