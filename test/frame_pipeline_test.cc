#include "src/frame_pipeline.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/frame.h"
#include "src/post.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;

// Tests are grouped by the specs/FramePipeline.tla property they stand for.

class MockTickets : public FrameTickets {
 public:
  MOCK_METHOD(std::optional<std::uint64_t>, OnFrameCaptured, (), (override));
  MOCK_METHOD(void, OnReaction,
              (std::uint64_t ticket, const Reaction& reaction), (override));
};

class MockFaceModel : public FaceModel {
 public:
  // A failed expectation here would end the test run, which is fine.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~MockFaceModel() override { Destroyed(); }
  MOCK_METHOD(Result, Detect,
              (std::span<const std::uint8_t> rgba, int width, int height,
               std::int64_t timestamp_ms, Face& face, std::string& error),
              (override));
  MOCK_METHOD(void, Destroyed, ());
};

// A serial queue whose tasks run only when the test drains it.
class FakeQueue {
 public:
  Post AsPost() {
    return [this](std::function<void()> task) {
      tasks_.push_back(std::move(task));
    };
  }

  // Runs every task queued so far, and any they queue.
  void Drain() {
    while (!tasks_.empty()) {
      const std::function<void()> task = std::move(tasks_.front());
      tasks_.pop_front();
      task();
    }
  }

 private:
  std::deque<std::function<void()>> tasks_;
};

// One 1x1 BGRA pixel; the model sees it as RGBA.
constexpr std::array<std::uint8_t, 4> kBgra = {1, 2, 3, 4};
constexpr std::array<std::uint8_t, 4> kOtherBgra = {9, 9, 9, 9};
constexpr std::int64_t kTimestampMs = 66;
constexpr std::uint64_t kTicket = 7;

Frame FrameOf(const std::array<std::uint8_t, 4>& bgra) {
  return Frame{.width = 1,
               .height = 1,
               .bytes_per_row = 4,
               .bgra = bgra.data(),
               .timestamp_ms = kTimestampMs};
}

class FramePipelineTest : public ::testing::Test {
 protected:
  // Makes the next model the factory returns, owned by the pipeline once
  // made, and returns it.
  MockFaceModel* NextModel() {
    auto model = std::make_unique<StrictMock<MockFaceModel>>();
    MockFaceModel* raw = model.get();
    next_models_.push_back(std::move(model));
    return raw;
  }

  // Opens the camera and runs the model's create.
  MockFaceModel* Opened() {
    MockFaceModel* model = NextModel();
    pipeline_.OnCameraOpen();
    inference_.Drain();
    return model;
  }

  // Captures kBgra, and gives it kTicket.
  void Captured() {
    EXPECT_CALL(tickets_, OnFrameCaptured()).WillOnce(Return(kTicket));
    pipeline_.OnFrame(FrameOf(kBgra));
    control_.Drain();
  }

  std::unique_ptr<FaceModel> MakeModel(std::string& error) {
    if (next_models_.empty()) {
      error = "no model";
      return nullptr;
    }
    std::unique_ptr<FaceModel> model = std::move(next_models_.front());
    next_models_.pop_front();
    return model;
  }

  StrictMock<MockTickets> tickets_;
  FakeQueue control_;
  FakeQueue inference_;
  std::deque<std::unique_ptr<FaceModel>> next_models_;
  std::vector<std::string> errors_;
  std::vector<std::uint8_t> reacted_to_;  // Each face's first pose value.
  std::optional<Reaction> reaction_ = "nod";
  FramePipeline pipeline_{
      tickets_, control_.AsPost(), inference_.AsPost(),
      FramePipeline::Callbacks{
          .make_model = [this](std::string& error) { return MakeModel(error); },
          .react =
              [this](const Face& face) {
                reacted_to_.push_back(
                    static_cast<std::uint8_t>(face.pose_column_major[0]));
                return reaction_;
              },
          .on_error =
              [this](const std::string& error) { errors_.push_back(error); },
      }};
};

// A face whose first pose value is `marker`.
Face FaceMarked(float marker) {
  Face face{};
  face.pose_column_major[0] = marker;
  return face;
}

using CanDetectTest = FramePipelineTest;

