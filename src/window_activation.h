#ifndef FACEPALM_SRC_WINDOW_ACTIVATION_H_
#define FACEPALM_SRC_WINDOW_ACTIVATION_H_

#include <cstdint>

namespace facepalm {

// What tools/menu/main.mm must do to NSApp's activation policy, from
// WindowActivationTracker's count (#146): a menu-bar-only accessory app
// never reaches Command-Tab, the Dock or the app switcher, so Facepalm
// switches to a regular app the moment Settings, the camera preview or
// the calibration wizard first opens, and back to accessory the moment
// the last of them closes.
enum class ActivationPolicyChange : std::uint8_t {
  kNone,
  kBecomeRegular,
  kBecomeAccessory
};

// Counts how many of Settings, the camera preview and the calibration
// wizard are open right now, and decides when main.mm must change NSApp's
// activation policy (#146, #64): kept apart from FacepalmApp so the
// counting is tested on its own, the way tools/menu/main_linux.cc's
// CameraPreview is kept apart from main() there.
class WindowActivationTracker {
 public:
  // Called once, each time one of the tracked windows opens. Returns
  // kBecomeRegular the first time (0 -> 1 open), kNone otherwise.
  ActivationPolicyChange WindowOpened();

  // Called once, each time one of the tracked windows closes, however it
  // closed. Returns kBecomeAccessory when this was the last one open (1
  // -> 0), kNone otherwise -- including a close with none open, which a
  // correct caller never sends, rather than letting the count go
  // negative.
  ActivationPolicyChange WindowClosed();

  // How many tracked windows are open right now.
  [[nodiscard]] int OpenCount() const { return open_count_; }

 private:
  int open_count_ = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_WINDOW_ACTIVATION_H_
