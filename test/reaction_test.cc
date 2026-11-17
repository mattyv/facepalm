#include "src/reaction.h"

#include <array>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/face_model.h"

namespace facepalm {
namespace {

using ::testing::Eq;

// A face whose transformation matrix, column by column, is `pose`.
Face FaceWithPose(const std::array<float, FP_POSE_VALUES>& pose) {
  Face face{};
  for (int i = 0; i < FP_POSE_VALUES; ++i) {
    face.pose_column_major[i] = pose.at(i);
  }
  return face;
}

TEST(ReactionFromTest, AFaceStraightAheadHasNoTurn) {
  const Face face = FaceWithPose({1, 0, 0, 0,  //
                                  0, 1, 0, 0,  //
                                  0, 0, 1, 0,  //
                                  0, 0, -60, 1});

  EXPECT_THAT(ReactionFrom(face), Eq(R"({"yaw":0.0,"pitch":0.0,"roll":0.0})"));
}

TEST(ReactionFromTest, GivesMediaPipesPortraitsAnglesToOneDecimalPlace) {
  // MediaPipe's expected matrix for its portrait.jpg, as in head_pose_test,
  // whose angles are yaw 2.24, pitch 6.32 and roll 0.99 degrees.
  const Face face = FaceWithPose({0.9995292F, 0.01715696F, -0.03715533F, 0,  //
                                  -0.01294756F, 0.9937692F, 0.11070588F, 0,  //
                                  0.038823195F, -0.1101321F, 0.99315894F, 0,
                                  -0.3691378F, 22.75809F, -65.765925F, 1});

  EXPECT_THAT(ReactionFrom(face), Eq(R"({"yaw":2.2,"pitch":6.3,"roll":1.0})"));
}

}  // namespace
}  // namespace facepalm
