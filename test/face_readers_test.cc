#include "src/face_readers.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/camera_controller.h"
#include "src/config.h"
#include "src/expression_reader.h"
#include "src/expressions.h"
#include "src/face_model.h"
#include "src/gesture_reader.h"
#include "src/look_away_reader.h"
#include "src/screen_context_reader.h"

// The readers are pure computation, so the test makes them for real.

namespace facepalm {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

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
  ScreenContextReader reader{ExpressionTracker::Settings{}};
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
  ScreenContextReader reader{ExpressionTracker::Settings{}};

  EXPECT_THAT(MakeFaceReaders(config, &reader), SizeIs(3));
}

}  // namespace
}  // namespace facepalm
