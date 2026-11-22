#include "src/window_activation.h"

namespace facepalm {

ActivationPolicyChange WindowActivationTracker::WindowOpened() {
  ++open_count_;
  return open_count_ == 1 ? ActivationPolicyChange::kBecomeRegular
                          : ActivationPolicyChange::kNone;
}

ActivationPolicyChange WindowActivationTracker::WindowClosed() {
  if (open_count_ == 0) {
    return ActivationPolicyChange::kNone;
  }
  --open_count_;
  return open_count_ == 0 ? ActivationPolicyChange::kBecomeAccessory
                          : ActivationPolicyChange::kNone;
}

}  // namespace facepalm
