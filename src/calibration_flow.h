#ifndef FACEPALM_SRC_CALIBRATION_FLOW_H_
#define FACEPALM_SRC_CALIBRATION_FLOW_H_

#include <cstdint>
#include <optional>
#include <string>

#include "src/calibrate_cli.h"
#include "src/calibration.h"

namespace facepalm {

// Drives one calibration wizard from a menu's Settings window (#118, #36,
// #35, item 12 of #61), against a live daemon over CalibrateClientIo
// (src/calibrate_cli.h, the same seam calibrate_cli uses), following
// specs/CalibrationMenu.tla: the one place a wizard decides what request
// to send and how to read the daemon's reply, so tools/menu/main.mm and
// main_linux.cc (#64) stay thin AppKit/Qt glue around it. One instance is
// good for one wizard run: Start is called once, right after
// construction, and the window this flow drives is torn down once
// CurrentView().state leaves kPrompting.
class CalibrationFlow {
 public:
  enum class State : std::uint8_t {
    kClosed,     // CalibrationMenu.tla's "closed": nothing to show yet, or
                 // a cancel the daemon confirmed; close the window now.
    kPrompting,  // "prompting": a step is shown, waiting on Next or Cancel.
    kDone,       // "done": the last step finished and the profile saved.
    kFailed,     // "failed": the connection or the daemon refused.
  };

  // What the window shows; valid fields depend on state.
  struct View {
    State state = State::kClosed;
    // Set only while state == kPrompting.
    bool resting_step = false;
    int screen = 0;
    CalibrationPoint point = CalibrationPoint::kCentre;
    int index = 0;
    int total = 0;
    // Set only while state == kFailed.
    std::string error;
  };

  // `socket_path` and `token` are resolved by the caller (src/user_paths.h,
  // the "calibrate" client's own token, approved the same way
  // SwitchService approves its own client in tools/menu/main.mm and
  // main_linux.cc); `io` is connected fresh by Start and must outlive this.
  CalibrationFlow(std::string socket_path, std::string token,
                  CalibrateClientIo& io);

  // CalibrationMenu.tla's OpenSucceeds/OpenFails: connects, says hello,
  // and starts a session of `screens` screens (CalibrationSession::kMaxScreens
  // at most). Called once, right after construction.
  void Start(int screens);
  // CalibrationMenu.tla's NextContinues/NextFinishes/NextFails: sends
  // "calibrate next". A no-op unless CurrentView().state == kPrompting.
  void Next();
  // CalibrationMenu.tla's UserCancel, followed by CancelSucceeds or
  // CancelFails: sends "calibrate cancel" and waits for its reply. A no-op
  // unless CurrentView().state == kPrompting.
  void Cancel();
  // CalibrationMenu.tla's WindowClosed: the window's own close control was
  // used while a step was showing, rather than the Cancel button; exactly
  // Cancel(), under its own name so the call site shows which one happened
  // (main.mm, main_linux.cc). Proven by CalibrationMenu.tla's
  // NoOrphanOnClose never to leave a session running with the window gone.
  void WindowClosed();

  [[nodiscard]] const View& CurrentView() const { return view_; }

 private:
  void Fail(std::string error);
  void ApplyReply(const std::optional<std::string>& reply);
  void SendCancelAndClose();

  std::string socket_path_;
  std::string token_;
  CalibrateClientIo& io_;
  View view_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATION_FLOW_H_
