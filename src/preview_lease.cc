#include "src/preview_lease.h"

#include <utility>

namespace facepalm {

PreviewLease::PreviewLease(std::function<void(bool)> streaming_changed)
    : streaming_changed_(std::move(streaming_changed)) {}

void PreviewLease::CameraOpened() {
  bool changed = false;
  {
    const std::scoped_lock lock(mutex_);
    camera_open_ = true;
    changed = SetStreaming(false);
  }
  if (changed) {
    streaming_changed_(false);
  }
}

void PreviewLease::CameraClosed() {
  bool changed = false;
  {
    const std::scoped_lock lock(mutex_);
    camera_open_ = false;
    changed = SetStreaming(false);
  }
  if (changed) {
    streaming_changed_(false);
  }
}

bool PreviewLease::Start() {
  bool changed = false;
  bool granted = false;
  {
    const std::scoped_lock lock(mutex_);
    if (camera_open_) {
      changed = SetStreaming(true);
      granted = true;
    }
  }
  if (changed) {
    streaming_changed_(true);
  }
  return granted;
}

void PreviewLease::Stop() {
  bool changed = false;
  {
    const std::scoped_lock lock(mutex_);
    changed = SetStreaming(false);
  }
  if (changed) {
    streaming_changed_(false);
  }
}

bool PreviewLease::IsStreaming() const {
  const std::scoped_lock lock(mutex_);
  return streaming_;
}

// Called with mutex_ already held; never calls streaming_changed_ itself,
// since that could run arbitrary code (PreviewPublisher's write to the
// menu's frame file) while holding the lock. Returns whether the value
// changed, so the caller can invoke streaming_changed_ once it has
// released mutex_.
bool PreviewLease::SetStreaming(bool streaming) {
  if (streaming == streaming_) {
    return false;
  }
  streaming_ = streaming;
  return true;
}

}  // namespace facepalm
