#ifndef FACEPALM_SRC_FRAME_PIPELINE_H_
#define FACEPALM_SRC_FRAME_PIPELINE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/frame.h"
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
//   inference: creates the model, detects on a ticketed frame, and destroys
//              the model, in the order they were asked for.
//
// Each method names the spec action it implements. Tasks it posts hold a
// pointer to it, so it outlives the queues' work.
class FramePipeline {
 public:
  // Makes a face model, or returns null and sets `error`.
  using ModelFactory =
      std::function<std::unique_ptr<FaceModel>(std::string& error)>;
  // Makes a reaction from a face, or nothing when there is none to send.
  using React = std::function<std::optional<Reaction>(const Face& face)>;
  // Hears why loading the model or a detect failed, on the inference queue.
  using OnError = std::function<void(const std::string& error)>;

  struct Callbacks {
    ModelFactory make_model;
    React react;
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
  void OnCameraClosed();

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
  std::unique_ptr<FaceModel> model_;  // Inference queue only.
  Face face_{};                       // Inference queue only.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FRAME_PIPELINE_H_
