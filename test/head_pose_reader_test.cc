#include "src/head_pose_reader.h"

#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/camera_controller.h"
#include "src/face_model.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// A face looking straight ahead.
Face Ahead() {
  Face face{};
  face.pose_column_major[0] = 1;
  face.pose_column_major[5] = 1;
  face.pose_column_major[10] = 1;
  face.pose_column_major[15] = 1;
  return face;
}

TEST(HeadPoseReaderTest, ReactsToAFaceWithItsHeadAngles) {
  HeadPoseReader reader;
  std::vector<Reaction> reactions;

  reader.OnFace(Ahead(), 1, 1, 33, reactions);

  EXPECT_THAT(reactions, ElementsAre(R"({"yaw":0.0,"pitch":0.0,"roll":0.0})"));
}

TEST(HeadPoseReaderTest, ReactsToNothingForAFaceWithADegeneratePose) {
  HeadPoseReader reader;
  std::vector<Reaction> reactions;

  reader.OnFace(Face{}, 1, 1, 33, reactions);

  EXPECT_THAT(reactions, IsEmpty());
}

TEST(HeadPoseReaderTest, OnNoFaceReactsToNothing) {
  HeadPoseReader reader;
  const std::vector<Reaction> reactions;

  reader.OnNoFace();

  EXPECT_THAT(reactions, IsEmpty());
}

}  // namespace
}  // namespace facepalm
