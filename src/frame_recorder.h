#ifndef FACEPALM_SRC_FRAME_RECORDER_H_
#define FACEPALM_SRC_FRAME_RECORDER_H_

#include <atomic>
#include <cstddef>
#include <functional>

#include "src/frame.h"
#include "src/post.h"

namespace facepalm {

// Saves one recorded frame: its image, and its capture time in the
// recording's manifest.
class FrameSink {
 public:
  virtual ~FrameSink() = default;
  // Saves frame number `index`, counting from 1, whose pixels are valid only
  // during the call. Returns whether both its image and its time were saved.
  virtual bool Save(int index, const Frame& frame) = 0;
};

// Records camera_probe's frames, as specs/ProbeShutdown.tla specifies (#41):
// the capture queue copies each frame onto a serial write queue, which saves
// it through a FrameSink. At most `capacity` copies wait there; a frame
// that arrives while they do is dropped, so a stalled disk cannot take all
// the memory. Finish reports, once every accepted frame has been saved or
// has failed, how many were saved, failed and dropped.
class FrameRecorder {
 public:
  // What became of the frames recorded.
  struct Summary {
    int saved = 0;
    int failed = 0;
    int dropped = 0;

    friend bool operator==(const Summary&, const Summary&) = default;
  };

  FrameRecorder(FrameSink& sink, Post write, std::size_t capacity);

  // Copies `frame` and queues it to be saved, or drops it when the backlog
  // is full. Capture queue only.
  void Record(const Frame& frame);

  // Calls `done` on `control` with the summary, after every frame recorded
  // so far is saved or has failed. Call it once the camera has stopped.
  void Finish(Post control, std::function<void(const Summary&)> done);

 private:
  FrameSink& sink_;
  Post write_;
  std::size_t capacity_;
  std::atomic<std::size_t> pending_ = 0;  // Copies on the write queue.
  int accepted_ = 0;                      // Capture queue only.
  std::atomic<int> dropped_ = 0;
  int saved_ = 0;   // Write queue only.
  int failed_ = 0;  // Write queue only.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FRAME_RECORDER_H_
