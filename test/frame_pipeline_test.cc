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
#include "src/frame.h"
#include "src/frame_analyzer.h"
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
// The analysers together are the spec's model.

class MockTickets : public FrameTickets {
 public:
  MOCK_METHOD(std::optional<std::uint64_t>, OnFrameCaptured,
              (std::uint64_t attempt), (override));
  MOCK_METHOD(void, OnReaction,
              (std::uint64_t ticket, const Reaction& reaction), (override));
};

class MockAnalyzer : public FrameAnalyzer {
 public:
  // A failed expectation here would end the test run, which is fine.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~MockAnalyzer() override { Destroyed(); }
  MOCK_METHOD(bool, Analyze,
              (std::span<const std::uint8_t> rgba, int width, int height,
               std::int64_t timestamp_ms, std::vector<Reaction>& reactions,
               std::string& error),
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

// One 1x1 BGRA pixel; the analysers see it as RGBA.
constexpr std::array<std::uint8_t, 4> kBgra = {1, 2, 3, 4};
constexpr std::array<std::uint8_t, 4> kOtherBgra = {9, 9, 9, 9};
constexpr std::int64_t kTimestampMs = 66;
constexpr std::uint64_t kTicket = 7;
constexpr std::uint64_t kAttempt = 3;

Frame FrameOf(const std::array<std::uint8_t, 4>& bgra) {
  return Frame{.width = 1,
               .height = 1,
               .bytes_per_row = 4,
               .bgra = bgra.data(),
               .timestamp_ms = kTimestampMs,
               .attempt = kAttempt};
}

// An Analyze that appends `reaction` and succeeds. A failed allocation would
// end the test run, which is fine.
auto Reacts(const Reaction& reaction) {
  // NOLINTNEXTLINE(bugprone-exception-escape)
  return [reaction](std::span<const std::uint8_t>, int, int, std::int64_t,
                    std::vector<Reaction>& reactions, std::string&) {
    reactions.push_back(reaction);
    return true;
  };
}

class FramePipelineTest : public ::testing::Test {
 protected:
  // Adds an analyser to those the next open makes, owned by the pipeline
  // once made, and returns it.
  MockAnalyzer* NextAnalyzer() {
    auto analyzer = std::make_unique<StrictMock<MockAnalyzer>>();
    MockAnalyzer* raw = analyzer.get();
    next_.push_back(std::move(analyzer));
    return raw;
  }

  // Opens the camera with one analyser, runs the factory, and returns it.
  MockAnalyzer* Opened() {
    MockAnalyzer* analyzer = NextAnalyzer();
    OpenedWithNext();
    return analyzer;
  }

  // Opens the camera with the analysers added so far.
  void OpenedWithNext() {
    pipeline_.OnCameraOpen();
    inference_.Drain();
  }

  // Captures kBgra, and gives it kTicket. The ticket is asked for with the
  // attempt the frame was stamped with at capture (CameraLifecycle's
  // GuardTicketChecksCapture, #40).
  void Captured() {
    EXPECT_CALL(tickets_, OnFrameCaptured(kAttempt)).WillOnce(Return(kTicket));
    pipeline_.OnFrame(FrameOf(kBgra));
    control_.Drain();
  }

  FramePipeline::Analyzers MakeAnalyzers(std::string& error) {
    if (next_.empty() || factory_error_) {
      error = "no model";
    }
    FramePipeline::Analyzers made = std::move(next_);
    next_.clear();
    return made;
  }

  StrictMock<MockTickets> tickets_;
  FakeQueue control_;
  FakeQueue inference_;
  FramePipeline::Analyzers next_;
  bool factory_error_ = false;
  std::vector<std::string> errors_;
  FramePipeline pipeline_{
      tickets_, control_.AsPost(), inference_.AsPost(),
      FramePipeline::Callbacks{
          .make_analyzers =
              [this](std::string& error) { return MakeAnalyzers(error); },
          .on_error =
              [this](const std::string& error) { errors_.push_back(error); },
      }};
};

using CanDetectTest = FramePipelineTest;

TEST_F(CanDetectTest, AnalysesATicketedFrameAndSendsItsReaction) {
  MockAnalyzer* analyzer = Opened();
  EXPECT_CALL(*analyzer,
              Analyze(ElementsAre(3, 2, 1, 4), 1, 1, kTimestampMs, _, _))
      .WillOnce(Reacts("nod"));
  Captured();
  inference_.Drain();

  EXPECT_CALL(tickets_, OnReaction(kTicket, Reaction("nod")));
  control_.Drain();
  EXPECT_CALL(*analyzer, Destroyed());
}

TEST_F(CanDetectTest, SendsNothingWhenThereIsNoReaction) {
  MockAnalyzer* analyzer = Opened();
  EXPECT_CALL(*analyzer, Analyze(_, _, _, _, _, _)).WillOnce(Return(true));
  Captured();
  inference_.Drain();
  control_.Drain();  // StrictMock: an OnReaction here would fail.

  EXPECT_CALL(*analyzer, Destroyed());
}

TEST_F(CanDetectTest, RunsEveryAnalyzerAndSendsEachReaction) {
  MockAnalyzer* first = NextAnalyzer();
  MockAnalyzer* second = NextAnalyzer();
  OpenedWithNext();
  EXPECT_CALL(*first, Analyze(_, _, _, _, _, _)).WillOnce(Reacts("head"));
  EXPECT_CALL(*second, Analyze(_, _, _, _, _, _)).WillOnce(Reacts("nod"));
  Captured();
  inference_.Drain();

  {
    InSequence order;
    EXPECT_CALL(tickets_, OnReaction(kTicket, Reaction("head")));
    EXPECT_CALL(tickets_, OnReaction(kTicket, Reaction("nod")));
  }
  control_.Drain();
  EXPECT_CALL(*first, Destroyed());
  EXPECT_CALL(*second, Destroyed());
}

using NoDetectWithoutModelTest = FramePipelineTest;

TEST_F(NoDetectWithoutModelTest, AnalysesNothingBeforeTheCameraOpens) {
  Captured();
  inference_.Drain();  // No analysers: none could even be called.

  EXPECT_THAT(errors_, ElementsAre());
}

TEST_F(NoDetectWithoutModelTest, AnalysesNothingWhenNoneCouldBeMade) {
  OpenedWithNext();
  Captured();
  inference_.Drain();

  EXPECT_THAT(errors_, ElementsAre("no model"));
}

TEST_F(NoDetectWithoutModelTest, RunsTheAnalysersThatCouldBeMade) {
  MockAnalyzer* analyzer = NextAnalyzer();
  factory_error_ = true;
  OpenedWithNext();
  EXPECT_CALL(*analyzer, Analyze(_, _, _, _, _, _)).WillOnce(Return(true));
  Captured();
  inference_.Drain();

  EXPECT_THAT(errors_, ElementsAre("no model"));
  EXPECT_CALL(*analyzer, Destroyed());
}

TEST_F(NoDetectWithoutModelTest, AnalysesNothingOnceClosed) {
  MockAnalyzer* analyzer = Opened();
  EXPECT_CALL(*analyzer, Destroyed());
  pipeline_.OnCameraClosed();
  Captured();
  inference_.Drain();  // The destroy runs first; the analyse finds none.
}

TEST_F(NoDetectWithoutModelTest, ClosingWaitsForAnAnalyseAlreadyQueued) {
  MockAnalyzer* analyzer = Opened();
  // Recorded rather than ordered by expectations, so a destroy that comes
  // too early fails here instead of leaving a dangling mock.
  std::vector<std::string> events;
  EXPECT_CALL(*analyzer, Analyze(_, _, _, _, _, _))
      .WillOnce([&events](std::span<const std::uint8_t>, int, int, std::int64_t,
                          std::vector<Reaction>&, std::string&) {
        events.emplace_back("analyse");
        return true;
      });
  EXPECT_CALL(*analyzer, Destroyed()).WillOnce([&events] {
    events.emplace_back("destroy");
  });
  Captured();
  pipeline_.OnCameraClosed();
  inference_.Drain();

  EXPECT_THAT(events, ElementsAre("analyse", "destroy"));
}

using NeverWrittenWhileReadTest = FramePipelineTest;

TEST_F(NeverWrittenWhileReadTest, AFrameArrivingDuringAnalyseLeavesItsPixels) {
  MockAnalyzer* analyzer = Opened();
  EXPECT_CALL(*analyzer, Analyze(_, _, _, _, _, _))
      .WillOnce([this](std::span<const std::uint8_t> rgba, int, int,
                       std::int64_t, std::vector<Reaction>&, std::string&) {
        pipeline_.OnFrame(FrameOf(kOtherBgra));
        EXPECT_THAT(rgba, ElementsAre(3, 2, 1, 4));
        return true;
      });
  Captured();
  inference_.Drain();
  control_.Drain();  // StrictMock: no ticket for the dropped frame.
  EXPECT_CALL(*analyzer, Destroyed());
}

using AtMostOneInFlightTest = FramePipelineTest;

TEST_F(AtMostOneInFlightTest, DropsAFrameThatArrivesBeforeTheTicket) {
  EXPECT_CALL(tickets_, OnFrameCaptured(_)).WillOnce(Return(std::nullopt));
  pipeline_.OnFrame(FrameOf(kBgra));
  pipeline_.OnFrame(FrameOf(kOtherBgra));
  control_.Drain();
}

TEST_F(AtMostOneInFlightTest, DropsAFrameThatArrivesBeforeTheAnalyse) {
  Captured();
  pipeline_.OnFrame(FrameOf(kOtherBgra));
  control_.Drain();  // StrictMock: no second ticket.
  inference_.Drain();
}

using SlotFreedTest = FramePipelineTest;

TEST_F(SlotFreedTest, AfterARefusedTicket) {
  EXPECT_CALL(tickets_, OnFrameCaptured(_))
      .WillOnce(Return(std::nullopt))
      .WillOnce(Return(std::nullopt));
  pipeline_.OnFrame(FrameOf(kBgra));
  control_.Drain();
  pipeline_.OnFrame(FrameOf(kBgra));
  control_.Drain();
}

TEST_F(SlotFreedTest, AfterAFailedAnalyse) {
  MockAnalyzer* analyzer = Opened();
  EXPECT_CALL(*analyzer, Analyze(_, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgReferee<5>(std::string("model broke")), Return(false)))
      .WillOnce(Return(true));
  Captured();
  inference_.Drain();
  Captured();
  inference_.Drain();

  EXPECT_THAT(errors_, ElementsAre("model broke"));
  EXPECT_CALL(*analyzer, Destroyed());
}

TEST_F(SlotFreedTest, AfterOneAnalyserFailsAndTheOthersRun) {
  MockAnalyzer* failing = NextAnalyzer();
  MockAnalyzer* working = NextAnalyzer();
  OpenedWithNext();
  EXPECT_CALL(*failing, Analyze(_, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgReferee<5>(std::string("model broke")), Return(false)));
  EXPECT_CALL(*working, Analyze(_, _, _, _, _, _)).WillOnce(Reacts("nod"));
  Captured();
  inference_.Drain();

  EXPECT_THAT(errors_, ElementsAre("model broke"));
  EXPECT_CALL(tickets_, OnReaction(kTicket, Reaction("nod")));
  control_.Drain();
  EXPECT_CALL(*failing, Destroyed());
  EXPECT_CALL(*working, Destroyed());
}

TEST_F(SlotFreedTest, AfterAReaction) {
  MockAnalyzer* analyzer = Opened();
  EXPECT_CALL(*analyzer, Analyze(_, _, _, _, _, _))
      .Times(2)
      .WillRepeatedly(Reacts("nod"));
  EXPECT_CALL(tickets_, OnReaction(kTicket, _)).Times(2);
  for (int i = 0; i < 2; ++i) {
    Captured();
    inference_.Drain();
    control_.Drain();
  }
  EXPECT_CALL(*analyzer, Destroyed());
}

TEST_F(SlotFreedTest, WithNoModel) {
  Captured();
  inference_.Drain();
  Captured();
  inference_.Drain();
}

// Not a spec property: a frame's pixels are wiped however it ends, so no
// image of the user outlives its use. The buffer is watched through the span
// an analyse was given, which stays valid because the vector keeps its
// allocation for a frame of the same size.
using PixelsWipedTest = FramePipelineTest;

// An Analyze that keeps the span it was given in `buffer`.
auto Watches(std::span<const std::uint8_t>& buffer) {
  return [&buffer](std::span<const std::uint8_t> rgba, int, int, std::int64_t,
                   std::vector<Reaction>&, std::string&) {
    buffer = rgba;
    return true;
  };
}

TEST_F(PixelsWipedTest, AfterAnAnalyse) {
  MockAnalyzer* analyzer = Opened();
  std::span<const std::uint8_t> buffer;
  EXPECT_CALL(*analyzer, Analyze(_, _, _, _, _, _)).WillOnce(Watches(buffer));
  Captured();
  inference_.Drain();

  EXPECT_THAT(buffer, ElementsAre(0, 0, 0, 0));
  EXPECT_CALL(*analyzer, Destroyed());
}

TEST_F(PixelsWipedTest, AfterARefusedTicket) {
  MockAnalyzer* analyzer = Opened();
  std::span<const std::uint8_t> buffer;
  EXPECT_CALL(*analyzer, Analyze(_, _, _, _, _, _)).WillOnce(Watches(buffer));
  Captured();
  inference_.Drain();
  // The next frame's pixels are copied in, then refused.
  EXPECT_CALL(tickets_, OnFrameCaptured(_)).WillOnce(Return(std::nullopt));
  pipeline_.OnFrame(FrameOf(kOtherBgra));
  control_.Drain();

  EXPECT_THAT(buffer, ElementsAre(0, 0, 0, 0));
  EXPECT_CALL(*analyzer, Destroyed());
}

using ModelGoneAfterCloseTest = FramePipelineTest;

TEST_F(ModelGoneAfterCloseTest, ClosingDestroysTheAnalysers) {
  MockAnalyzer* analyzer = Opened();
  // By the end of the drain, not at the pipeline's own destruction, which
  // would satisfy a bare expectation even when closing destroys nothing.
  bool destroyed = false;
  EXPECT_CALL(*analyzer, Destroyed()).WillOnce([&destroyed] {
    destroyed = true;
  });
  pipeline_.OnCameraClosed();
  inference_.Drain();

  EXPECT_TRUE(destroyed);
}

TEST_F(ModelGoneAfterCloseTest, ReopeningMakesNewAnalysers) {
  MockAnalyzer* first = Opened();
  EXPECT_CALL(*first, Destroyed());
  pipeline_.OnCameraClosed();
  MockAnalyzer* second = Opened();
  EXPECT_CALL(*second, Analyze(_, _, _, _, _, _)).WillOnce(Return(true));
  Captured();
  inference_.Drain();
  EXPECT_CALL(*second, Destroyed());
}

}  // namespace
}  // namespace facepalm
