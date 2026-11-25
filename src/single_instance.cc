#include "src/single_instance.h"

namespace facepalm {

// specs/SingleInstance.tla: TryLockAtomic, TryLockHeld, Forward and
// ForwardLost. TryLock is QLockFile::tryLock() in the real tray: one
// atomic call, so no other launch can observe the lock as free in
// between seeing it that way and claiming it (GuardAtomicClaim), and one
// that also reclaims a dead owner's lock rather than wedging every later
// launch (GuardStaleReclaimed). A launch that loses the lock only talks
// to the owner when it was asked for Settings itself
// (GuardForwardOnSettings); otherwise it exits without starting a
// redundant tray.
SingleInstanceRole ClaimSingleInstance(bool wants_settings,
                                       SingleInstanceIo& io) {
  for (int attempt = 0; attempt < kMaxSingleInstanceAttempts; ++attempt) {
    if (io.TryLock()) {
      return SingleInstanceRole::kPrimary;
    }
    if (!wants_settings) {
      return SingleInstanceRole::kForwarded;
    }
    if (io.RequestSettings()) {
      return SingleInstanceRole::kForwarded;
    }
    // ForwardLost: no valid acknowledgement arrived. Try again: the
    // owner may have quit, leaving the lock available.
  }
  return SingleInstanceRole::kHandoffFailed;
}

}  // namespace facepalm