TEST_F(CanDetectTest, DetectsATicketedFrameAndSendsItsReaction) {
  MockFaceModel* model = Opened();
  EXPECT_CALL(*model, Detect(ElementsAre(3, 2, 1, 4), 1, 1, kTimestampMs, _, _))
      .WillOnce(DoAll(SetArgReferee<4>(FaceMarked(5)),
                      Return(FaceModel::Result::kFace)));
  Captured();
  inference_.Drain();

  EXPECT_THAT(reacted_to_, ElementsAre(5));
  EXPECT_CALL(tickets_, OnReaction(kTicket, Reaction("nod")));
  control_.Drain();
  EXPECT_CALL(*model, Destroyed());
}

TEST_F(CanDetectTest, SendsNothingWhenThereIsNoFace) {
  MockFaceModel* model = Opened();
  EXPECT_CALL(*model, Detect(_, _, _, _, _, _))
      .WillOnce(Return(FaceModel::Result::kNoFace));
  Captured();
  inference_.Drain();
  control_.Drain();

  EXPECT_THAT(reacted_to_, ElementsAre());
  EXPECT_CALL(*model, Destroyed());
}

TEST_F(CanDetectTest, SendsNothingWhenTheFaceMakesNoReaction) {
  MockFaceModel* model = Opened();
  reaction_ = std::nullopt;
  EXPECT_CALL(*model, Detect(_, _, _, _, _, _))
      .WillOnce(Return(FaceModel::Result::kFace));
  Captured();
  inference_.Drain();
  control_.Drain();  // StrictMock: an OnReaction here would fail.

  EXPECT_THAT(reacted_to_, ElementsAre(0));
  EXPECT_CALL(*model, Destroyed());
}

using NoDetectWithoutModelTest = FramePipelineTest;

TEST_F(NoDetectWithoutModelTest, DetectsNothingBeforeTheCameraOpens) {
  Captured();
  inference_.Drain();  // No model: a Detect could not even be called.

  EXPECT_THAT(errors_, ElementsAre());
}

TEST_F(NoDetectWithoutModelTest, DetectsNothingWhenTheModelFailedToLoad) {
  pipeline_.OnCameraOpen();
  inference_.Drain();
  Captured();
  inference_.Drain();

  EXPECT_THAT(errors_, ElementsAre("no model"));
}

TEST_F(NoDetectWithoutModelTest, DetectsNothingOnceClosed) {
  MockFaceModel* model = Opened();
  EXPECT_CALL(*model, Destroyed());
  pipeline_.OnCameraClosed();
  Captured();
  inference_.Drain();  // The destroy runs first; the detect finds no model.
}

TEST_F(NoDetectWithoutModelTest, ClosingWaitsForADetectAlreadyQueued) {
  MockFaceModel* model = Opened();
  Captured();
  pipeline_.OnCameraClosed();
  {
    InSequence order;
    EXPECT_CALL(*model, Detect(_, _, _, _, _, _))
        .WillOnce(Return(FaceModel::Result::kNoFace));
    EXPECT_CALL(*model, Destroyed());
  }
  inference_.Drain();
}

using NeverWrittenWhileReadTest = FramePipelineTest;

TEST_F(NeverWrittenWhileReadTest, AFrameArrivingDuringDetectLeavesItsPixels) {
  MockFaceModel* model = Opened();
  EXPECT_CALL(*model, Detect(_, _, _, _, _, _))
      .WillOnce([this](std::span<const std::uint8_t> rgba, int, int,
                       std::int64_t, Face&, std::string&) {
        pipeline_.OnFrame(FrameOf(kOtherBgra));
        EXPECT_THAT(rgba, ElementsAre(3, 2, 1, 4));
        return FaceModel::Result::kNoFace;
      });
  Captured();
  inference_.Drain();
  control_.Drain();  // StrictMock: no ticket for the dropped frame.
  EXPECT_CALL(*model, Destroyed());
}

using AtMostOneInFlightTest = FramePipelineTest;

TEST_F(AtMostOneInFlightTest, DropsAFrameThatArrivesBeforeTheTicket) {
  EXPECT_CALL(tickets_, OnFrameCaptured()).WillOnce(Return(std::nullopt));
  pipeline_.OnFrame(FrameOf(kBgra));
  pipeline_.OnFrame(FrameOf(kOtherBgra));
  control_.Drain();
}

TEST_F(AtMostOneInFlightTest, DropsAFrameThatArrivesBeforeTheDetect) {
  Captured();
  pipeline_.OnFrame(FrameOf(kOtherBgra));
  control_.Drain();  // StrictMock: no second ticket.
  inference_.Drain();
}

using SlotFreedTest = FramePipelineTest;

