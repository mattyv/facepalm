#ifndef FACEPALM_SRC_FRAME_PIPELINE_H_
#define FACEPALM_SRC_FRAME_PIPELINE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/camera_controller.h"
#include "src/frame.h"
#include "src/frame_analyzer.h"
#include "src/post.h"

namespace facepalm {

// Takes each captured frame to the face model, as specs/FramePipeline.tla
// specifies. Three serial queues share one frame slot:
//
//   capture:   OnFrame copies the frame into the slot as RGBA, only when the
//              slot is free; a frame that arrives while it is taken is
//              dropped;
//   control:   the controller's queue, where the frame gets a ticket, and
//              where OnCameraOpen and OnCameraClosed are called;
//   inference: makes the analysers, runs them on a ticketed frame, and
//              destroys them, in the order they were asked for. The
//              analysers together are the spec's model.
//
// Each method names the spec action it implements. Tasks it posts hold a
// pointer to it, so it outlives the queues' work.
class FramePipeline {
 public:
  using Analyzers = std::vector<std::unique_ptr<FrameAnalyzer>>;
  // Makes the analysers, the spec's model. Returns those it could make, and
  // sets `error` when one could not be made.
  using AnalyzerFactory = std::function<Analyzers(std::string& error)>;
  // Hears why making an analyser or analysing a frame failed, on the
  // inference queue.
  using OnError = std::function<void(const std::string& error)>;

  struct Callbacks {
    AnalyzerFactory make_analyzers;
    OnError on_error;
  };

  FramePipeline(FrameTickets& tickets, Post control, Post inference,
                Callbacks callbacks);

  // The camera is open: OnOpened, not when opening starts, so a camera that
  // fails to open leaves no model behind (spec: Open).
  void OnCameraOpen();
  // The camera has stopped: OnClosed and OnDeviceLost both, every way the
  // controller leaves open (spec: Close). A dead helper takes the model with
  // it.
  // Posts completion to control only after the analysers are destroyed.
  void OnCameraClosed(std::function<void()> on_closed = {});

  // A captured frame, on the capture queue. Valid only during the call
  // (spec: CaptureStart and CaptureEnd).
  void OnFrame(const Frame& frame);

 private:
  enum class Slot : std::uint8_t { kFree, kCopied, kTicketed };

  void Ticket();
  void CreateModel();
  void DestroyModel();
  void Detect();
  void Free();

  FrameTickets& tickets_;
  Post control_;
  Post inference_;
  Callbacks callbacks_;
  // Who owns the slot's buffer: capture while kFree, control while kCopied,
  // inference while kTicketed. Posting a task hands it from one queue to the
  // next; the release store in Free hands it back to capture, which checks
  // it with an acquire load.
  std::atomic<Slot> slot_ = Slot::kFree;
  std::vector<std::uint8_t> rgba_;
  int width_ = 0;
  int height_ = 0;
  std::int64_t timestamp_ms_ = 0;
  std::uint64_t ticket_ = 0;
  Analyzers analyzers_;  // Inference queue only.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FRAME_PIPELINE_H_
