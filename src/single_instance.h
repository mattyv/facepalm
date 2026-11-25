#ifndef FACEPALM_SRC_SINGLE_INSTANCE_H_
#define FACEPALM_SRC_SINGLE_INSTANCE_H_

#include <cstdint>

namespace facepalm {

// What launching facepalm-menu decides, following specs/SingleInstance.tla:
// this process claimed the lock and becomes the tray, or it handed off to
// the one already running, or the settings handoff failed.
enum class SingleInstanceRole : std::uint8_t {
  kPrimary,
  kForwarded,
  kHandoffFailed
};

// At most this many round trips through the lock before giving up on a
// handoff that fails (ForwardLost in specs/SingleInstance.tla), including
// when the owner is alive but unresponsive.
inline constexpr int kMaxSingleInstanceAttempts = 3;

// The primitives the guard needs. The Linux tray's implementation claims a
// QLockFile under the user's state directory (src/user_paths.h) and
// forwards over a QLocalServer/QLocalSocket; this interface lets
// ClaimSingleInstance be tested without either.
class SingleInstanceIo {
 public:
  virtual ~SingleInstanceIo() = default;
  // Claims the lock in one atomic step (TryLockAtomic): true when this
  // call wins it, because it was free or its owner has since died; false
  // when a live instance already holds it.
  virtual bool TryLock() = 0;
  // Asks the instance that holds the lock to raise its Settings window
  // (TryLock must have just returned false). True only when the owner
  // acknowledges processing the request; false on timeout, disconnect, or an
  // invalid acknowledgement.
  virtual bool RequestSettings() = 0;
};

// specs/SingleInstance.tla: TryLockAtomic, TryLockHeld, Forward and
// ForwardLost. Claims the lock through `io`, becoming the tray; or, when
// it is already held, asks the running instance to show Settings if
// `wants_settings` was asked for (--settings), retrying while the handoff
// fails, up to kMaxSingleInstanceAttempts, then reports failure.
SingleInstanceRole ClaimSingleInstance(bool wants_settings,
                                       SingleInstanceIo& io);

}  // namespace facepalm

#endif  // FACEPALM_SRC_SINGLE_INSTANCE_H_
