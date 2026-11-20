#include "src/frame_pipeline.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/camera_controller.h"
#include "src/frame.h"
#include "src/frame_analyzer.h"
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
void FramePipeline::OnCameraClosed(std::function<void()> on_closed) {
  inference_([this, on_closed = std::move(on_closed)] {
    DestroyModel();
    if (on_closed) {
      control_(on_closed);
    }
  });
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
  attempt_ = frame.attempt;
  slot_.store(Slot::kCopied, std::memory_order_release);
  control_([this] { Ticket(); });
}

// specs/FramePipeline.tla: Ticket, with GuardFreeOnRefusal.
void FramePipeline::Ticket() {
  // CameraLifecycle's Ticket, with GuardTicketChecksCapture: the attempt
  // was stamped at capture, so a delay here cannot move the frame into a
  // later one (#40).
  const std::optional<std::uint64_t> ticket =
      tickets_.OnFrameCaptured(attempt_);
  if (!ticket) {
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
  analyzers_ = callbacks_.make_analyzers(error);
  if (!error.empty()) {
    callbacks_.on_error(error);
  }
}

// specs/FramePipeline.tla: DestroyModel.
void FramePipeline::DestroyModel() { analyzers_.clear(); }

// specs/FramePipeline.tla: DetectStart with GuardSkipAbsent, then DetectEnd,
// or DetectFails with GuardFreeOnError: one analyser failing reports its
// error and leaves the others to run. Each reaction goes to the control
// queue with the frame's ticket.
void FramePipeline::Detect() {
  std::vector<Reaction> reactions;
  for (const std::unique_ptr<FrameAnalyzer>& analyzer : analyzers_) {
    std::string error;
    if (!analyzer->Analyze(rgba_, width_, height_, timestamp_ms_, reactions,
                           error)) {
      callbacks_.on_error(error);
    }
  }
  for (Reaction& reaction : reactions) {
    control_([this, ticket = ticket_, reaction = std::move(reaction)] {
      tickets_.OnReaction(ticket, reaction);
    });
  }
  Free();
}

// Wipes the frame's pixels before freeing the slot, however the frame ends,
// so no image of the user outlives its use.
void FramePipeline::Free() {
  std::ranges::fill(rgba_, 0);
  slot_.store(Slot::kFree, std::memory_order_release);
}

}  // namespace facepalm
