#include "src/face_readers.h"

#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/config.h"
#include "src/expression_reader.h"
#include "src/gesture_reader.h"
#include "src/look_away_reader.h"

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

}  // namespace
}  // namespace facepalm
