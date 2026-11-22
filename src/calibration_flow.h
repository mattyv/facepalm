#ifndef FACEPALM_SRC_CALIBRATION_FLOW_H_
#define FACEPALM_SRC_CALIBRATION_FLOW_H_

#include <chrono>
#include <cstdint>
#include <functional>
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
    bool gains_step = false;  // #137's fixate-and-turn step.
    int screen = 0;
    CalibrationPoint point = CalibrationPoint::kCentre;
    int index = 0;
    int total = 0;
    // Set only while state == kDone (#135, #137): the result line the
    // wizard ends on, "Calibrated N screens."
    int screens = 0;
    // Set only while state == kFailed.
    std::string error;
  };

  // Sleeps between "calibrate start" retries while CalibrationMenu.tla's
  // "opening" waits for the daemon's camera to come up (#128); real
  // callers pass std::this_thread::sleep_for, a test a no-op that counts
  // or records its calls, since this runs on the caller's own thread
  // (main.mm, main_linux.cc), blocking it for as long as Start retries.
  using Wait = std::function<void(std::chrono::milliseconds)>;

  // How many times, kCameraWaitInterval apart, Start retries "calibrate
  // start" while it is refused only because the camera is not up yet,
  // before giving up (specs/CalibrationMenu.tla's MaxCameraWaits).
  static constexpr int kMaxCameraWaits = 10;
  static constexpr std::chrono::milliseconds kCameraWaitInterval{200};

  // `socket_path` and `token` are resolved by the caller (src/user_paths.h,
  // the "calibrate" client's own token, approved the same way
  // SwitchService approves its own client in tools/menu/main.mm and
  // main_linux.cc); `io` is connected fresh by Start and must outlive this.
  // `wait` defaults to a real sleep; tests replace it.
  CalibrationFlow(std::string socket_path, std::string token,
                  CalibrateClientIo& io, Wait wait = DefaultWait);

  // CalibrationMenu.tla's OpenSucceeds/OpenFails/OpenWaitsForCamera/
  // RetrySucceeds/RetryWaits/RetryGivesUp: connects, says hello, and
  // starts a session of `screens` screens (CalibrationSession::kMaxScreens
  // at most), retrying "calibrate start" while the daemon's own camera is
  // not up yet, up to kMaxCameraWaits times, rather than failing outright
  // on a cold start (#128). Called once, right after construction.
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
  static void DefaultWait(std::chrono::milliseconds duration);

  void Fail(std::string error);
  // Reads the daemon's next reply and parses it the way
  // ParseCalibrationReply (calibration_flow.cc) does; nullopt, with
  // `error`, on a hang-up or a refusal, for the caller to decide what to
  // do about.
  std::optional<CalibrationController::Status> ReadStatus(std::string& error);
  // Applies a status ReadStatus already found: CalibrationMenu.tla's
  // NextContinues/NextFinishes, or a defensive Fail for a status this
  // flow never asked the daemon to send.
  void ApplyStatus(const CalibrationController::Status& status);
  void SendCancelAndClose();

  std::string socket_path_;
  std::string token_;
  CalibrateClientIo& io_;
  Wait wait_;
  View view_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATION_FLOW_H_
