#ifndef FACEPALM_SRC_PREVIEW_LEASE_H_
#define FACEPALM_SRC_PREVIEW_LEASE_H_

#include <cstdint>
#include <functional>
#include <mutex>

namespace facepalm {

// Whether the camera preview may stream frames to the menu (#37 item 8,
// #36), following specs/PreviewLease.tla. This decides only whether to
// publish to the menu's own local channel at all; it says nothing about
// the approved-token service every other client reaches
// (src/approved_feed.h), which never carries a frame either way.
//
// The menu must ask again after every time the camera closes: an old
// grant does not carry over to a later session, so a preview can never
// resume on its own (GuardStartNeedsCamera, together with the session
// check PreviewLease!Start and PreviewLease!Stop implement).
//
// Unlike CameraController and DaemonControl, this is called from more
// than one queue: CameraOpened/CameraClosed from the control queue
// (DaemonAdmission's own subscription signal), Start/Stop/IsStreaming
// from PreviewPublisher on the frame-capture path. A mutex, not a single-
// queue convention, is what keeps it safe.
class PreviewLease {
 public:
  // `streaming_changed` is told the new value only when it changes.
  explicit PreviewLease(std::function<void(bool)> streaming_changed);

  // PreviewLease!CameraOpens: DaemonAdmission.tla's own first-join, as
  // the daemon already tracks it. Starts a new session: a grant from
  // before this call no longer matches it.
  void CameraOpened();
  // PreviewLease!CameraCloses: DaemonAdmission.tla's own last-leave.
  // Ends any preview at once, same as the indicator (#37 item 8)
  // stopping with it.
  void CameraClosed();

  // PreviewLease!Start: the menu's own request, such as a new
  // connection to its preview channel. False, granting nothing, when
  // the camera is not open.
  bool Start();
  // PreviewLease!Stop: the menu's own request, or its connection to the
  // preview channel closing.
  void Stop();

  [[nodiscard]] bool IsStreaming() const;

 private:
  // Call with mutex_ held; returns whether the value changed.
  bool SetStreaming(bool streaming);

  std::function<void(bool)> streaming_changed_;
  mutable std::mutex mutex_;
  bool camera_open_ = false;           // Under mutex_.
  std::uint64_t camera_session_ = 0;   // Under mutex_.
  std::uint64_t started_session_ = 0;  // Under mutex_; 0: no real session.
  bool streaming_ = false;             // Under mutex_.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_PREVIEW_LEASE_H_