TEST_F(SlotFreedTest, AfterARefusedTicket) {
  EXPECT_CALL(tickets_, OnFrameCaptured())
      .WillOnce(Return(std::nullopt))
      .WillOnce(Return(std::nullopt));
  pipeline_.OnFrame(FrameOf(kBgra));
  control_.Drain();
  pipeline_.OnFrame(FrameOf(kBgra));
  control_.Drain();
}

TEST_F(SlotFreedTest, AfterAFailedDetect) {
  MockFaceModel* model = Opened();
  EXPECT_CALL(*model, Detect(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<5>(std::string("model broke")),
                      Return(FaceModel::Result::kFailed)))
      .WillOnce(Return(FaceModel::Result::kNoFace));
  Captured();
  inference_.Drain();
  Captured();
  inference_.Drain();

  EXPECT_THAT(errors_, ElementsAre("model broke"));
  EXPECT_CALL(*model, Destroyed());
}

TEST_F(SlotFreedTest, AfterAReaction) {
  MockFaceModel* model = Opened();
  EXPECT_CALL(*model, Detect(_, _, _, _, _, _))
      .Times(2)
      .WillRepeatedly(Return(FaceModel::Result::kFace));
  EXPECT_CALL(tickets_, OnReaction(kTicket, _)).Times(2);
  for (int i = 0; i < 2; ++i) {
    Captured();
    inference_.Drain();
    control_.Drain();
  }
  EXPECT_CALL(*model, Destroyed());
}

TEST_F(SlotFreedTest, WithNoModel) {
  Captured();
  inference_.Drain();
  Captured();
  inference_.Drain();
}

// Not a spec property: a frame's pixels are wiped however it ends, so no
// image of the user outlives its use. The buffer is watched through the span
// a detect was given, which stays valid because the vector keeps its
// allocation for a frame of the same size.
using PixelsWipedTest = FramePipelineTest;

TEST_F(PixelsWipedTest, AfterADetect) {
  MockFaceModel* model = Opened();
  std::span<const std::uint8_t> buffer;
  EXPECT_CALL(*model, Detect(_, _, _, _, _, _))
      .WillOnce([&buffer](std::span<const std::uint8_t> rgba, int, int,
                          std::int64_t, Face&, std::string&) {
        buffer = rgba;
        return FaceModel::Result::kNoFace;
      });
  Captured();
  inference_.Drain();

  EXPECT_THAT(buffer, ElementsAre(0, 0, 0, 0));
  EXPECT_CALL(*model, Destroyed());
}

TEST_F(PixelsWipedTest, AfterARefusedTicket) {
  MockFaceModel* model = Opened();
  std::span<const std::uint8_t> buffer;
  EXPECT_CALL(*model, Detect(_, _, _, _, _, _))
      .WillOnce([&buffer](std::span<const std::uint8_t> rgba, int, int,
                          std::int64_t, Face&, std::string&) {
        buffer = rgba;
        return FaceModel::Result::kNoFace;
      });
  Captured();
  inference_.Drain();
  // The buffer keeps its storage for a frame of the same size, so `buffer`
  // still views it. The next frame's pixels are copied in, then refused.
  EXPECT_CALL(tickets_, OnFrameCaptured()).WillOnce(Return(std::nullopt));
  pipeline_.OnFrame(FrameOf(kOtherBgra));
  control_.Drain();

  EXPECT_THAT(buffer, ElementsAre(0, 0, 0, 0));
  EXPECT_CALL(*model, Destroyed());
}

using ModelGoneAfterCloseTest = FramePipelineTest;

TEST_F(ModelGoneAfterCloseTest, ClosingDestroysTheModel) {
  MockFaceModel* model = Opened();
  pipeline_.OnCameraClosed();

  // By the end of the drain, not at the pipeline's own destruction, which
  // would satisfy a bare expectation even when closing destroys nothing.
  bool destroyed = false;
  EXPECT_CALL(*model, Destroyed()).WillOnce([&destroyed] { destroyed = true; });
  inference_.Drain();
  EXPECT_TRUE(destroyed);
}

TEST_F(ModelGoneAfterCloseTest, ReopeningMakesANewModel) {
  MockFaceModel* first = Opened();
  EXPECT_CALL(*first, Destroyed());
  pipeline_.OnCameraClosed();
  MockFaceModel* second = Opened();
  EXPECT_CALL(*second, Detect(_, _, _, _, _, _))
      .WillOnce(Return(FaceModel::Result::kNoFace));
  Captured();
  inference_.Drain();
  EXPECT_CALL(*second, Destroyed());
}

}  // namespace
}  // namespace facepalm
