#include "src/face_analyzer.h"

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
#include "src/face_reader.h"

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

class MockFaceReader : public FaceReader {
 public:
  MOCK_METHOD(void, OnFace,
              (const Face& face, int width, int height,
               std::int64_t timestamp_ms, std::vector<Reaction>& reactions),
              (override));
  MOCK_METHOD(void, OnNoFace, (), (override));
};

class FaceAnalyzerTest : public ::testing::Test {
 protected:
  FaceAnalyzerTest() {
    auto model = std::make_unique<MockFaceModel>();
    model_ = model.get();
    auto reader_a = std::make_unique<MockFaceReader>();
    auto reader_b = std::make_unique<MockFaceReader>();
    reader_a_ = reader_a.get();
    reader_b_ = reader_b.get();
    std::vector<std::unique_ptr<FaceReader>> readers;
    readers.push_back(std::move(reader_a));
    readers.push_back(std::move(reader_b));
    analyzer_ =
        std::make_unique<FaceAnalyzer>(std::move(model), std::move(readers));
  }

  MockFaceModel* model_;
  MockFaceReader* reader_a_;
  MockFaceReader* reader_b_;
  std::unique_ptr<FaceAnalyzer> analyzer_;
  std::vector<std::uint8_t> rgba_ = std::vector<std::uint8_t>(4, 0);
  std::vector<Reaction> reactions_;
  std::string error_;
};

TEST_F(FaceAnalyzerTest, GivesEveryReaderTheFaceItFound) {
  Face face{};
  face.pose_column_major[0] = 1;
  EXPECT_CALL(*model_, Detect(ElementsAre(0, 0, 0, 0), 2, 3, 33, _, _))
      .WillOnce(
          DoAll(SetArgReferee<4>(face), Return(FaceModel::Result::kFace)));
  EXPECT_CALL(*reader_a_, OnFace(_, 2, 3, 33, _));
  EXPECT_CALL(*reader_b_, OnFace(_, 2, 3, 33, _));

  EXPECT_TRUE(analyzer_->Analyze(rgba_, 2, 3, 33, reactions_, error_));
}

TEST_F(FaceAnalyzerTest, TellsEveryReaderWhenThereIsNoFace) {
  EXPECT_CALL(*model_, Detect(_, _, _, _, _, _))
      .WillOnce(Return(FaceModel::Result::kNoFace));
  EXPECT_CALL(*reader_a_, OnNoFace());
  EXPECT_CALL(*reader_b_, OnNoFace());

  EXPECT_TRUE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions_, error_));
  EXPECT_THAT(reactions_, IsEmpty());
}

TEST_F(FaceAnalyzerTest, FailsWithTheModelsReasonAndTellsNoReader) {
  EXPECT_CALL(*model_, Detect(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<5>(std::string("model broke")),
                      Return(FaceModel::Result::kFailed)));
  EXPECT_CALL(*reader_a_, OnFace(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*reader_a_, OnNoFace()).Times(0);
  EXPECT_CALL(*reader_b_, OnFace(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*reader_b_, OnNoFace()).Times(0);

  EXPECT_FALSE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions_, error_));
  EXPECT_THAT(error_, Eq("model broke"));
  EXPECT_THAT(reactions_, IsEmpty());
}

}  // namespace
}  // namespace facepalm
