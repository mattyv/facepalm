#include "src/frame_recorder.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/frame.h"
#include "src/post.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::StrictMock;

// Tests are grouped by the specs/ProbeShutdown.tla property they stand for.

class MockSink : public FrameSink {
 public:
  MOCK_METHOD(bool, Save, (int index, const Frame& frame), (override));
};

// A serial queue whose tasks run only when the test says.
class FakeQueue {
 public:
  Post AsPost() {
    return [this](std::function<void()> task) {
      tasks_.push_back(std::move(task));
    };
  }

  // Runs the task at the head of the queue.
  void RunOne() {
    const std::function<void()> task = std::move(tasks_.front());
    tasks_.pop_front();
    task();
  }

  // Runs every task queued so far, and any they queue.
  void Drain() {
    while (!tasks_.empty()) {
      RunOne();
    }
  }

  [[nodiscard]] bool Empty() const { return tasks_.empty(); }

 private:
  std::deque<std::function<void()>> tasks_;
};

// A 1x1 frame of `bgra`, captured at `timestamp_ms`.
Frame FrameOf(const std::array<std::uint8_t, 4>& bgra,
              std::int64_t timestamp_ms) {
  return Frame{.width = 1,
               .height = 1,
               .bytes_per_row = bgra.size(),
               .bgra = bgra.data(),
               .timestamp_ms = timestamp_ms};
}

constexpr std::array<std::uint8_t, 4> kPixel = {1, 2, 3, 4};

// The summary Finish reports once `write` and `control` have run.
std::optional<FrameRecorder::Summary> Finished(FrameRecorder& recorder,
                                               FakeQueue& write) {
  FakeQueue control;
  std::optional<FrameRecorder::Summary> summary;
  recorder.Finish(
      control.AsPost(),
      [&summary](const FrameRecorder::Summary& result) { summary = result; });
  write.Drain();
  control.Drain();
  return summary;
}

// A sink that saves every frame and keeps its index, time and pixels.
struct Saved {
  int index = 0;
  std::int64_t timestamp_ms = 0;
  std::vector<std::uint8_t> bgra;

  friend bool operator==(const Saved&, const Saved&) = default;
};

TEST(FrameRecorderTest, SavesEachFrameNumberedFromOneWithItsTimeAndPixels) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/2);
  std::vector<Saved> saved;
  EXPECT_CALL(sink, Save(_, _))
      .Times(2)
      .WillRepeatedly([&saved](int index, const Frame& frame) {
        saved.push_back({.index = index,
                         .timestamp_ms = frame.timestamp_ms,
                         .bgra = std::vector<std::uint8_t>(
                             frame.bgra, frame.bgra + frame.bytes_per_row)});
        return true;
      });
  std::array<std::uint8_t, 4> pixel = kPixel;

  recorder.Record(FrameOf(pixel, 100));
  pixel.fill(0);  // The capture's buffer is gone once Record returns.
  recorder.Record(FrameOf(pixel, 167));
  write.Drain();

  EXPECT_THAT(
      saved, ElementsAre(
                 Saved{.index = 1, .timestamp_ms = 100, .bgra = {1, 2, 3, 4}},
                 Saved{.index = 2, .timestamp_ms = 167, .bgra = {0, 0, 0, 0}}));
}

TEST(BacklogBoundedTest, DropsAFrameWhileTheBacklogIsFull) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/2);
  EXPECT_CALL(sink, Save(1, _)).WillOnce(Return(true));
  EXPECT_CALL(sink, Save(2, _)).WillOnce(Return(true));

  recorder.Record(FrameOf(kPixel, 1));
  recorder.Record(FrameOf(kPixel, 2));
  recorder.Record(FrameOf(kPixel, 3));

  EXPECT_THAT(
      Finished(recorder, write),
      Eq(FrameRecorder::Summary{.saved = 2, .failed = 0, .dropped = 1}));
}

TEST(BacklogBoundedTest, TakesFramesAgainOnceOneIsSaved) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/1);
  EXPECT_CALL(sink, Save(1, _)).WillOnce(Return(true));
  EXPECT_CALL(sink, Save(2, _)).WillOnce(Return(true));

  recorder.Record(FrameOf(kPixel, 1));
  write.RunOne();
  recorder.Record(FrameOf(kPixel, 2));

  EXPECT_THAT(
      Finished(recorder, write),
      Eq(FrameRecorder::Summary{.saved = 2, .failed = 0, .dropped = 0}));
}

TEST(BacklogBoundedTest, TakesFramesAgainOnceOneHasFailed) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/1);
  EXPECT_CALL(sink, Save(1, _)).WillOnce(Return(false));
  EXPECT_CALL(sink, Save(2, _)).WillOnce(Return(true));

  recorder.Record(FrameOf(kPixel, 1));
  write.RunOne();
  recorder.Record(FrameOf(kPixel, 2));

  EXPECT_THAT(
      Finished(recorder, write),
      Eq(FrameRecorder::Summary{.saved = 1, .failed = 1, .dropped = 0}));
}

TEST(ExitAfterDrainTest, FinishReportsOnlyAfterEveryRecordedFrame) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FakeQueue control;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/2);
  MockFunction<void(const FrameRecorder::Summary&)> done;
  {
    const InSequence order;
    EXPECT_CALL(sink, Save(1, _)).WillOnce(Return(true));
    EXPECT_CALL(sink, Save(2, _)).WillOnce(Return(true));
    EXPECT_CALL(done, Call(FrameRecorder::Summary{
                          .saved = 2, .failed = 0, .dropped = 0}));
  }
  recorder.Record(FrameOf(kPixel, 1));
  recorder.Record(FrameOf(kPixel, 2));

  recorder.Finish(control.AsPost(), done.AsStdFunction());
  write.RunOne();
  write.RunOne();
  control.Drain();  // Nothing to report until the finish task has run.
  write.Drain();
  control.Drain();
}

TEST(ExitAfterDrainTest, FinishReportsOnTheControlQueue) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FakeQueue control;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/1);
  MockFunction<void(const FrameRecorder::Summary&)> done;
  EXPECT_CALL(done, Call(_)).Times(0);

  recorder.Finish(control.AsPost(), done.AsStdFunction());
  write.Drain();

  EXPECT_FALSE(control.Empty());
}

TEST(SuccessMeansCompleteTest, ASummaryWithEveryFrameSavedHasNoLoss) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/1);
  EXPECT_CALL(sink, Save(1, _)).WillOnce(Return(true));

  recorder.Record(FrameOf(kPixel, 1));

  EXPECT_THAT(
      Finished(recorder, write),
      Eq(FrameRecorder::Summary{.saved = 1, .failed = 0, .dropped = 0}));
}

TEST(SuccessMeansCompleteTest, CountsAFrameWhoseSaveFailed) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/1);
  EXPECT_CALL(sink, Save(1, _)).WillOnce(Return(false));

  recorder.Record(FrameOf(kPixel, 1));

  EXPECT_THAT(
      Finished(recorder, write),
      Eq(FrameRecorder::Summary{.saved = 0, .failed = 1, .dropped = 0}));
}

TEST(SuccessMeansCompleteTest, NothingRecordedGivesAnEmptySummary) {
  StrictMock<MockSink> sink;
  FakeQueue write;
  FrameRecorder recorder(sink, write.AsPost(), /*capacity=*/1);

  EXPECT_THAT(Finished(recorder, write), Eq(FrameRecorder::Summary{}));
}

}  // namespace
}  // namespace facepalm
