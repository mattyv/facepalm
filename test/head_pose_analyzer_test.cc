#include "src/head_pose_analyzer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/camera_controller.h"
#include "src/face_model.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SetArgReferee;

class MockFaceModel : public FaceModel {
 public:
  MOCK_METHOD(Result, Detect,
              (std::span<const std::uint8_t> rgba, int width, int height,
               std::int64_t timestamp_ms, Face& face, std::string& error),
              (override));
};

// A face looking straight ahead.
Face Ahead() {
  Face face{};
  face.pose_column_major[0] = 1;
  face.pose_column_major[5] = 1;
  face.pose_column_major[10] = 1;
  face.pose_column_major[15] = 1;
  return face;
}

class HeadPoseAnalyzerTest : public ::testing::Test {
 protected:
  HeadPoseAnalyzerTest() {
    auto model = std::make_unique<MockFaceModel>();
    model_ = model.get();
    analyzer_ = std::make_unique<HeadPoseAnalyzer>(std::move(model));
  }

  MockFaceModel* model_;
  std::unique_ptr<HeadPoseAnalyzer> analyzer_;
  std::vector<std::uint8_t> rgba_ = std::vector<std::uint8_t>(4, 0);
  std::vector<Reaction> reactions_;
  std::string error_;
};

TEST_F(HeadPoseAnalyzerTest, ReactsToAFaceWithItsHeadAngles) {
  EXPECT_CALL(*model_, Detect(ElementsAre(0, 0, 0, 0), 1, 1, 33, _, _))
      .WillOnce(
          DoAll(SetArgReferee<4>(Ahead()), Return(FaceModel::Result::kFace)));

  EXPECT_TRUE(analyzer_->Analyze(rgba_, 1, 1, 33, reactions_, error_));
  EXPECT_THAT(reactions_, ElementsAre(R"({"yaw":0.0,"pitch":0.0,"roll":0.0})"));
}

TEST_F(HeadPoseAnalyzerTest, MakesNoReactionWithoutAFace) {
  EXPECT_CALL(*model_, Detect(_, _, _, _, _, _))
      .WillOnce(Return(FaceModel::Result::kNoFace));

  EXPECT_TRUE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions_, error_));
  EXPECT_THAT(reactions_, IsEmpty());
}

TEST_F(HeadPoseAnalyzerTest, FailsWithTheModelsReason) {
  EXPECT_CALL(*model_, Detect(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<5>(std::string("model broke")),
                      Return(FaceModel::Result::kFailed)));

  EXPECT_FALSE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions_, error_));
  EXPECT_THAT(error_, Eq("model broke"));
  EXPECT_THAT(reactions_, IsEmpty());
}

}  // namespace
}  // namespace facepalm
