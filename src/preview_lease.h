#ifndef FACEPALM_SRC_PREVIEW_LEASE_H_
#define FACEPALM_SRC_PREVIEW_LEASE_H_

#include <functional>
#include <mutex>

namespace facepalm {

// Whether the camera preview may stream frames to the menu (#37 item 8,
// #36, #114), following specs/PreviewLease.tla. This decides only
// whether to publish to the menu's own local channel at all; it says
// nothing about the approved-token service every other client reaches
// (src/approved_feed.h), which never carries a frame either way.
//
// The menu must ask again after every time the camera closes: an old
// grant does not carry over, so a preview can never resume on its own
// (GuardStartNeedsCamera). CameraOpened and CameraClosed both force
// streaming off on their own, directly, rather than through a session
// counter a later Start or Frame check would have to compare against:
// an earlier revision of this class kept one (camera_session_,
// started_session_), found write-only in the #61 item 8 Fable review
// (#114) -- CameraOpened's and CameraClosed's own direct reset already
// made the comparison it would have made redundant, for the same reason
// PreviewLease.tla's own CameraOpens and CameraCloses set `streaming`
// directly rather than through its Fresh formula.
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
  // the daemon already tracks it. Ends any preview a stale grant might
  // otherwise carry over.
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

  // PreviewLease!Publish (#129): PreviewPublisher::OnFrame's own publish
  // step, once it has encoded a frame. Calls `write` only while still
  // streaming, and -- unlike a plain IsStreaming() check before the
  // call -- does so under the same mutex_ every transition to not
  // streaming already takes before clearing the frame file through
  // streaming_changed_, so a CameraOpened, CameraClosed or Stop that
  // lands while an encode that began on a different queue was still
  // running can never have its clear undone by that call's own write
  // landing afterward: whichever runs first under the lock, the other
  // sees the result it left, not the one it started from. Returns
  // FALSE, without calling `write`, when not streaming; otherwise
  // `write`'s own result.
  bool Publish(const std::function<bool()>& write);

 private:
  // Call with mutex_ held; returns whether the value changed.
  bool SetStreaming(bool streaming);

  std::function<void(bool)> streaming_changed_;
  mutable std::mutex mutex_;
  bool camera_open_ = false;  // Under mutex_.
  bool streaming_ = false;    // Under mutex_.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_PREVIEW_LEASE_H_
