#include "src/frame_pipeline.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/frame.h"
#include "src/post.h"

namespace facepalm {

FramePipeline::FramePipeline(FrameTickets& tickets, Post control,
                             Post inference, Callbacks callbacks)
    : tickets_(tickets),
      control_(std::move(control)),
      inference_(std::move(inference)),
      callbacks_(std::move(callbacks)) {}

// specs/FramePipeline.tla: Open, which queues the model's create.
void FramePipeline::OnCameraOpen() {
  inference_([this] { CreateModel(); });
}

// specs/FramePipeline.tla: Close, with GuardOneQueue and GuardDestroyOnClose:
// the destroy waits on the inference queue behind any detect.
void FramePipeline::OnCameraClosed() {
  inference_([this] { DestroyModel(); });
}

// specs/FramePipeline.tla: CaptureStart and CaptureEnd, with GuardOneSlot.
void FramePipeline::OnFrame(const Frame& frame) {
  if (slot_.load(std::memory_order_acquire) != Slot::kFree) {
    return;
  }
  ToRgba(frame, rgba_);
  width_ = frame.width;
  height_ = frame.height;
  timestamp_ms_ = frame.timestamp_ms;
  slot_.store(Slot::kCopied, std::memory_order_release);
  control_([this] { Ticket(); });
}

// specs/FramePipeline.tla: Ticket, with GuardFreeOnRefusal. A refused frame's
// pixels are zeroed before the slot is freed, so a frame captured around the
// camera stopping does not stay in memory. A ticketed frame's pixels stay in
// the slot after its detect, until the next frame overwrites them.
void FramePipeline::Ticket() {
  const std::optional<std::uint64_t> ticket = tickets_.OnFrameCaptured();
  if (!ticket) {
    std::ranges::fill(rgba_, 0);
    Free();
    return;
  }
  ticket_ = *ticket;
  slot_.store(Slot::kTicketed, std::memory_order_release);
  inference_([this] { Detect(); });
}

// specs/FramePipeline.tla: CreateModel.
void FramePipeline::CreateModel() {
  std::string error;
  model_ = callbacks_.make_model(error);
  if (!model_) {
    callbacks_.on_error(error);
  }
}

// specs/FramePipeline.tla: DestroyModel.
void FramePipeline::DestroyModel() { model_.reset(); }

// specs/FramePipeline.tla: DetectStart with GuardSkipAbsent, then DetectEnd,
// or DetectFails with GuardFreeOnError. The reaction goes to the control
// queue with the frame's ticket.
void FramePipeline::Detect() {
  if (!model_) {
    Free();
    return;
  }
  std::string error;
  const FaceModel::Result result =
      model_->Detect(rgba_, width_, height_, timestamp_ms_, face_, error);
  if (result == FaceModel::Result::kFailed) {
    callbacks_.on_error(error);
  } else if (result == FaceModel::Result::kFace) {
    std::optional<Reaction> reaction = callbacks_.react(face_);
    if (reaction) {
      control_([this, ticket = ticket_, reaction = std::move(*reaction)] {
        tickets_.OnReaction(ticket, reaction);
      });
    }
  }
  Free();
}

void FramePipeline::Free() {
  slot_.store(Slot::kFree, std::memory_order_release);
}

}  // namespace facepalm
