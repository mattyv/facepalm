#include "src/gesture_analyzer.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/camera_controller.h"
#include "src/face_detector.h"
#include "test/nodding_pigeon_frames.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SetArgReferee;

using Frames = std::array<std::array<float, 16>, 60>;

class MockFaceDetector : public FaceDetector {
 public:
  MOCK_METHOD(std::optional<int>, Detect,
              (std::span<const std::uint8_t> rgba, int width, int height,
               Detection& first, std::string& error),
              (override));
};

// The detection a RawFrame came from.
Detection DetectionOf(const std::array<float, 16>& frame) {
  Detection detection{};
  for (int i = 0; i < 4; ++i) {
    detection.box[i] = frame.at(i);
  }
  for (int i = 0; i < FP_KEYPOINTS; ++i) {
    detection.keypoints[i][0] = frame.at(4 + i);
    detection.keypoints[i][1] = frame.at(10 + i);
  }
  return detection;
}

class GestureAnalyzerTest : public ::testing::Test {
 protected:
  GestureAnalyzerTest() {
    auto detector = std::make_unique<MockFaceDetector>();
    detector_ = detector.get();
    // predict_video's default thresholds.
    analyzer_ = std::make_unique<GestureAnalyzer>(
        std::move(detector),
        GestureAnalyzer::Thresholds{.motion = 0.5F, .gesture = 0.9F});
  }

  // Feeds `frames`, each as the one face in a frame, and returns every
  // reaction made.
  std::vector<Reaction> Feed(const Frames& frames) {
    std::vector<Reaction> reactions;
    for (const auto& frame : frames) {
      EXPECT_CALL(*detector_, Detect(_, _, _, _, _))
          .WillOnce(DoAll(SetArgReferee<3>(DetectionOf(frame)), Return(1)))
          .RetiresOnSaturation();
      EXPECT_TRUE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions, error_));
    }
    return reactions;
  }

  MockFaceDetector* detector_;
  std::unique_ptr<GestureAnalyzer> analyzer_;
  std::vector<std::uint8_t> rgba_ = std::vector<std::uint8_t>(4, 0);
  std::string error_;
};

TEST_F(GestureAnalyzerTest, ReactsToANodOnceItsWindowIsFull) {
  EXPECT_THAT(Feed(nodding_pigeon::kNoddingFrames),
              ElementsAre(R"({"gesture":"nod"})"));
}

TEST_F(GestureAnalyzerTest, ReactsToAShake) {
  EXPECT_THAT(Feed(nodding_pigeon::kTurningFrames),
              ElementsAre(R"({"gesture":"shake"})"));
}

TEST_F(GestureAnalyzerTest, MakesNoReactionToAStillHead) {
  EXPECT_THAT(Feed(nodding_pigeon::kStationaryFrames), IsEmpty());
}

TEST_F(GestureAnalyzerTest, DoesNotRepeatAGestureWhileItLasts) {
  Feed(nodding_pigeon::kNoddingFrames);
  std::vector<Reaction> reactions;
  // The same window again, shifted by one: still a nod.
  EXPECT_CALL(*detector_, Detect(_, _, _, _, _))
      .WillOnce(DoAll(
          SetArgReferee<3>(DetectionOf(nodding_pigeon::kNoddingFrames[0])),
          Return(1)));
  EXPECT_TRUE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions, error_));
  EXPECT_THAT(reactions, IsEmpty());
}

TEST_F(GestureAnalyzerTest, DoesNotRepeatANodThatFlickersToUndefined) {
  // A nod, then 9 frames of a shake: the windows go nod, undefined, nod,
  // undefined, nod, and only then stationary, then nod (seen live, #34).
  std::vector<Reaction> reactions = Feed(nodding_pigeon::kNoddingFrames);
  for (int i = 0; i < 9; ++i) {
    EXPECT_CALL(*detector_, Detect(_, _, _, _, _))
        .WillOnce(DoAll(
            SetArgReferee<3>(DetectionOf(nodding_pigeon::kTurningFrames.at(i))),
            Return(1)))
        .RetiresOnSaturation();
    EXPECT_TRUE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions, error_));
  }
  for (const Reaction& reaction : Feed(nodding_pigeon::kNoddingFrames)) {
    reactions.push_back(reaction);
  }

  EXPECT_THAT(reactions,
              ElementsAre(R"({"gesture":"nod"})", R"({"gesture":"nod"})"));
}

TEST_F(GestureAnalyzerTest, ReactsAgainAfterTheHeadIsStill) {
  Feed(nodding_pigeon::kNoddingFrames);
  Feed(nodding_pigeon::kStationaryFrames);

  EXPECT_THAT(Feed(nodding_pigeon::kNoddingFrames),
              ElementsAre(R"({"gesture":"nod"})"));
}

TEST_F(GestureAnalyzerTest, SkipsAFrameWithoutExactlyOneFace) {
  std::vector<Reaction> reactions;
  EXPECT_CALL(*detector_, Detect(_, _, _, _, _))
      .WillOnce(Return(0))
      .WillOnce(Return(2));
  EXPECT_TRUE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions, error_));
  EXPECT_TRUE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions, error_));

  // Neither counted: the nod still needs all 60 of its own frames.
  EXPECT_THAT(Feed(nodding_pigeon::kNoddingFrames),
              ElementsAre(R"({"gesture":"nod"})"));
  EXPECT_THAT(reactions, IsEmpty());
}

TEST_F(GestureAnalyzerTest, FailsWithTheDetectorsReason) {
  std::vector<Reaction> reactions;
  EXPECT_CALL(*detector_, Detect(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<4>(std::string("detector broke")),
                      Return(std::nullopt)));

  EXPECT_FALSE(analyzer_->Analyze(rgba_, 1, 1, 0, reactions, error_));
  EXPECT_THAT(error_, Eq("detector broke"));
}

}  // namespace
}  // namespace facepalm
