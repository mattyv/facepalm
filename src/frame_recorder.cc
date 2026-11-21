#include "src/frame_recorder.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "src/frame.h"
#include "src/post.h"

namespace facepalm {

FrameRecorder::FrameRecorder(FrameSink& sink, Post write, std::size_t capacity)
    : sink_(sink), write_(std::move(write)), capacity_(capacity) {}

// specs/ProbeShutdown.tla: Capture, with GuardBound. Only this queue adds to
// pending_, so the check and the add cannot be overtaken.
void FrameRecorder::Record(const Frame& frame) {
  if (pending_.load() >= capacity_) {
    ++dropped_;
    return;
  }
  ++pending_;
  const int index = ++accepted_;
  // Shared, since a Post's task must be copyable.
  const auto pixels = std::make_shared<const std::vector<std::uint8_t>>(
      frame.bgra, frame.bgra + (frame.bytes_per_row *
                                static_cast<std::size_t>(frame.height)));
  Frame copy = frame;
  copy.bgra = nullptr;
  // specs/ProbeShutdown.tla: WriteSaves or WriteFails.
  write_([this, index, copy, pixels]() mutable {
    copy.bgra = pixels->data();
    if (sink_.Save(index, copy)) {
      ++saved_;
    } else {
      ++failed_;
    }
    --pending_;
  });
}

// specs/ProbeShutdown.tla: FinishRuns, with GuardDrain: the write queue is
// serial, so this task runs behind every write queued before it.
void FrameRecorder::Finish(Post control,
                           std::function<void(const Summary&)> done) {
  write_([this, control = std::move(control), done = std::move(done)] {
    const Summary summary{
        .saved = saved_, .failed = failed_, .dropped = dropped_.load()};
    control([done, summary] { done(summary); });
  });
}

}  // namespace facepalm
