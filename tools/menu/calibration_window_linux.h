#ifndef FACEPALM_TOOLS_MENU_CALIBRATION_WINDOW_LINUX_H_
#define FACEPALM_TOOLS_MENU_CALIBRATION_WINDOW_LINUX_H_

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <memory>
#include <string>

#include "src/calibrate_cli.h"
#include "src/calibration_flow.h"

namespace facepalm_menu {

// A small Qt window around src/calibration_flow.h's CalibrationFlow
// (#118, #36, #35, item 12 of #61): kept apart from main() the way
// tools/menu/main_linux.cc's CameraPreview already is (#64), so this
// adds nothing to main()'s own complexity. Every decision about what to
// send and show is CalibrationFlow's; this only renders its
// CurrentView() and forwards button clicks and the window's close.
class CalibrationWindow {
 public:
  // `socket_path` is resolved by the caller (src/user_paths.h); fixed
  // for the tray's own lifetime, the way CameraPreview's request and
  // frame paths are.
  CalibrationWindow(QWidget* parent, std::string socket_path);

  // Opens the wizard: a plain explanation that calibration is optional,
  // with a button to begin it. Raises the window instead of starting a
  // second session if one is already open. `token` is the "calibrate"
  // client's own token, approved fresh on each open the way
  // SwitchService approves its own client in main_linux.cc -- not once
  // at launch, so running the tray never approves a client the person
  // has not asked to use yet.
  void Open(std::string token);

 private:
  void Render();
  // Moves the window onto `view`'s own screen (#135): only a screen's
  // point step names one.
  void MoveToScreenForStep(const facepalm::CalibrationFlow::View& view);
  void PrimaryClicked();
  void SecondaryClicked();
  void WindowClosed();

  QDialog window_;
  QLabel* label_;
  QPushButton* primary_;
  QPushButton* secondary_;
  const std::string socket_path_;
  std::unique_ptr<facepalm::CalibrateClientIo> io_;
  std::unique_ptr<facepalm::CalibrationFlow> flow_;
  bool started_ = false;
};

}  // namespace facepalm_menu

#endif  // FACEPALM_TOOLS_MENU_CALIBRATION_WINDOW_LINUX_H_
